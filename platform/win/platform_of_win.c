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

/* uint16 -> uint8: simple >>6 shift, no black level subtraction.
 * Maps 0-16383 uniformly to 0-255. Both frames get identical mapping
 * so NVOF sees no artificial brightness shifts. Dark pixels stay at
 * their natural low values instead of being clamped to 0. */
static void u16_to_u8(const uint16_t *src, uint8_t *dst, int n) {
    for (int i = 0; i < n; i++) {
        int v = src[i] >> 6;
        dst[i] = (uint8_t)(v > 255 ? 255 : v);
    }
}

/* Downsample green channel 2x with box filter (averages 2x2 blocks).
 * Reduces noise by ~2x, making NVOF much more reliable. */
static void downsample_green_2x(const uint16_t *src, int sw, int sh,
                                  uint16_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int sy = y * 2, sx = x * 2;
            uint32_t sum = (uint32_t)src[sy * sw + sx]
                         + (uint32_t)src[sy * sw + sx + 1]
                         + (uint32_t)src[(sy + 1) * sw + sx]
                         + (uint32_t)src[(sy + 1) * sw + sx + 1];
            dst[y * dw + x] = (uint16_t)(sum >> 2);
        }
    }
}

/* Downsample green channel 4x with box filter (averages 4x4 blocks). */
static void downsample_green_4x(const uint16_t *src, int sw, int sh,
                                  uint16_t *dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            uint32_t sum = 0;
            for (int ky = 0; ky < 4; ky++)
                for (int kx = 0; kx < 4; kx++) {
                    int sy = y * 4 + ky, sx = x * 4 + kx;
                    if (sy < sh && sx < sw)
                        sum += src[sy * sw + sx];
                }
            dst[y * dw + x] = (uint16_t)(sum >> 4);
        }
    }
}

/* Downsampled buffers for NVOF */
static uint16_t *g_ds_center = NULL;
static uint16_t *g_ds_neighbor = NULL;
static int g_ds_w = 0, g_ds_h = 0;

int platform_of_init(int width, int height) {
    /* NVOF runs at 1/4 green resolution (downsample 4x for noise reduction) */
    int gw = width  >> 3;  /* green_w / 4 */
    int gh = height >> 3;  /* green_h / 4 */

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
    ip.outGridSize         = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;  /* 4x4 blocks — robust to noise */
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

    /* Allocate downsample buffers */
    free(g_ds_center); free(g_ds_neighbor);
    g_ds_w = gw; g_ds_h = gh;
    g_ds_center   = (uint16_t *)malloc((size_t)gw * gh * sizeof(uint16_t));
    g_ds_neighbor = (uint16_t *)malloc((size_t)gw * gh * sizeof(uint16_t));

    g_init_w = gw; g_init_h = gh;
    fprintf(stderr, "NVOF: SDK 5 initialized %dx%d, SLOW quality\n", gw, gh);

    return 0;
}

