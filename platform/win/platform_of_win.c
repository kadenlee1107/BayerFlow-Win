/* platform_of_win.c — NVIDIA Optical Flow SDK 5.x implementation
 *
 * SDK 5 changed to a function-pointer table API loaded from nvofapi64.dll
 * (shipped with the NVIDIA driver — no separate .lib file needed).
 *
 * Entry point: NvOFAPICreateInstanceCuda() populates NV_OF_CUDA_API_FUNCTION_LIST.
 * All subsequent calls go through that function table.
 *
 * Expected performance on RTX 5070 at 2880x1520 (half-res green channel):
 *   ~2-5ms per frame pair → 4 neighbors = ~8-20ms total
 */

#include "platform_of.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef BAYERFLOW_WINDOWS

#include <windows.h>
#include <cuda_runtime.h>
#include <cuda.h>
#include "nvOpticalFlowCuda.h"
#include "nvOpticalFlowCommon.h"

/* ---- Dynamic loader ---- */
typedef NV_OF_STATUS (NVOFAPI *PFN_NvOFAPICreateInstanceCuda)(
    uint32_t apiVer, NV_OF_CUDA_API_FUNCTION_LIST *fl);

static HMODULE                      g_dll      = NULL;
static NV_OF_CUDA_API_FUNCTION_LIST g_fn       = {0};
static int                          g_fn_valid = 0;

static int load_nvofapi(void) {
    if (g_fn_valid) return 0;

    g_dll = LoadLibraryA("nvofapi64.dll");
    if (!g_dll) {
        fprintf(stderr, "NVOF: nvofapi64.dll not found — driver too old or not installed\n");
        return -1;
    }

    PFN_NvOFAPICreateInstanceCuda createFn =
        (PFN_NvOFAPICreateInstanceCuda)GetProcAddress(g_dll, "NvOFAPICreateInstanceCuda");
    if (!createFn) {
        fprintf(stderr, "NVOF: NvOFAPICreateInstanceCuda not found in nvofapi64.dll\n");
        return -1;
    }

    NV_OF_STATUS s = createFn(NV_OF_API_VERSION, &g_fn);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NVOF: NvOFAPICreateInstanceCuda failed: %d\n", s);
        return -1;
    }

    g_fn_valid = 1;
    return 0;
}

/* ---- State ---- */
static NvOFHandle          g_hOF       = NULL;
static NvOFGPUBufferHandle g_hInput[2] = {NULL, NULL};
static NvOFGPUBufferHandle g_hOutput   = NULL;
static CUstream            g_of_stream = NULL;
static int                 g_init_w    = 0;
static int                 g_init_h    = 0;

static uint8_t *g_center_u8   = NULL;
static uint8_t *g_neighbor_u8 = NULL;

/* uint16 -> uint8: shift right 8 (16-bit green -> 8-bit grayscale for NVOF) */
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

    if (load_nvofapi() != 0) return -1;

    /* Ensure a CUDA context exists (runtime creates one via cudaFree(0)) */
    cudaFree(0);
    CUcontext ctx = NULL;
    cuCtxGetCurrent(&ctx);

    NV_OF_STATUS s = g_fn.nvCreateOpticalFlowCuda(ctx, &g_hOF);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NVOF: nvCreateOpticalFlowCuda failed: %d\n", s);
        return -1;
    }

    NV_OF_INIT_PARAMS ip = {0};
    ip.width               = (uint32_t)gw;
    ip.height              = (uint32_t)gh;
    ip.outGridSize         = NV_OF_OUTPUT_VECTOR_GRID_SIZE_1;  /* per-pixel */
    ip.hintGridSize        = NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED;
    ip.mode                = NV_OF_MODE_OPTICALFLOW;
    ip.perfLevel           = NV_OF_PERF_LEVEL_SLOW;           /* best quality */
    ip.enableExternalHints = NV_OF_FALSE;
    ip.enableOutputCost    = NV_OF_FALSE;
    ip.enableRoi           = NV_OF_FALSE;

    s = g_fn.nvOFInit(g_hOF, &ip);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NVOF: nvOFInit failed: %d\n", s);
        g_fn.nvOFDestroy(g_hOF); g_hOF = NULL;
        return -1;
    }

    /* Input buffers: GRAYSCALE8, CUdeviceptr */
    NV_OF_BUFFER_DESCRIPTOR bi = {0};
    bi.width        = (uint32_t)gw;
    bi.height       = (uint32_t)gh;
    bi.bufferFormat = NV_OF_BUFFER_FORMAT_GRAYSCALE8;
    bi.bufferUsage  = NV_OF_BUFFER_USAGE_INPUT;
    for (int i = 0; i < 2; i++) {
        s = g_fn.nvOFCreateGPUBufferCuda(g_hOF, &bi,
                NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &g_hInput[i]);
        if (s != NV_OF_SUCCESS) {
            fprintf(stderr, "NVOF: nvOFCreateGPUBufferCuda(input[%d]) failed: %d\n", i, s);
            return -1;
        }
    }

    /* Output buffer: SHORT2 (int16 x,y pairs, quarter-pixel units) */
    NV_OF_BUFFER_DESCRIPTOR bo = {0};
    bo.width        = (uint32_t)gw;
    bo.height       = (uint32_t)gh;
    bo.bufferFormat = NV_OF_BUFFER_FORMAT_SHORT2;
    bo.bufferUsage  = NV_OF_BUFFER_USAGE_OUTPUT;
    s = g_fn.nvOFCreateGPUBufferCuda(g_hOF, &bo,
            NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &g_hOutput);
    if (s != NV_OF_SUCCESS) {
        fprintf(stderr, "NVOF: nvOFCreateGPUBufferCuda(output) failed: %d\n", s);
        return -1;
    }

    cuStreamCreate(&g_of_stream, CU_STREAM_DEFAULT);
    g_fn.nvOFSetIOCudaStreams(g_hOF, g_of_stream, g_of_stream);

    size_t npix    = (size_t)gw * gh;
    g_center_u8   = (uint8_t *)malloc(npix);
    g_neighbor_u8 = (uint8_t *)malloc(npix);

    g_init_w = gw; g_init_h = gh;
    fprintf(stderr, "NVOF: SDK 5 initialized %dx%d, SLOW quality (best)\n", gw, gh);
    return 0;
}

