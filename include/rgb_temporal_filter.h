/*
 * RGB Temporal Filter
 *
 * Adapts the VST + bilateral temporal denoiser for 3-channel RGB input.
 * Used for formats that only provide debayered RGB (e.g., RED R3D via SDK).
 *
 * The algorithm is identical to the Bayer VST+bilateral filter:
 *   1. Anscombe VST per channel (stabilize noise variance)
 *   2. Motion-compensated bilateral temporal averaging
 *   3. Inverse Anscombe transform
 *
 * Optical flow is computed from luma (0.2126R + 0.7152G + 0.0722B).
 * Each RGB channel is denoised independently using the shared flow field.
 */

#ifndef RGB_TEMPORAL_FILTER_H
#define RGB_TEMPORAL_FILTER_H

#include <stdint.h>

typedef struct {
    int   window_size;   /* number of frames in sliding window */
    float strength;      /* denoising strength multiplier */
    float noise_sigma;   /* estimated noise level (16-bit units), 0 = auto */
} RgbTemporalFilterConfig;

/* Initialize config with defaults. */
void rgb_temporal_filter_init(RgbTemporalFilterConfig *cfg);

/* Estimate noise sigma from a single RGB frame.
 * Uses variance of Laplacian on the green channel.
 * rgb_planar: [R plane: w*h] [G plane: w*h] [B plane: w*h] */
float rgb_temporal_filter_estimate_noise(const uint16_t *rgb_planar,
                                          int width, int height);

/* Compute luma from planar RGB for optical flow.
 * luma_out: w*h float array.
 * rgb_planar: [R: w*h] [G: w*h] [B: w*h] */
void rgb_compute_luma(const uint16_t *rgb_planar, int width, int height,
                      float *luma_out);

/* Denoise center RGB frame using motion-compensated temporal averaging.
 *
 * output      : denoised planar RGB (3 * width * height uint16_t, pre-allocated)
 * frames      : sliding window of planar RGB frames (each 3 * w * h)
 * flows_x/y   : per-pixel optical flow from center to each neighbor
 *               (in pixel units, not sub-sampled; flows[center_idx] == NULL)
 * num_frames  : window size
 * center_idx  : index of the frame being denoised
 * width,height: frame dimensions
 * cfg         : filter parameters
 */
void rgb_temporal_filter_frame(
    uint16_t       *output,
    const uint16_t **frames,
    const float    **flows_x,
    const float    **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    const RgbTemporalFilterConfig *cfg
);

#endif /* RGB_TEMPORAL_FILTER_H */
