/* platform_gpu_win.c — Windows GPU temporal filter implementation
 * TODO: Replace CPU fallback with CUDA kernel (temporal_filter.cu).
 *
 * CUDA port plan:
 *   - TemporalFilter.metal → temporal_filter.cu (mechanical translation)
 *   - 4-pass pipeline identical: collect → preestimate → fuse → finalize
 *   - cudaMallocManaged for ring buffers (unified memory, CPU+GPU access)
 *   - cudaStreamCreate for async commit/wait (replaces MTLCommandBuffer)
 *   - Expected speedup: 3× bandwidth (672 vs 200 GB/s) → ~0.023s/frame
 *
 * RTX 5070 expected wall clock: ~0.05s/frame (~20 fps) */

#include "platform_gpu.h"
#include "../../../RAWDenoiser/include/temporal_techniques.h"
#include <stdlib.h>
#include <string.h>

/* ---- Ring buffer (CPU malloc for now, CUDA unified memory later) ---- */

static int   g_num_slots = 0;
static int   g_width     = 0;
static int   g_height    = 0;
static uint16_t **g_frame_ring    = NULL;
static uint16_t **g_denoised_ring = NULL;

void platform_gpu_ring_init(int num_slots, int width, int height) {
    /* Free previous allocation if reinitializing */
    if (g_frame_ring) {
        for (int i = 0; i < g_num_slots; i++) {
            free(g_frame_ring[i]);
            free(g_denoised_ring[i]);
        }
        free(g_frame_ring);
        free(g_denoised_ring);
    }
    g_num_slots = num_slots;
    g_width     = width;
    g_height    = height;
    size_t frame_bytes = (size_t)width * height * sizeof(uint16_t);
    g_frame_ring    = malloc(num_slots * sizeof(uint16_t *));
    g_denoised_ring = malloc(num_slots * sizeof(uint16_t *));
    for (int i = 0; i < num_slots; i++) {
        g_frame_ring[i]    = malloc(frame_bytes);
        g_denoised_ring[i] = malloc(frame_bytes);
    }
}

uint16_t *platform_gpu_ring_frame_ptr(int slot) {
    if (slot < 0 || slot >= g_num_slots) return NULL;
    return g_frame_ring[slot];
}

uint16_t *platform_gpu_ring_denoised_ptr(int slot) {
    if (slot < 0 || slot >= g_num_slots) return NULL;
    return g_denoised_ring[slot];
}

/* ---- Temporal filter (CPU fallback — replace with CUDA) ---- */

static void run_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    /* Build frame pointer array from ring */
    const uint16_t *frame_ptrs[64];
    int n = (num_frames < 64) ? num_frames : 64;
    int neighbors_used = 0;
    frame_ptrs[center_idx] = use_denoised[center_idx]
        ? g_denoised_ring[ring_slots[center_idx]]
        : g_frame_ring[ring_slots[center_idx]];

    for (int dist = 1; dist <= n; dist++) {
        if (neighbors_used >= max_neighbors) break;
        for (int sign = -1; sign <= 1; sign += 2) {
            if (neighbors_used >= max_neighbors) break;
            int i = center_idx + dist * sign;
            if (i < 0 || i >= n) continue;
            frame_ptrs[i] = use_denoised[i]
                ? g_denoised_ring[ring_slots[i]]
                : g_frame_ring[ring_slots[i]];
            neighbors_used++;
        }
    }

    /* TODO: replace with CUDA kernel call */
    technique_vst_bilateral(output, frame_ptrs, flows_x, flows_y,
                            num_frames, center_idx, width, height, noise_sigma);
    (void)black_level; (void)shot_gain; (void)read_noise;
}

void platform_gpu_temporal_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    run_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                      num_frames, center_idx, width, height,
                      noise_sigma, black_level, shot_gain, read_noise, max_neighbors);
}

/* ---- Async stub (synchronous for now, CUDA stream later) ---- */

static uint16_t  *g_async_output     = NULL;
static int        g_async_committed  = 0;

void platform_gpu_temporal_vst_bilateral_commit(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    /* TODO: launch CUDA kernel async, store cudaStream_t for wait() */
    run_vst_bilateral(output, ring_slots, use_denoised, flows_x, flows_y,
                      num_frames, center_idx, width, height,
                      noise_sigma, black_level, shot_gain, read_noise, max_neighbors);
    g_async_output    = output;
    g_async_committed = 1;
}

int platform_gpu_temporal_wait(void) {
    /* TODO: cudaStreamSynchronize() */
    g_async_committed = 0;
    return 1;
}

int platform_gpu_available(void) {
    /* TODO: return 1 after CUDA init succeeds */
    return 0;
}
