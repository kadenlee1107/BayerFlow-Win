#ifndef NOISE_PROFILE_H
#define NOISE_PROFILE_H

#include <stdint.h>

/* Noise model: sigma(v)^2 = read_noise^2 + shot_gain * max(0, v - black_level)
 * All values in 16-bit pixel domain (0..65535). */
typedef struct {
    float sigma;        /* measured noise std-dev at patch brightness */
    float read_noise;   /* floor noise (electronics) — sigma at black */
    float shot_gain;    /* photon shot noise slope (electrons per DN) */
    float black_level;  /* estimated sensor black level */
    float mean_signal;  /* mean pixel value in the patch */
    int   valid;        /* 1 if fit succeeded, 0 if patch too small/flat */
} CNoiseProfile;

/*
 * Estimate noise model from a flat, textureless patch in a Bayer frame.
 *
 * Uses Gr-Gb green channel differences within the patch to measure noise
 * without signal contamination. Then subdivides into brightness sub-zones
 * to fit the shot-noise slope.
 *
 * bayer       : full 16-bit Bayer frame (RGGB layout)
 * bayer_w/h   : full frame dimensions
 * px, py      : top-left corner of the analysis patch (in Bayer pixels)
 * pw, ph      : patch size (in Bayer pixels, must be >= 32x32 for reliable fit)
 * out         : filled on return
 */
void noise_profile_from_patch(const uint16_t *bayer,
                               int bayer_w, int bayer_h,
                               int px, int py, int pw, int ph,
                               CNoiseProfile *out);

/*
 * Read a single raw Bayer frame from a video file (no denoising).
 * Returns a malloc'd uint16_t buffer (caller must free) or NULL on error.
 * *out_width and *out_height are set to the frame dimensions.
 */
uint16_t *noise_profile_read_frame(const char *path, int frame_index,
                                   int *out_width, int *out_height);

#endif /* NOISE_PROFILE_H */
