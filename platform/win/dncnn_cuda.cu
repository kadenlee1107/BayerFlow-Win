/* dncnn_cuda.cu — DnCNN spatial denoiser via CUDA.
 * Loads weights from postfilter_1ch_weights.bin (same format as Mac MPS).
 * 7-layer 3x3 conv + ReLU, single-channel (Bayer sub-channel).
 * Runs on RTX 5070 tensor cores for ~5-10ms per sub-channel. */

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
static float *g_buf[2] = {NULL, NULL};  /* ping-pong GPU buffers */
static int g_buf_size = 0;

/* ---- 3x3 Convolution + Bias + optional ReLU kernel ---- */
__global__ void conv3x3_relu_kernel(
    const float * __restrict__ input,
    const float * __restrict__ weight,  /* [outCh, inCh, 3, 3] */
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

                float pixel = input[ic * H * W + iy * W + ix];
                float w = weight[oc * inCh * 9 + ic * 9 + (ky + 1) * 3 + (kx + 1)];
                sum += pixel * w;
            }
        }
    }

    if (apply_relu && sum < 0.0f) sum = 0.0f;
    output[oc * H * W + y * W + x] = sum;
}

/* ---- Load weights from .bin file ---- */
extern "C" int dncnn_cuda_init(const char *weight_path) {
    FILE *f = fopen(weight_path, "rb");
    if (!f) { fprintf(stderr, "DnCNN: cannot open %s\n", weight_path); return -1; }

    /* Header: "DCNN" + version + in_ch + hidden_ch + num_layers */
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
        if (i == 0) g_layers[i].inCh = (int)in_ch;
        else g_layers[i].inCh = (int)hidden_ch;

        /* Upload to GPU */
        cudaMalloc(&g_layers[i].d_weight, w_count * sizeof(float));
        cudaMemcpy(g_layers[i].d_weight, w_cpu, w_count * sizeof(float), cudaMemcpyHostToDevice);
        cudaMalloc(&g_layers[i].d_bias, b_count * sizeof(float));
        cudaMemcpy(g_layers[i].d_bias, b_cpu, b_count * sizeof(float), cudaMemcpyHostToDevice);

        free(w_cpu);
        free(b_cpu);
    }

    fclose(f);
    fprintf(stderr, "DnCNN CUDA: loaded %d layers (%d→%dch) from %s\n",
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
    g_num_layers = 0;
}

/* ---- Run DnCNN on a single-channel float image ---- */
extern "C" int dncnn_cuda_denoise(float *d_input, float *d_output, int H, int W) {
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
    dim3 grd;

    grd = dim3((W + 15) / 16, (H + 15) / 16, g_layers[0].outCh);
    conv3x3_relu_kernel<<<grd, blk>>>(d_input, g_layers[0].d_weight, g_layers[0].d_bias,
                                       g_buf[0], H, W, g_layers[0].inCh, g_layers[0].outCh, 1);

    for (int i = 1; i < g_num_layers - 1; i++) {
        int src = (i - 1) % 2;
        int dst = i % 2;
        grd = dim3((W + 15) / 16, (H + 15) / 16, g_layers[i].outCh);
        conv3x3_relu_kernel<<<grd, blk>>>(g_buf[src], g_layers[i].d_weight, g_layers[i].d_bias,
                                           g_buf[dst], H, W, g_layers[i].inCh, g_layers[i].outCh, 1);
    }

    int last = g_num_layers - 1;
    int src = (last - 1) % 2;
    grd = dim3((W + 15) / 16, (H + 15) / 16, g_layers[last].outCh);
    conv3x3_relu_kernel<<<grd, blk>>>(g_buf[src], g_layers[last].d_weight, g_layers[last].d_bias,
                                       d_output, H, W, g_layers[last].inCh, g_layers[last].outCh, 0);
    cudaDeviceSynchronize();
    {
        cudaError_t e = cudaGetLastError();
        if (e != cudaSuccess) { fprintf(stderr, "DnCNN kernel error: %s\n", cudaGetErrorString(e)); return -1; }
    }
    return 0;
}

/* ---- High-level: denoise a uint16 Bayer frame (all 4 sub-channels) ---- */
extern "C" int dncnn_cuda_denoise_bayer(uint16_t *bayer, int width, int height,
                                          float blend, float noise_sigma) {
    if (!g_layers) return -1;

    /* Serialize CUDA access � wait for temporal filter kernels to finish
     * before DnCNN launches its own kernels (avoids thread deadlock) */
    cudaDeviceSynchronize();

    int scw = width / 2, sch = height / 2;
    size_t sc_pixels = (size_t)scw * sch;
    float scale = 1.0f / 65535.0f;

    /* Allocate GPU buffers for one sub-channel */
    float *d_in, *d_out;
    cudaMalloc(&d_in, sc_pixels * sizeof(float));
    cudaMalloc(&d_out, sc_pixels * sizeof(float));

    /* Process each Bayer sub-channel: (0,0)=R, (0,1)=Gr, (1,0)=Gb, (1,1)=B */
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            /* Extract sub-channel to float [0,1] on CPU, upload */
            float *h_sc = (float *)malloc(sc_pixels * sizeof(float));
            for (int y = 0; y < sch; y++)
                for (int x = 0; x < scw; x++)
                    h_sc[y * scw + x] = (float)bayer[(y * 2 + dy) * width + (x * 2 + dx)] * scale;

            cudaMemcpy(d_in, h_sc, sc_pixels * sizeof(float), cudaMemcpyHostToDevice);

            /* Run DnCNN — outputs noise residual */
            dncnn_cuda_denoise(d_in, d_out, sch, scw);

            /* Download and blend: output = input - blend * residual */
            float *h_out = (float *)malloc(sc_pixels * sizeof(float));
            cudaMemcpy(h_out, d_out, sc_pixels * sizeof(float), cudaMemcpyDeviceToHost);

            for (int y = 0; y < sch; y++) {
                for (int x = 0; x < scw; x++) {
                    int i = y * scw + x;
                    /* Residual subtraction: output = input - blend * noise_estimate */
                    float noise_est = h_out[i];
                    float denoised = h_sc[i] - blend * noise_est;
                    if (denoised < 0.0f) denoised = 0.0f;
                    if (denoised > 1.0f) denoised = 1.0f;
                    bayer[(y * 2 + dy) * width + (x * 2 + dx)] = (uint16_t)(denoised * 65535.0f + 0.5f);
                }
            }

            free(h_sc);
            free(h_out);
        }
    }

    cudaFree(d_in);
    cudaFree(d_out);
    return 0;
}
