#ifndef DENOISE_H
#define DENOISE_H

#include <stdint.h>

/*
 * Denoise module for ProRes RAW encoder.
 *
 * Two-stage approach:
 *   1. Estimate per-tile noise from G1/G2 Bayer channel difference
 *   2. Apply soft thresholding to DCT coefficients (AC only)
 *
 * Inserted between forward_dct_8x8() and quantize_block() in the pipeline.
 * Controlled by g_denoise_config — disabled by default so existing
 * behavior is unchanged unless explicitly enabled.
 */

typedef struct {
    int    enabled;        /* 0 = bypass (default), 1 = active          */
    float  threshold_mul;  /* multiplier for noise-based threshold      */
    float  noise_sigma;    /* per-tile noise std dev (updated each tile) */
} DenoiseConfig;

/* Global config — lives in denoise.c */
extern DenoiseConfig g_denoise_config;

/* Set default values (enabled=0, threshold_mul=3.0) */
void denoise_init(DenoiseConfig *cfg);

/*
 * Estimate sensor noise for a tile region using the G1-G2 difference.
 * Returns noise standard deviation in raw pixel units (0–65535 scale).
 *
 * bayer_data : full-frame Bayer buffer
 * width      : frame width in pixels
 * tile_x/y   : top-left corner of the tile
 * tile_w/h   : tile dimensions (clamped to frame edge)
 */
float denoise_estimate_tile_noise(const uint16_t *bayer_data, int width,
                                  int tile_x, int tile_y,
                                  int tile_w, int tile_h);

/*
 * Soft-threshold DCT coefficients in-place (AC only, DC untouched).
 * Call between forward_dct_8x8() and quantize_block().
 *
 * block         : 64-element int32_t DCT coefficient array
 * noise_sigma   : noise std dev from denoise_estimate_tile_noise()
 * threshold_mul : scaling factor for the threshold
 */
void denoise_dct_block(int32_t *block, float noise_sigma, float threshold_mul);

#endif
