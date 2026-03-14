/* platform_of_win.c — NVIDIA Optical Flow SDK implementation
 *
 * Prerequisites:
 *   Download NVOF SDK from https://developer.nvidia.com/optical-flow-sdk
 *   Extract to C:\nvof_sdk\ (or update NVOF_SDK_ROOT in CMakeLists.txt)
 *   Requires RTX 2000+ (Turing or newer) — RTX 5070 uses 5th gen NVOF.
 *
 * Expected performance on RTX 5070 at 2880x1520 (half-res green channel):
 *   ~2-5ms per frame pair → 4 neighbors = ~8-20ms total vs ANE 240ms
 */

#include "platform_of.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef BAYERFLOW_WINDOWS

#include <cuda_runtime.h>
#include <NvOFCuda.h>

/* ---- State ---- */
static NvOFHandle  g_hOF         = NULL;
static int         g_init_w      = 0;
static int         g_init_h      = 0;

static NvOFGPUBufferHandle g_hInput[2];
static NvOFGPUBufferHandle g_hOutput;
static cudaStream_t g_of_stream  = NULL;

static uint8_t *g_center_u8   = NULL;
static uint8_t *g_neighbor_u8 = NULL;

/* uint16 → uint8: shift right 8 (16-bit green → 8-bit grayscale for NVOF) */
static void u16_to_u8(const uint16_t *src, uint8_t *dst, int n) {
    for (int i = 0; i < n; i++)
        dst[i] = (uint8_t)(src[i] >> 8);
}

int platform_of_init(int width, int height) {
    int gw = width  >> 1;
    int gh = height >> 1;

    if (g_hOF && gw == g_init_w && gh == g_init_h)
        return 0;

    platform_of_destroy();

    NvOFStatus s;

    s = NvOFCreate(NV_OF_MODE_OPTICALFLOW, NvOFCuda_DoCreate, &g_hOF);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NvOFCreate failed: %d\n", s); return -1;
    }

    NvOFInitParams ip = {0};
    ip.width               = (uint32_t)gw;
    ip.height              = (uint32_t)gh;
    ip.outGridSize         = NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;  /* per-pixel */
    ip.hintGridSize        = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
    ip.mode                = NV_OF_MODE_OPTICALFLOW;
    ip.perfLevel           = NV_OF_PERF_LEVEL_SLOW;           /* best quality */
    ip.enableExternalHints = 0;
    ip.enableOutputCost    = 0;
    ip.enableRoi           = 0;

    s = NvOFCudaInit(g_hOF, &ip, 0 /* device 0 */, NULL);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NvOFCudaInit failed: %d\n", s);
        NvOFDestroy(g_hOF); g_hOF = NULL; return -1;
    }

    /* Input buffers: GRAYSCALE8 */
    NvOFGPUBufferDescEx bi = {0};
    bi.width = (uint32_t)gw; bi.height = (uint32_t)gh;
    bi.bufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
    bi.bufferUsage  = NV_OF_BUFFER_USAGE_INPUT;
    for (int i = 0; i < 2; i++) {
        s = NvOFCudaCreateGPUBuffers(g_hOF, &bi, 1, &g_hInput[i]);
        if (s != NV_OF_SUCCESS) {
            fprintf(stderr, "NvOFCudaCreateGPUBuffers(input[%d]) failed: %d\n", i, s);
            return -1;
        }
    }

    /* Output buffer: SHORT2 (int16 x,y pairs, quarter-pixel units) */
    NvOFGPUBufferDescEx bo = {0};
    bo.width = (uint32_t)gw; bo.height = (uint32_t)gh;
    bo.bufferFormat = NV_OF_BUFFER_FORMAT_SHORT2;
    bo.bufferUsage  = NV_OF_BUFFER_USAGE_OUTPUT;
    s = NvOFCudaCreateGPUBuffers(g_hOF, &bo, 1, &g_hOutput);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NvOFCudaCreateGPUBuffers(output) failed: %d\n", s);
        return -1;
    }

    cudaStreamCreate(&g_of_stream);

    size_t npix    = (size_t)gw * gh;
    g_center_u8   = (uint8_t *)malloc(npix);
    g_neighbor_u8 = (uint8_t *)malloc(npix);

    g_init_w = gw; g_init_h = gh;
    fprintf(stderr, "NVOF: initialized %dx%d, SLOW quality (best)\n", gw, gh);
    return 0;
}

