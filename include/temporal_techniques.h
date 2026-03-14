#ifndef TEMPORAL_TECHNIQUES_H
#define TEMPORAL_TECHNIQUES_H

#include <stdint.h>

/* Function signature for all alternative temporal denoising techniques.
 * Same interface as the baseline NLM: receives a window of aligned Bayer frames
 * with per-frame optical flow, produces a denoised center frame. */
typedef void (*TechniqueFn)(
    uint16_t       *output,
    const uint16_t **frames,
    const float    **flows_x,
    const float    **flows_y,
    int num_frames,
    int center_idx,
    int width,
    int height,
    float noise_sigma);

/* Technique 1: Variance Stabilizing Transform + Temporal Wiener Shrinkage.
 * Per-pixel, no patches. Mathematically optimal for Gaussian noise. */
void technique_vst_wiener(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 2: Inverse-Variance Weighted Mean.
 * Per-pixel. Weights by 1/sigma^2(pixel_value) from calibrated noise model. */
void technique_inv_variance(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 3: VST + Uniform NLM (no signal-dependent heuristics).
 * 5x5 same-color patches in Anscombe domain, fixed bandwidth h=1.0. */
void technique_vst_nlm(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 4: Temporal DCT Shrinkage.
 * Per-pixel. 1D DCT across temporal axis, hard-threshold. */
void technique_dct_shrinkage(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 5: Sigma-Clipped Robust Mean.
 * Per-pixel. Iterative 2.5-sigma outlier rejection, then weighted mean. */
void technique_sigma_clip(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 6: VST + Wiener + Guided Filter.
 * Phase 1: VST+Wiener temporal fusion (maximum NR, blurs edges).
 * Phase 2: Guided filter using center VST frame as guide.
 *   Flat regions: output ≈ Wiener (max NR).
 *   Edges: output ≈ center frame (edge preserved).
 *   One parameter: epsilon = noise variance (1.0 in VST domain). */
void technique_vst_guided(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique 7: VST + Temporal Bilateral.
 * Bilateral range kernel in Anscombe domain with h = noise_std ≈ 1.0.
 * Edge preservation from range kernel (large z-diff → low weight).
 * Per-pixel, no patches. Fast. Analytically determined bandwidth. */
void technique_vst_bilateral(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Parameter variants for tuning sweep */
void technique_vst_guided_e01(   /* ε=0.1 */
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);
void technique_vst_guided_e03(   /* ε=0.3 */
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);
void technique_vst_bilateral_h08( /* h=0.8 */
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);
void technique_vst_bilateral_h12( /* h=1.2 */
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height, float noise_sigma);

/* Technique name lookup (indices 0-11, 0 = baseline NLM). */
static const char *technique_names[] = {
    "Baseline NLM",
    "VST+Wiener",
    "Inv-Variance",
    "VST+NLM",
    "DCT Shrinkage",
    "Sigma-Clip",
    "GF e=1.0",
    "BL h=1.0",
    "GF e=0.1",
    "GF e=0.3",
    "BL h=0.8",
    "BL h=1.2"
};

#define NUM_TECHNIQUES 12

#endif /* TEMPORAL_TECHNIQUES_H */
