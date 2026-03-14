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

/* ---- CPU pyramid block matching optical flow ----
 * Uses motion_est.c's 3-level pyramid + half-pixel refinement.
 * Works directly on uint16 green channel — no u8 conversion needed.
 * Block matching with SAD is inherently noise-robust (averages over block).
 * Much more accurate than NVOF on noisy high-ISO RAW footage. */

#define BM_BLOCK_SIZE 8  /* block size in green pixels */

static int g_of_init_w = 0, g_of_init_h = 0;

int platform_of_init(int width, int height) {
    g_of_init_w = width >> 1;  /* green_w */
    g_of_init_h = height >> 1; /* green_h */
    fprintf(stderr, "OF: CPU pyramid block matching %dx%d (block=%d)\n",
            g_of_init_w, g_of_init_h, BM_BLOCK_SIZE);
    return 0;
}

void platform_of_destroy(void) {
    g_of_init_w = g_of_init_h = 0;
}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    if (g_of_init_w == 0) {
        platform_of_init(green_w * 2, green_h * 2);
    }

    int grid_w = green_w / BM_BLOCK_SIZE;
    int grid_h = green_h / BM_BLOCK_SIZE;

    for (int n = 0; n < num_neighbors; n++) {
        /* Run pyramid block matching: center=reference, neighbor=current */
        MotionVector *mvs = motion_estimate(center, neighbors[n],
                                             green_w, green_h, BM_BLOCK_SIZE);
        if (!mvs) {
            /* Fallback: zero flow */
            size_t npix = (size_t)green_w * green_h;
            memset(fx_out[n], 0, npix * sizeof(float));
            memset(fy_out[n], 0, npix * sizeof(float));
            continue;
        }

        /* Bilinear interpolation of block MVs to per-pixel dense flow.
         * MV centers are at (gx*BS + BS/2, gy*BS + BS/2) in green coords.
         * MVs are in half-green-pixel units → divide by 2 for green pixels. */
        for (int py = 0; py < green_h; py++) {
            /* Map pixel to fractional grid position */
            float gfy = ((float)py - (float)BM_BLOCK_SIZE * 0.5f) / (float)BM_BLOCK_SIZE;
            int gy0 = (int)floorf(gfy);
            int gy1 = gy0 + 1;
            float fy = gfy - (float)gy0;
            if (gy0 < 0) { gy0 = 0; fy = 0.0f; }
            if (gy1 >= grid_h) { gy1 = grid_h - 1; }
            if (gy0 >= grid_h) { gy0 = grid_h - 1; }

            for (int px = 0; px < green_w; px++) {
                float gfx = ((float)px - (float)BM_BLOCK_SIZE * 0.5f) / (float)BM_BLOCK_SIZE;
                int gx0 = (int)floorf(gfx);
                int gx1 = gx0 + 1;
                float fx = gfx - (float)gx0;
                if (gx0 < 0) { gx0 = 0; fx = 0.0f; }
                if (gx1 >= grid_w) { gx1 = grid_w - 1; }
                if (gx0 >= grid_w) { gx0 = grid_w - 1; }

                /* Bilinear interpolation of 4 surrounding block MVs */
                int i00 = gy0 * grid_w + gx0;
                int i10 = gy0 * grid_w + gx1;
                int i01 = gy1 * grid_w + gx0;
                int i11 = gy1 * grid_w + gx1;

                /* MVs in half-green-pixel units → * 0.5 for green pixels */
                float vx = ((1-fx)*(1-fy)*(float)mvs[i00].dx
                          +    fx *(1-fy)*(float)mvs[i10].dx
                          + (1-fx)*   fy *(float)mvs[i01].dx
                          +    fx *   fy *(float)mvs[i11].dx) * 0.5f;
                float vy = ((1-fx)*(1-fy)*(float)mvs[i00].dy
                          +    fx *(1-fy)*(float)mvs[i10].dy
                          + (1-fx)*   fy *(float)mvs[i01].dy
                          +    fx *   fy *(float)mvs[i11].dy) * 0.5f;

                int gi = py * green_w + px;
                fx_out[n][gi] = vx;
                fy_out[n][gi] = vy;
            }
        }

        motion_vectors_free(mvs);
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
