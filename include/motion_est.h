#ifndef MOTION_EST_H
#define MOTION_EST_H

#include <stdint.h>

typedef struct {
    int16_t dx, dy;
} MotionVector;

/* Extract half-resolution green channel from RGGB Bayer data.
 * Output is (width/2) x (height/2), averaging Gr and Gb per 2x2 cell.
 * green_out must be pre-allocated to (width/2)*(height/2) elements. */
void extract_green_channel(const uint16_t *bayer, int width, int height,
                           uint16_t *green_out);

/* Estimate per-block motion vectors from ref to cur frame.
 * Operates on half-res green channel images (green_w x green_h).
 * block_size is in green-channel pixels (e.g. 8 = 16 raw pixels).
 * Returns allocated array of (green_w/block_size) * (green_h/block_size) vectors.
 * MVs are in half-green-pixel units (= raw Bayer pixel units).
 * Caller must free() the returned array. */
MotionVector *motion_estimate(const uint16_t *ref_green, const uint16_t *cur_green,
                              int green_w, int green_h, int block_size);

/* Free motion vector array. */
void motion_vectors_free(MotionVector *mvs);

/* Lightweight 5x5 bilateral filter on green channel for OF preprocessing.
 * Smooths noise while preserving edges, improving optical flow accuracy
 * in dark regions. Operates in-place. noise_sigma is in 16-bit pixel units. */
void spatial_filter_green(uint16_t *green, int gw, int gh, float noise_sigma);

#endif
