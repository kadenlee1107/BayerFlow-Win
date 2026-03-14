#ifndef SPATIAL_DENOISE_H
#define SPATIAL_DENOISE_H

#include <stdint.h>

/* Apply spatial Haar wavelet denoising to a Bayer frame in-place.
 * Denoises each Bayer sub-channel (R, Gr, Gb, B) independently
 * using multi-level Haar wavelet decomposition + soft thresholding.
 *
 * bayer       : raw Bayer RGGB frame (width x height), modified in place
 * width,height: frame dimensions (must be even)
 * noise_sigma : noise standard deviation in 16-bit pixel units
 * strength    : denoising strength multiplier (1.0 = standard BayesShrink)
 */
void spatial_denoise_frame(uint16_t *bayer, int width, int height,
                           float noise_sigma, float strength);

#endif
