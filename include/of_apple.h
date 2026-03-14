#ifndef OF_APPLE_H
#define OF_APPLE_H

#include <stdint.h>

/* Compute dense optical flow using Apple's Vision framework (ML-based, Neural Engine).
 *
 * frame1, frame2 : 16-bit grayscale (green channel) images, green_w x green_h.
 * Computes flow FROM frame1 TO frame2:
 *   flow_x[y*w+x] = x displacement of pixel (x,y) in frame1, in green-pixel units
 *   flow_y[y*w+x] = y displacement, positive = downward (matches raw image coords)
 *
 * Caller must pre-allocate flow_x and flow_y (green_w * green_h floats each).
 * On failure, outputs are zeroed and -1 is returned.
 * Requires macOS 11.0+.
 */
int compute_apple_flow(const uint16_t *frame1, const uint16_t *frame2,
                       int green_w, int green_h,
                       float *flow_x, float *flow_y);

/* Batch OF: compute flow from center to multiple neighbors in one call.
 * Center frame is downsampled/converted once and reused for all pairs.
 * fx_out[i] / fy_out[i] must be pre-allocated (green_w * green_h floats). */
int compute_apple_flow_batch(const uint16_t *center,
                             const uint16_t *const *neighbors, int num_neighbors,
                             int green_w, int green_h,
                             float **fx_out, float **fy_out);

#endif
