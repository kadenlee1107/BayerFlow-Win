/* dncnn_cuda.cu — DnCNN spatial denoiser via CUDA.
 * 7-layer 3x3 conv + ReLU, single-channel (Bayer sub-channel).
 * All data stays on GPU — 1 upload + 1 download per frame. */

#include <cuda_runtime.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

/* ---- Load weights from .bin file ---- */
extern "C" int dncnn_cuda_init(const char *weight_path) {
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

/* ---- High-level: denoise uint16 Bayer frame, all on GPU ---- */
extern "C" int dncnn_cuda_denoise_bayer(uint16_t *bayer, int width, int height,
                                          float blend, float noise_sigma) {
    if (!g_layers) return -1;
    (void)noise_sigma;

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
