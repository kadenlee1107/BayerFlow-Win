/* dncnn_cuda.cu — DnCNN spatial denoiser.
 * Two modes:
 *   1. ONNX Runtime CPU (preferred — fast via oneDNN, frees GPU for RAFT)
 *   2. Hand-written CUDA kernels (fallback if dncnn.onnx not found)
 * 7-layer 3x3 conv + ReLU, single-channel (Bayer sub-channel). */

#include <cuda_runtime.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "onnxruntime_c_api.h"

/* ---- ONNX Runtime CPU session for DnCNN ---- */
static const OrtApi *g_dncnn_ort = NULL;
static OrtSession *g_dncnn_session = NULL;
static OrtSessionOptions *g_dncnn_sopts = NULL;
static OrtEnv *g_dncnn_env = NULL;
static OrtMemoryInfo *g_dncnn_mem = NULL;
static int g_use_ort_cpu = 0;  /* 1 = ONNX CPU mode, 0 = CUDA kernel mode */

/* ---- DnCNN layer data ---- */
typedef struct {
    float *d_weight;  /* GPU: [outCh, inCh, 3, 3] */
    float *d_bias;    /* GPU: [outCh] */
    int outCh, inCh;
} DnCNNLayer;

static DnCNNLayer *g_layers = NULL;
static int g_num_layers = 0;
static int g_in_channels = 0;
static int g_hidden_channels = 0;
static float *g_buf[2] = {NULL, NULL};
static int g_buf_size = 0;

/* Persistent GPU buffers */
static float *g_d_sc = NULL;      /* sub-channel input */
static float *g_d_resid = NULL;   /* sub-channel residual output */
static uint16_t *g_d_bayer = NULL; /* full Bayer frame on GPU */
static int g_bayer_alloc = 0;
static int g_sc_alloc = 0;

/* ---- 3x3 Convolution + Bias + optional ReLU kernel ---- */
__global__ void conv3x3_relu_kernel(
    const float * __restrict__ input,
    const float * __restrict__ weight,
    const float * __restrict__ bias,
    float *output,
    int H, int W, int inCh, int outCh, int apply_relu)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int oc = blockIdx.z;
    if (x >= W || y >= H || oc >= outCh) return;

    float sum = bias[oc];
    for (int ic = 0; ic < inCh; ic++) {
        for (int ky = -1; ky <= 1; ky++) {
            int iy = y + ky;
            if (iy < 0) iy = 0;
            if (iy >= H) iy = H - 1;
            for (int kx = -1; kx <= 1; kx++) {
                int ix = x + kx;
                if (ix < 0) ix = 0;
                if (ix >= W) ix = W - 1;
                sum += input[ic * H * W + iy * W + ix] *
                       weight[oc * inCh * 9 + ic * 9 + (ky + 1) * 3 + (kx + 1)];
            }
        }
    }
    if (apply_relu && sum < 0.0f) sum = 0.0f;
    output[oc * H * W + y * W + x] = sum;
}

/* ---- GPU kernel: extract Bayer sub-channel to float [0,1] ---- */
__global__ void extract_subchannel_k(
    const uint16_t * __restrict__ bayer, float *output,
    int width, int dx, int dy, int scw, int sch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= scw || y >= sch) return;
    output[y * scw + x] = (float)bayer[(y * 2 + dy) * width + (x * 2 + dx)] * (1.0f / 65535.0f);
}

/* ---- GPU kernel: blend residual back into Bayer ---- */
__global__ void blend_residual_k(
    uint16_t *bayer, const float * __restrict__ input,
    const float * __restrict__ residual, float blend,
    int width, int dx, int dy, int scw, int sch)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= scw || y >= sch) return;
    int i = y * scw + x;
    float d = input[i] - blend * residual[i];
    if (d < 0.0f) d = 0.0f;
    if (d > 1.0f) d = 1.0f;
    bayer[(y * 2 + dy) * width + (x * 2 + dx)] = (uint16_t)(d * 65535.0f + 0.5f);
}

