/* platform_of_mac_stub.c — Mac CLI stub for CMake builds.
 * The real Mac implementation is of_apple.m (Vision framework, Xcode only).
 * This stub allows the CMake CLI build to link on Mac for testing core logic. */

#include "platform_of.h"
#include <string.h>

int platform_of_init(int width, int height) { (void)width; (void)height; return 0; }
void platform_of_destroy(void) {}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors, int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    size_t npix = (size_t)green_w * green_h;
    for (int i = 0; i < num_neighbors; i++) {
        (void)neighbors[i];
        memset(fx_out[i], 0, npix * sizeof(float));
        memset(fy_out[i], 0, npix * sizeof(float));
    }
    (void)center;
    return 0;
}
