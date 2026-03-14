/* platform_gpu_mac_stub.c — Mac CLI stub for CMake builds.
 * The real Mac implementation is MetalTemporalFilter.swift (Metal, Xcode only).
 * This stub runs the CPU fallback so the CMake CLI build works on Mac. */

#include "platform_gpu.h"
#include <stdlib.h>
#include <string.h>

static int       g_num_slots = 0;
static uint16_t **g_frame_ring    = NULL;
static uint16_t **g_denoised_ring = NULL;

void platform_gpu_ring_init(int num_slots, int width, int height) {
    g_num_slots = num_slots;
    size_t fb = (size_t)width * height * sizeof(uint16_t);
    g_frame_ring    = malloc(num_slots * sizeof(uint16_t *));
    g_denoised_ring = malloc(num_slots * sizeof(uint16_t *));
    for (int i = 0; i < num_slots; i++) {
        g_frame_ring[i]    = malloc(fb);
        g_denoised_ring[i] = malloc(fb);
    }
}

uint16_t *platform_gpu_ring_frame_ptr(int slot)    { return (slot >= 0 && slot < g_num_slots) ? g_frame_ring[slot]    : NULL; }
uint16_t *platform_gpu_ring_denoised_ptr(int slot) { return (slot >= 0 && slot < g_num_slots) ? g_denoised_ring[slot] : NULL; }

void platform_gpu_temporal_vst_bilateral(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    /* CPU fallback — calls existing C implementation */
    (void)ring_slots; (void)use_denoised; (void)flows_x; (void)flows_y;
    (void)num_frames; (void)center_idx; (void)width; (void)height;
    (void)noise_sigma; (void)black_level; (void)shot_gain; (void)read_noise;
    (void)max_neighbors; (void)output;
    /* TODO: wire to technique_vst_bilateral */
}

void platform_gpu_temporal_vst_bilateral_commit(
    uint16_t *output,
    const int *ring_slots, const int *use_denoised,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float noise_sigma, float black_level, float shot_gain, float read_noise,
    int max_neighbors)
{
    platform_gpu_temporal_vst_bilateral(output, ring_slots, use_denoised,
        flows_x, flows_y, num_frames, center_idx, width, height,
        noise_sigma, black_level, shot_gain, read_noise, max_neighbors);
}

int platform_gpu_temporal_wait(void) { return 1; }
int platform_gpu_available(void)     { return 0; }