/* ---- Try ONNX init (GPU if RAFT disabled, otherwise CPU) ---- */
static int try_onnx_init(const char *bin_path) {
    /* Derive dncnn.onnx path from the .bin path */
    char onnx_path[512];
    const char *dir_end = strrchr(bin_path, '\\');
    if (!dir_end) dir_end = strrchr(bin_path, '/');
    if (dir_end) {
        int dirlen = (int)(dir_end - bin_path);
        snprintf(onnx_path, sizeof(onnx_path), "%.*s\\dncnn.onnx", dirlen, bin_path);
    } else {
        snprintf(onnx_path, sizeof(onnx_path), "dncnn.onnx");
    }

    FILE *test = fopen(onnx_path, "rb");
    if (!test) return -1;
    fclose(test);

    g_dncnn_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_dncnn_ort) return -1;

    g_dncnn_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "dncnn", &g_dncnn_env);
    g_dncnn_ort->CreateSessionOptions(&g_dncnn_sopts);
    g_dncnn_ort->SetSessionGraphOptimizationLevel(g_dncnn_sopts, ORT_ENABLE_ALL);

    /* Check if GPU is available for DnCNN (when RAFT ONNX is disabled) */
    const char *no_onnx_raft = getenv("BAYERFLOW_NO_ONNX_RAFT");
    int try_gpu = (no_onnx_raft && no_onnx_raft[0] == '1');

    if (try_gpu) {
        /* Try CUDA EP — full GPU available since RAFT uses Python server */
        OrtCUDAProviderOptionsV2 *cuda_opts = NULL;
        OrtStatus *cs = g_dncnn_ort->CreateCUDAProviderOptions(&cuda_opts);
        if (cs == NULL) {
            const char *keys[] = {"device_id", "arena_extend_strategy", "gpu_mem_limit"};
            const char *vals[] = {"0", "kSameAsRequested", "4294967296"};
            g_dncnn_ort->UpdateCUDAProviderOptions(cuda_opts, keys, vals, 3);
            cs = g_dncnn_ort->SessionOptionsAppendExecutionProvider_CUDA_V2(g_dncnn_sopts, cuda_opts);
            g_dncnn_ort->ReleaseCUDAProviderOptions(cuda_opts);
            if (cs != NULL) {
                fprintf(stderr, "DnCNN ORT: CUDA EP failed, falling back to CPU\n");
                g_dncnn_ort->ReleaseStatus(cs);
                try_gpu = 0;
            }
        }
    }

    if (!try_gpu) {
        /* CPU mode with 8 threads */
        g_dncnn_ort->SetIntraOpNumThreads(g_dncnn_sopts, 8);
    }

    wchar_t wpath[512];
    for (size_t i = 0; i <= strlen(onnx_path); i++) wpath[i] = (wchar_t)onnx_path[i];

    OrtStatus *st = g_dncnn_ort->CreateSession(g_dncnn_env, wpath, g_dncnn_sopts, &g_dncnn_session);
    if (st != NULL) {
        fprintf(stderr, "DnCNN ORT: CreateSession failed: %s\n", g_dncnn_ort->GetErrorMessage(st));
        g_dncnn_ort->ReleaseStatus(st);
        return -1;
    }

    g_dncnn_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_dncnn_mem);
    g_use_ort_cpu = 1;
    fprintf(stderr, "DnCNN ORT: loaded %s (%s)\n", onnx_path, try_gpu ? "CUDA GPU" : "CPU 8 threads");
    return 0;
}

/* ---- Run one sub-channel through ONNX CPU ---- */
static int dncnn_ort_cpu_run(const float *h_in, float *h_out, int H, int W) {
    int64_t shape[] = {1, 1, H, W};
    size_t npix = (size_t)H * W;

    OrtValue *input_tensor = NULL;
    g_dncnn_ort->CreateTensorWithDataAsOrtValue(g_dncnn_mem, (void *)h_in, npix * sizeof(float),
        shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensor);

    const char *in_names[] = {"input"};
    const char *out_names[] = {"residual"};
    OrtValue *output_tensor = NULL;

    OrtStatus *st = g_dncnn_ort->Run(g_dncnn_session, NULL,
        in_names, (const OrtValue *const *)&input_tensor, 1,
        out_names, 1, &output_tensor);

    if (st != NULL) {
        fprintf(stderr, "DnCNN ORT CPU Run error: %s\n", g_dncnn_ort->GetErrorMessage(st));
        g_dncnn_ort->ReleaseStatus(st);
        g_dncnn_ort->ReleaseValue(input_tensor);
        return -1;
    }

    float *out_data = NULL;
    g_dncnn_ort->GetTensorMutableData(output_tensor, (void **)&out_data);
    memcpy(h_out, out_data, npix * sizeof(float));

    g_dncnn_ort->ReleaseValue(input_tensor);
    g_dncnn_ort->ReleaseValue(output_tensor);
    return 0;
}

