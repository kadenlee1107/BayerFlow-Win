/* platform_gpu_win.c — Windows GPU temporal filter (CUDA backend)
 * Thin C wrapper around the extern "C" functions in temporal_filter.cu.
 * When CUDA is unavailable, falls back to CPU VST+bilateral. */

#include "platform_gpu.h"
#include "temporal_techniques.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Declarations from temporal_filter.cu (extern "C") ---- */
extern int       bf_cuda_init(int num_slots, int width, int height);
extern uint16_t *bf_cuda_ring_frame_ptr(int slot);
extern uint16_t *bf_cuda_ring_denoised_ptr(int slot);
extern int       bf_cuda_available(void);
extern void      bf_cuda_temporal_vst_bilateral(
    uint16_t *, const int *, const int *,
    const float **, const float **,
    int, int, int, int,
    float, float, float, float, int);
extern void      bf_cuda_temporal_vst_bilateral_commit(
    uint16_t *, const int *, const int *,
    const float **, const float **,
    int, int, int, int,
    float, float, float, float, int);
extern int bf_cuda_temporal_wait(void);

/* ---- CPU fallback ring (used when CUDA unavailable) ---- */
static int       g_cpu_num_slots = 0;
static int       g_cpu_width     = 0;
static int       g_cpu_height    = 0;
static uint16_t **g_cpu_frame    = NULL;
static uint16_t **g_cpu_denoised = NULL;

static void cpu_ring_init(int n, int w, int h) {
    if (g_cpu_frame) {
        for (int i = 0; i < g_cpu_num_slots; i++) {
            free(g_cpu_frame[i]); free(g_cpu_denoised[i]);
        }
        free(g_cpu_frame); free(g_cpu_denoised);
    }
    g_cpu_num_slots = n; g_cpu_width = w; g_cpu_height = h;
    size_t fb = (size_t)w * h * sizeof(uint16_t);
    g_cpu_frame    = (uint16_t **)malloc(n * sizeof(uint16_t *));
    g_cpu_denoised = (uint16_t **)malloc(n * sizeof(uint16_t *));
    for (int i = 0; i < n; i++) {
        g_cpu_frame[i]    = (uint16_t *)malloc(fb);
        g_cpu_denoised[i] = (uint16_t *)malloc(fb);
    }
}

/* ---- Platform interface ---- */

int platform_gpu_available(void) {
    static int checked = 0, result = 0;
    if (!checked) {
        result  = bf_cuda_available();
        checked = 1;
        fprintf(stderr, "GPU: %s\n", result ? "CUDA available" : "CPU fallback");
    }
    return result;
}

void platform_gpu_ring_init(int num_slots, int width, int height) {
    if (platform_gpu_available())
        bf_cuda_init(num_slots, width, height);
    else
        cpu_ring_init(num_slots, width, height);
}

uint16_t *platform_gpu_ring_frame_ptr(int slot) {
    if (platform_gpu_available()) return bf_cuda_ring_frame_ptr(slot);
    return (slot >= 0 && slot < g_cpu_num_slots) ? g_cpu_frame[slot] : NULL;
}

uint16_t *platform_gpu_ring_denoised_ptr(int slot) {
    if (platform_gpu_available()) return bf_cuda_ring_denoised_ptr(slot);
    return (slot >= 0 && slot < g_cpu_num_slots) ? g_cpu_denoised[slot] : NULL;
}

/* ---- CPU fallback: build ptrs from ring, call technique_vst_bilateral ---- */
static void cpu_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height,
    float noise_sigma, int max_neighbors)
{
    const uint16_t *ptrs[64] = {0};
    int count = 0;

    ptrs[center_idx] = use_denoised[center_idx]
        ? g_cpu_denoised[ring_slots[center_idx]]
        : g_cpu_frame[ring_slots[center_idx]];

    for (int dist = 1; dist <= num_frames && count < max_neighbors; dist++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            if (count >= max_neighbors) break;
            int f = center_idx + dist * sign;
            if (f < 0 || f >= num_frames) continue;
            ptrs[f] = use_denoised[f]
                ? g_cpu_denoised[ring_slots[f]]
                : g_cpu_frame[ring_slots[f]];
            count++;
        }
    }

    technique_vst_bilateral(output, ptrs, flows_x, flows_y,
                            num_frames, center_idx, width, height, noise_sigma);
}

void platform_gpu_temporal_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    if (platform_gpu_available())
        bf_cuda_temporal_vst_bilateral(
            output, ring_slots, use_denoised, flows_x, flows_y,
            num_frames, center_idx, width, height,
            noise_sigma, black_level, shot_gain, read_noise, max_neighbors);
    else
        cpu_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                          num_frames, center_idx, width, height,
                          noise_sigma, max_neighbors);
}

void platform_gpu_temporal_vst_bilateral_commit(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx, int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    if (platform_gpu_available())
        bf_cuda_temporal_vst_bilateral_commit(
            output, ring_slots, use_denoised, flows_x, flows_y,
            num_frames, center_idx, width, height,
            noise_sigma, black_level, shot_gain, read_noise, max_neighbors);
    else
        cpu_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                          num_frames, center_idx, width, height,
                          noise_sigma, max_neighbors);
}

int platform_gpu_temporal_wait(void) {
    return platform_gpu_available() ? bf_cuda_temporal_wait() : 1;
}