void platform_of_destroy(void) {
    free(g_center_u8);   g_center_u8   = NULL;
    free(g_neighbor_u8); g_neighbor_u8 = NULL;
    free(g_ds_center);   g_ds_center   = NULL;
    free(g_ds_neighbor); g_ds_neighbor = NULL;
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

    /* Downsample center green 4x for noise reduction, then convert to u8 */
    int ds_w = green_w >> 2;
    int ds_h = green_h >> 2;
    size_t ds_npix = (size_t)ds_w * ds_h;
    downsample_green_4x(center, green_w, green_h, g_ds_center, ds_w, ds_h);
    u16_to_u8(g_ds_center, g_center_u8, (int)ds_npix);

    /* Debug: print u8 range */
    {
        uint8_t mn=255, mx=0;
        for (int i=0; i<(int)npix && i<100000; i++) {
            if (g_center_u8[i]<mn) mn=g_center_u8[i];
            if (g_center_u8[i]>mx) mx=g_center_u8[i];
        }
        static int dbg_count=0;
        if (dbg_count<3) {
            fprintf(stderr, "NVOF debug: u16 center[0]=%u center[1000]=%u, u8 range=[%u,%u], shift=%s\n",
                    center[0], center[1000], mn, mx,
                    (center[1000]>8192)?">>8":">>4");
            dbg_count++;
        }
    }
    CUdeviceptr cptr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hInput[0]);
    NV_OF_CUDA_BUFFER_STRIDE_INFO csi = {0};
    g_fn.nvOFGPUBufferGetStrideInfo(g_hInput[0], &csi);
    uint32_t cpitch = csi.strideInfo[0].strideXInBytes;

    cudaMemcpy2D((void *)cptr, cpitch, g_center_u8, ds_w,
                 ds_w, ds_h, cudaMemcpyHostToDevice);

    /* Output buffer ptr + pitch */
    CUdeviceptr optr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hOutput);
    NV_OF_CUDA_BUFFER_STRIDE_INFO osi = {0};
    g_fn.nvOFGPUBufferGetStrideInfo(g_hOutput, &osi);
    uint32_t opitch = osi.strideInfo[0].strideXInBytes;

    int out_w = (ds_w + 3) / 4;  /* ceil(ds_w / 4) for grid size 4 */
    int out_h = (ds_h + 3) / 4;
    size_t out_npix = (size_t)out_w * out_h;
    short *flow_raw = (short *)malloc(out_npix * 2 * sizeof(short));
    if (!flow_raw) return -1;

    for (int n = 0; n < num_neighbors; n++) {
        /* Upload neighbor */
        downsample_green_4x(neighbors[n], green_w, green_h, g_ds_neighbor, ds_w, ds_h);
        u16_to_u8(g_ds_neighbor, g_neighbor_u8, (int)ds_npix);

        CUdeviceptr nptr = g_fn.nvOFGPUBufferGetCUdeviceptr(g_hInput[1]);
        NV_OF_CUDA_BUFFER_STRIDE_INFO nsi = {0};
        g_fn.nvOFGPUBufferGetStrideInfo(g_hInput[1], &nsi);
        uint32_t npitch = nsi.strideInfo[0].strideXInBytes;

        cudaMemcpy2D((void *)nptr, npitch, g_neighbor_u8, ds_w,
                     ds_w, ds_h, cudaMemcpyHostToDevice);

        /* Debug: print first pixels of both frames */
        {
            static int pair_dbg = 0;
            if (pair_dbg < 2) {
                fprintf(stderr, "NVOF pair %d: center_u8=[%u,%u,%u,%u,%u] neighbor_u8=[%u,%u,%u,%u,%u]\n",
                        pair_dbg,
                        g_center_u8[0], g_center_u8[1], g_center_u8[2], g_center_u8[3], g_center_u8[4],
                        g_neighbor_u8[0], g_neighbor_u8[1], g_neighbor_u8[2], g_neighbor_u8[3], g_neighbor_u8[4]);
                /* Check if they're similar (adjacent frames should be) */
                int diff_sum = 0;
                for (int di = 0; di < 10000; di++)
                    diff_sum += abs((int)g_center_u8[di] - (int)g_neighbor_u8[di]);
                fprintf(stderr, "NVOF pair %d: mean_abs_diff=%d/10000=%d.%02d\n",
                        pair_dbg, diff_sum, diff_sum/10000, (diff_sum%10000)*100/10000);
                pair_dbg++;
            }
        }

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

        /* Readback: use out_w/out_h (grid size 4 output) */
        cudaMemcpy2D(flow_raw,
                     (size_t)out_w * 2 * sizeof(short),
                     (const void *)optr,
                     (size_t)opitch,
                     (size_t)out_w * 2 * sizeof(short),
                     (size_t)out_h,
                     cudaMemcpyDeviceToHost);

        /* Debug: print raw flow values */
        {
            static int fdbg=0;
            if (fdbg<2) {
                int mid = (int)out_npix/2;
                fprintf(stderr, "NVOF debug: raw flow[0]=(%d,%d) flow[mid]=(%d,%d) flow[100]=(%d,%d) opitch=%u out=%dx%d\n",
                        flow_raw[0], flow_raw[1],
                        flow_raw[mid*2], flow_raw[mid*2+1],
                        flow_raw[200], flow_raw[201], opitch, out_w, out_h);
                fdbg++;
            }
        }
        /* Convert int16 quarter-pixel -> float pixel, upscale to green resolution.
         * NVOF output is at out_w x out_h (grid=4, ds=4x -> 16x total).
         * Each output pixel covers 16x16 green pixels.
         * Flow in quarter ds-pixels * 0.25 = ds-pixels, * 4 = green-pixels. */
        for (int gy = 0; gy < green_h; gy++) {
            int oy = (gy >> 4);  /* /16: 4x ds + 4x grid */
            if (oy >= out_h) oy = out_h - 1;
            for (int gx = 0; gx < green_w; gx++) {
                int ox = (gx >> 4);
                if (ox >= out_w) ox = out_w - 1;
                int oi = oy * out_w + ox;
                int gi = gy * green_w + gx;
                fx_out[n][gi] = (float)flow_raw[oi * 2 + 0] * 1.0f;
                fy_out[n][gi] = (float)flow_raw[oi * 2 + 1] * 1.0f;
            }
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
