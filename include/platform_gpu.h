/* platform_gpu.h — GPU temporal filter platform abstraction
 * Mac:     Metal (TemporalFilter.metal + MetalTemporalFilter.swift)
 * Windows: CUDA (temporal_filter.cu) */

#pragma once
#include <stdint.h>

/* Initialize GPU ring buffer storage.
 * Must be called before any gpu_ring_* functions. */
void platform_gpu_ring_init(int num_slots, int width, int height);

/* Get CPU-writable pointer for raw frame ring slot. */
uint16_t *platform_gpu_ring_frame_ptr(int slot);

/* Get CPU-writable pointer for denoised cache ring slot. */
uint16_t *platform_gpu_ring_denoised_ptr(int slot);

/* VST+Bilateral temporal filter using ring buffers.
 * max_neighbors: motion-adaptive limit (4/8/14). */
void platform_gpu_temporal_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma,
    float black_level, float shot_gain, float read_noise,
    int max_neighbors);

/* Async variant: commit GPU work, return immediately.
 * Output invalid until platform_gpu_temporal_wait() returns 1. */
void platform_gpu_temporal_vst_bilateral_commit(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma,
    float black_level, float shot_gain, float read_noise,
    int max_neighbors);

/* Wait for previously committed async GPU work. Returns 1 on success. */
int platform_gpu_temporal_wait(void);

/* Returns 1 if GPU acceleration is available. */
int platform_gpu_available(void);
