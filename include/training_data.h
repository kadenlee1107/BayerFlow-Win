/*
 * Training Data Collection — C Bridge Header
 *
 * Declares functions implemented in Swift (TrainingDataBridge.swift)
 * that the C pipeline (denoise_core.c) calls to submit extracted patches.
 *
 * Patch extraction happens after temporal filtering on every frame:
 * 2 random 256×256 Bayer patches per frame (noisy center + denoised output).
 */

#ifndef TRAINING_DATA_H
#define TRAINING_DATA_H

#include <stdint.h>

/* Check if the user has opted in to training data collection.
 * Returns 1 if enabled, 0 if disabled. Thread-safe. */
extern int training_data_enabled(void);

/* Submit a noisy/denoised patch pair for collection.
 * Both buffers are 256×256 uint16_t (Bayer, 131072 bytes each).
 * Swift side copies immediately — caller retains ownership.
 *
 * patch_w, patch_h   : patch dimensions (always 256×256)
 * frame_w, frame_h   : full frame dimensions
 * patch_x, patch_y   : top-left position of patch in full frame (2px-aligned for Bayer)
 * frame_idx          : absolute frame number in the clip
 * noise_sigma        : estimated noise level (auto or manual)
 * flow_mag           : average optical flow magnitude for this frame (pixels)
 * camera_model       : camera model string (hashed on Swift side for anonymization)
 * iso                : ISO sensitivity from metadata
 */
extern void training_data_submit_patch(
    const uint16_t *noisy,
    const uint16_t *denoised,
    int32_t patch_w, int32_t patch_h,
    int32_t frame_w, int32_t frame_h,
    int32_t patch_x, int32_t patch_y,
    int32_t frame_idx,
    float noise_sigma, float flow_mag,
    const char *camera_model, int32_t iso);

#endif /* TRAINING_DATA_H */