/* ---- Load weights from .bin file ---- */
extern "C" int dncnn_cuda_init(const char *weight_path) {
    /* Try ONNX CPU first (faster, frees GPU for RAFT) */
    if (try_onnx_init(weight_path) == 0) return 0;

    /* Fallback: load .bin weights for CUDA kernel path */
    FILE *f = fopen(weight_path, "rb");
    if (!f) { fprintf(stderr, "DnCNN: cannot open %s\n", weight_path); return -1; }

    char magic[4];
    uint32_t version, in_ch, hidden_ch, num_layers;
    fread(magic, 1, 4, f);
    fread(&version, 4, 1, f);
    fread(&in_ch, 4, 1, f);
    fread(&hidden_ch, 4, 1, f);
    fread(&num_layers, 4, 1, f);

    if (memcmp(magic, "DCNN", 4) != 0 || version != 1) {
        fprintf(stderr, "DnCNN: bad magic or version\n");
        fclose(f); return -1;
    }

    g_in_channels = (int)in_ch;
    g_hidden_channels = (int)hidden_ch;
    g_num_layers = (int)num_layers;
    g_layers = (DnCNNLayer *)calloc(num_layers, sizeof(DnCNNLayer));

    for (int i = 0; i < (int)num_layers; i++) {
        uint32_t w_count, b_count;
        fread(&w_count, 4, 1, f);
        float *w_cpu = (float *)malloc(w_count * sizeof(float));
        fread(w_cpu, sizeof(float), w_count, f);
        fread(&b_count, 4, 1, f);
        float *b_cpu = (float *)malloc(b_count * sizeof(float));
        fread(b_cpu, sizeof(float), b_count, f);

        g_layers[i].outCh = (int)b_count;
        g_layers[i].inCh = (i == 0) ? (int)in_ch : (int)hidden_ch;

        cudaMalloc(&g_layers[i].d_weight, w_count * sizeof(float));
        cudaMemcpy(g_layers[i].d_weight, w_cpu, w_count * sizeof(float), cudaMemcpyHostToDevice);
        cudaMalloc(&g_layers[i].d_bias, b_count * sizeof(float));
        cudaMemcpy(g_layers[i].d_bias, b_cpu, b_count * sizeof(float), cudaMemcpyHostToDevice);

        free(w_cpu);
        free(b_cpu);
    }

    fclose(f);
    fprintf(stderr, "DnCNN CUDA: loaded %d layers (%d->%dch) from %s\n",
            g_num_layers, g_in_channels, g_hidden_channels, weight_path);
    return 0;
}

extern "C" void dncnn_cuda_destroy(void) {
    for (int i = 0; i < g_num_layers; i++) {
        cudaFree(g_layers[i].d_weight);
        cudaFree(g_layers[i].d_bias);
    }
    free(g_layers); g_layers = NULL;
    cudaFree(g_buf[0]); g_buf[0] = NULL;
    cudaFree(g_buf[1]); g_buf[1] = NULL;
    cudaFree(g_d_sc); g_d_sc = NULL;
    cudaFree(g_d_resid); g_d_resid = NULL;
    cudaFree(g_d_bayer); g_d_bayer = NULL;
    g_num_layers = 0;
}

/* ---- Run DnCNN on GPU sub-channel ---- */
static int dncnn_run_gpu(float *d_input, float *d_output, int H, int W) {
    if (!g_layers || g_num_layers == 0) return -1;

    int max_ch = g_hidden_channels;
    size_t need = (size_t)max_ch * H * W * sizeof(float);
    if (need > (size_t)g_buf_size) {
        cudaFree(g_buf[0]); cudaFree(g_buf[1]);
        cudaMalloc(&g_buf[0], need);
        cudaMalloc(&g_buf[1], need);
        g_buf_size = (int)need;
    }

    dim3 blk(16, 16);

    /* Layer 0 */
    dim3 grd((W + 15) / 16, (H + 15) / 16, g_layers[0].outCh);
    conv3x3_relu_kernel<<<grd, blk>>>(d_input, g_layers[0].d_weight, g_layers[0].d_bias,
                                       g_buf[0], H, W, g_layers[0].inCh, g_layers[0].outCh, 1);

    /* Hidden layers */
    for (int i = 1; i < g_num_layers - 1; i++) {
        int src = (i - 1) % 2, dst = i % 2;
        grd = dim3((W + 15) / 16, (H + 15) / 16, g_layers[i].outCh);
        conv3x3_relu_kernel<<<grd, blk>>>(g_buf[src], g_layers[i].d_weight, g_layers[i].d_bias,
                                           g_buf[dst], H, W, g_layers[i].inCh, g_layers[i].outCh, 1);
    }

    /* Last layer — no ReLU */
    int last = g_num_layers - 1;
    int src = (last - 1) % 2;
    grd = dim3((W + 15) / 16, (H + 15) / 16, g_layers[last].outCh);
    conv3x3_relu_kernel<<<grd, blk>>>(g_buf[src], g_layers[last].d_weight, g_layers[last].d_bias,
                                       d_output, H, W, g_layers[last].inCh, g_layers[last].outCh, 0);
    return 0;
}

