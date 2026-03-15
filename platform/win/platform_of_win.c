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
#include "motion_est.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef BAYERFLOW_WINDOWS

/* ---- RAFT optical flow via ONNX Runtime C API ----
 * No Python subprocess � direct GPU inference via onnxruntime.dll.
 * Model loaded once, ~0.05s per pair inference. */

#include "raft_onnx.h"

static int g_of_init_w = 0, g_of_init_h = 0;
static int g_onnx_init = 0;

int platform_of_init(int width, int height) {
    g_of_init_w = width >> 1;
    g_of_init_h = height >> 1;

    if (!g_onnx_init) {
        const char *model = "C:\\Users\\kaden\\BayerFlow-Win\\raft_small.onnx";
        if (raft_onnx_init(model, g_of_init_w, g_of_init_h) != 0) {
            fprintf(stderr, "OF: RAFT ONNX init failed, falling back to zero flow\n");
            return -1;
        }
        g_onnx_init = 1;
    }

    fprintf(stderr, "OF: RAFT ONNX C API %dx%d\n", g_of_init_w, g_of_init_h);
    return 0;
}

void platform_of_destroy(void) {
    if (g_onnx_init) {
        raft_onnx_destroy();
        g_onnx_init = 0;
    }
    g_of_init_w = g_of_init_h = 0;
}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    if (!g_onnx_init) {
        if (platform_of_init(green_w * 2, green_h * 2) != 0) {
            /* Zero flow fallback */
            size_t npix = (size_t)green_w * green_h;
            for (int n = 0; n < num_neighbors; n++) {
                memset(fx_out[n], 0, npix * sizeof(float));
                memset(fy_out[n], 0, npix * sizeof(float));
            }
            return 0;
        }
    }

    for (int n = 0; n < num_neighbors; n++) {
        if (raft_onnx_compute(center, neighbors[n], green_w, green_h,
                               fx_out[n], fy_out[n]) != 0) {
            size_t npix = (size_t)green_w * green_h;
            memset(fx_out[n], 0, npix * sizeof(float));
            memset(fy_out[n], 0, npix * sizeof(float));
        }
    }

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
