/* platform_of_win.c — Windows optical flow implementation
 * TODO: Replace stub with NVIDIA Optical Flow SDK (NVOF).
 *
 * NVOF setup:
 *   1. Download NVIDIA Optical Flow SDK from developer.nvidia.com
 *   2. Link against nvOpticalFlow.lib
 *   3. NvOFAPI: NvOFCreate → NvOFInit → NvOFExecute → NvOFDestroy
 *   4. Input: NV12 or grayscale uint8 — convert from uint16 green channel
 *   5. Output: flow in quarter-pixel units → divide by 4 for pixel units
 *
 * RTX 5070 expected: ~2-5ms per frame pair at 2880x1520 (half-res green) */

#include "platform_of.h"
#include <string.h>

int platform_of_init(int width, int height) {
    (void)width; (void)height;
    /* TODO: NvOFCreate + NvOFInit */
    return 0;
}

void platform_of_destroy(void) {
    /* TODO: NvOFDestroy */
}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    /* TODO: Call NVOF for each neighbor.
     * For now: zero flow (no motion assumed — CPU fallback quality). */
    size_t npix = (size_t)green_w * green_h;
    for (int i = 0; i < num_neighbors; i++) {
        (void)neighbors[i];
        memset(fx_out[i], 0, npix * sizeof(float));
        memset(fy_out[i], 0, npix * sizeof(float));
    }
    (void)center;
    return 0;
}
