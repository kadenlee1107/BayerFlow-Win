/* platform_of.h — Optical flow platform abstraction
 * Mac:     Vision framework (ANE)
 * Windows: NVIDIA Optical Flow SDK (NVOF) */

#pragma once
#include <stdint.h>

/* Compute optical flow from center to each neighbor.
 * center/neighbors: half-resolution green channel (uint16), green_w x green_h.
 * fx_out/fy_out:    output flow fields in pixels at green resolution.
 * Returns 0 on success, nonzero on error. */
int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out);

/* Optional: init/teardown (e.g. NVOF session lifecycle).
 * platform_of_init() called once at startup, platform_of_destroy() at shutdown.
 * Both are no-ops on Mac (Vision creates sessions per-request). */
int  platform_of_init(int width, int height);
void platform_of_destroy(void);
