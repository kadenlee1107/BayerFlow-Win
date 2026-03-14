#ifndef TEMPORAL_FILTER_H
#define TEMPORAL_FILTER_H

#include <stdint.h>

typedef struct {
    int   window_size;   /* number of frames in sliding window (default 5) */
    float strength;      /* denoising strength multiplier (default 1.0) */
    float noise_sigma;   /* estimated noise level, 0 = auto-estimate */
} TemporalFilterConfig;

/* Fine-tuning parameters (can be NULL for defaults).
 * Also defined in denoise_bridge.h for Swift bridging. */
#ifndef TEMPORAL_FILTER_TUNING_DEFINED
#define TEMPORAL_FILTER_TUNING_DEFINED
typedef struct {
    float chroma_boost;     /* R/B bilateral kernel multiplier (1.0 = same as luma) */
    float dist_sigma;       /* distance confidence sigma in green-pixel units */
    float flow_tightening;  /* per-pixel strictness = 1 + mag * flow_tightening */
} TemporalFilterTuning;
#endif

/* Initialize temporal filter config with defaults. */
void temporal_filter_init(TemporalFilterConfig *cfg);

/* Estimate noise sigma from a single Bayer frame using G1-G2 difference.
 * Returns noise standard deviation in 16-bit pixel units. */
float temporal_filter_estimate_noise(const uint16_t *bayer, int width, int height);

/* Denoise center frame using motion-compensated temporal averaging.
 *
 * output       : denoised Bayer frame (width x height, pre-allocated)
 * frames       : sliding window of Bayer frames
 * flows_x/y   : per-pixel optical flow in green-pixel units, from center to each
 *               neighbor frame (flows_x[center_idx] == NULL)
 * num_frames  : window size
 * center_idx  : index of the frame being denoised
 * width,height: frame dimensions in raw Bayer pixels
 * cfg         : filter parameters
 */
void temporal_filter_frame(
    uint16_t *output,
    const uint16_t **frames,
    const float **flows_x,
    const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    const TemporalFilterConfig *cfg,
    const TemporalFilterTuning *tuning  /* NULL = use defaults */
);

#endif