void platform_of_destroy(void) {
    free(g_center_u8);   g_center_u8   = NULL;
    free(g_neighbor_u8); g_neighbor_u8 = NULL;
    if (g_of_stream) { cudaStreamDestroy(g_of_stream); g_of_stream = NULL; }
    if (g_hOF)       { NvOFDestroy(g_hOF); g_hOF = NULL; }
    g_init_w = g_init_h = 0;
}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    if (!g_hOF) {
        if (platform_of_init(green_w * 2, green_h * 2) != 0)
            return -1;
    }

    size_t npix = (size_t)green_w * green_h;

    /* Convert center to uint8 and upload (once, reused for all neighbors) */
    u16_to_u8(center, g_center_u8, (int)npix);

    CUdeviceptr cptr; uint32_t cpitch;
    NvOFCudaGetDataPointer(g_hInput[0], &cptr, &cpitch);
    cudaMemcpy2D((void *)cptr, cpitch, g_center_u8, green_w,
                 green_w, green_h, cudaMemcpyHostToDevice);

    /* Temp buffer for raw int16 flow output */
    short *flow_raw = (short *)malloc(npix * 2 * sizeof(short));
    if (!flow_raw) return -1;

    for (int n = 0; n < num_neighbors; n++) {
        /* Upload neighbor */
        u16_to_u8(neighbors[n], g_neighbor_u8, (int)npix);
        CUdeviceptr nptr; uint32_t npitch;
        NvOFCudaGetDataPointer(g_hInput[1], &nptr, &npitch);
        cudaMemcpy2D((void *)nptr, npitch, g_neighbor_u8, green_w,
                     green_w, green_h, cudaMemcpyHostToDevice);

        /* Execute NVOF: input[0]=center (reference), input[1]=neighbor (target) */
        NvOFExecuteInputParams_CUDA  ei  = {0};
        NvOFExecuteOutputParams_CUDA eo  = {0};
        ei.inputFrame     = g_hInput[0];
        ei.referenceFrame = g_hInput[1];
        eo.outputBuffer   = g_hOutput;

        NvOFStatus s = NvOFCudaExecute(g_hOF, &ei, &eo, NULL, g_of_stream);
        if (s != NV_OF_SUCCESS) {
            fprintf(stderr, "NvOFCudaExecute failed: %d\n", s);
            free(flow_raw); return -1;
        }
        cudaStreamSynchronize(g_of_stream);

        /* Readback: row-by-row to handle pitch correctly */
        CUdeviceptr optr; uint32_t opitch;
        NvOFCudaGetDataPointer(g_hOutput, &optr, &opitch);
        for (int row = 0; row < green_h; row++) {
            cudaMemcpy(flow_raw + row * green_w * 2,
                       (const void *)(optr + (size_t)row * opitch),
                       green_w * 2 * sizeof(short),
                       cudaMemcpyDeviceToHost);
        }

        /* Convert int16 quarter-pixel → float pixel */
        for (int i = 0; i < (int)npix; i++) {
            fx_out[n][i] = (float)flow_raw[i * 2 + 0] * 0.25f;
            fy_out[n][i] = (float)flow_raw[i * 2 + 1] * 0.25f;
        }
    }

    free(flow_raw);
    return 0;
}

#else  /* Non-Windows stub */

int  platform_of_init(int w, int h) { (void)w; (void)h; return 0; }
void platform_of_destroy(void) {}
int  platform_of_compute_batch(
    const uint16_t *c, const uint16_t **nbrs, int nn,
    int gw, int gh, float **fx, float **fy) {
    size_t np = (size_t)gw * gh;
    for (int i = 0; i < nn; i++) {
        (void)nbrs[i];
        memset(fx[i], 0, np * sizeof(float));
        memset(fy[i], 0, np * sizeof(float));
    }
    (void)c; return 0;
}

#endif