/* ---- High-level: denoise uint16 Bayer frame ---- */
extern "C" int dncnn_cuda_denoise_bayer(uint16_t *bayer, int width, int height,
                                          float blend, float noise_sigma) {
    (void)noise_sigma;

    /* ONNX CPU path — preferred (fast + frees GPU) */
    if (g_use_ort_cpu) {
        int scw = width / 2, sch = height / 2;
        size_t sc_pixels = (size_t)scw * sch;
        float *h_in = (float *)malloc(sc_pixels * sizeof(float));
        float *h_out = (float *)malloc(sc_pixels * sizeof(float));
        if (!h_in || !h_out) { free(h_in); free(h_out); return -1; }
        float scale = 1.0f / 65535.0f;

        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                for (int y = 0; y < sch; y++)
                    for (int x = 0; x < scw; x++)
                        h_in[y * scw + x] = (float)bayer[(y * 2 + dy) * width + (x * 2 + dx)] * scale;

                if (dncnn_ort_cpu_run(h_in, h_out, sch, scw) != 0) continue;

                for (int y = 0; y < sch; y++) {
                    for (int x = 0; x < scw; x++) {
                        int i = y * scw + x;
                        float d = h_in[i] - blend * h_out[i];
                        if (d < 0.0f) d = 0.0f;
                        if (d > 1.0f) d = 1.0f;
                        bayer[(y * 2 + dy) * width + (x * 2 + dx)] = (uint16_t)(d * 65535.0f + 0.5f);
                    }
                }
            }
        }
        free(h_in);
        free(h_out);
        return 0;
    }

    /* CUDA kernel fallback */
    if (!g_layers) return -1;
    cudaDeviceSynchronize();

    int scw = width / 2, sch = height / 2;
    size_t sc_pixels = (size_t)scw * sch;
    size_t bayer_bytes = (size_t)width * height * sizeof(uint16_t);

    /* Allocate persistent GPU buffers */
    if ((int)sc_pixels > g_sc_alloc) {
        cudaFree(g_d_sc); cudaFree(g_d_resid);
        cudaMalloc(&g_d_sc, sc_pixels * sizeof(float));
        cudaMalloc(&g_d_resid, sc_pixels * sizeof(float));
        g_sc_alloc = (int)sc_pixels;
    }
    if ((int)bayer_bytes > g_bayer_alloc) {
        cudaFree(g_d_bayer);
        cudaMalloc(&g_d_bayer, bayer_bytes);
        g_bayer_alloc = (int)bayer_bytes;
    }

    /* Single upload: CPU → GPU */
    cudaMemcpy(g_d_bayer, bayer, bayer_bytes, cudaMemcpyHostToDevice);

    dim3 blk(16, 16);
    dim3 grd((scw + 15) / 16, (sch + 15) / 16);

    /* Process 4 sub-channels entirely on GPU */
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            /* Extract sub-channel on GPU */
            extract_subchannel_k<<<grd, blk>>>(g_d_bayer, g_d_sc, width, dx, dy, scw, sch);

            /* Run DnCNN (all on GPU) */
            dncnn_run_gpu(g_d_sc, g_d_resid, sch, scw);

            /* Blend residual back on GPU */
            blend_residual_k<<<grd, blk>>>(g_d_bayer, g_d_sc, g_d_resid, blend, width, dx, dy, scw, sch);
        }
    }

    /* Single download: GPU → CPU */
    cudaMemcpy(bayer, g_d_bayer, bayer_bytes, cudaMemcpyDeviceToHost);

    cudaError_t e = cudaGetLastError();
    if (e != cudaSuccess) { fprintf(stderr, "DnCNN error: %s\n", cudaGetErrorString(e)); return -1; }
    return 0;
}