void platform_of_destroy(void) {
    free(g_center_u8);   g_center_u8   = NULL;
    free(g_neighbor_u8); g_neighbor_u8 = NULL;
    if (g_of_stream) { cuStreamDestroy(g_of_stream); g_of_stream = NULL; }
    if (g_fn_valid) {
        if (g_hInput[0]) { g_fn.nvOFDestroyGPUBufferCuda(g_hInput[0]); g_hInput[0] = NULL; }
        if (g_hInput[1]) { g_fn.nvOFDestroyGPUBufferCuda(g_hInput[1]); g_hInput[1] = NULL; }
        if (g_hOutput)   { g_fn.nvOFDestroyGPUBufferCuda(g_hOutput);   g_hOutput   = NULL; }
        if (g_hOF)       { g_fn.nvOFDestroy(g_hOF); g_hOF = NULL; }
    }
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

    /* Convert center to uint8 and upload once (reused for all neighbors) */
    u16_to_u8(center, g_center_u8, (int)npix);

    CUdeviceptr cptr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hInput[0]);
    NV_OF_CUDA_BUFFER_STRIDE_INFO csi = {0};
    g_fn.nvOFGPUBufferGetStrideInfo(g_hInput[0], &csi);
    uint32_t cpitch = csi.strideInfo[0].strideXInBytes;

    cudaMemcpy2D((void *)cptr, cpitch, g_center_u8, green_w,
                 green_w, green_h, cudaMemcpyHostToDevice);

    /* Output buffer ptr + pitch */
    CUdeviceptr optr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hOutput);
    NV_OF_CUDA_BUFFER_STRIDE_INFO osi = {0};
    g_fn.nvOFGPUBufferGetStrideInfo(g_hOutput, &osi);
    uint32_t opitch = osi.strideInfo[0].strideXInBytes;

    short *flow_raw = (short *)malloc(npix * 2 * sizeof(short));
    if (!flow_raw) return -1;

    for (int n = 0; n < num_neighbors; n++) {
        /* Upload neighbor */
        u16_to_u8(neighbors[n], g_neighbor_u8, (int)npix);

        CUdeviceptr nptr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hInput[1]);
        NV_OF_CUDA_BUFFER_STRIDE_INFO nsi = {0};
        g_fn.nvOFGPUBufferGetStrideInfo(g_hInput[1], &nsi);
        uint32_t npitch = nsi.strideInfo[0].strideXInBytes;

        cudaMemcpy2D((void *)nptr, npitch, g_neighbor_u8, green_w,
                     green_w, green_h, cudaMemcpyHostToDevice);

        /* Execute NVOF: input[0]=center (reference), input[1]=neighbor (target) */
        NV_OF_EXECUTE_INPUT_PARAMS  ei = {0};
        NV_OF_EXECUTE_OUTPUT_PARAMS eo = {0};
        ei.inputFrame     = g_hInput[0];
        ei.referenceFrame = g_hInput[1];
        eo.outputBuffer   = g_hOutput;

        NV_OF_STATUS s = g_fn.nvOFExecute(g_hOF, &ei, &eo);
        if (s != NV_OF_SUCCESS) {
            fprintf(stderr, "NVOF: nvOFExecute failed: %d\n", s);
            free(flow_raw); return -1;
        }
        cuStreamSynchronize(g_of_stream);

        /* Readback: row-by-row to handle pitch correctly */
        for (int row = 0; row < green_h; row++) {
            cudaMemcpy(flow_raw + row * green_w * 2,
                       (const void *)(optr + (size_t)row * opitch),
                       green_w * 2 * sizeof(short),
                       cudaMemcpyDeviceToHost);
        }

        /* Convert int16 quarter-pixel -> float pixel */
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
