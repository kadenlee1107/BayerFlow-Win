/*
 * Minimal EXR Writer — Half-Float RGB
 *
 * Writes uncompressed OpenEXR files with 3 half-float channels (R, G, B).
 * Pure C, no external dependencies.
 *
 * Input: 16-bit unsigned integer planar RGB [R: w*h] [G: w*h] [B: w*h]
 * Output: Half-float EXR normalized to [0.0, 1.0] range
 */

#ifndef EXR_WRITER_H
#define EXR_WRITER_H

#include <stdint.h>

/* Write a single EXR frame from planar 16-bit RGB data.
 *
 * path       : output file path (e.g., "output/frame_000001.exr")
 * rgb_planar : 3 planar channels, each width*height uint16_t values
 *              Layout: [R plane: w*h] [G plane: w*h] [B plane: w*h]
 * width      : image width in pixels
 * height     : image height in pixels
 *
 * Returns 0 on success, -1 on error.
 */
int exr_write_frame(const char *path, const uint16_t *rgb_planar,
                    int width, int height);

#endif /* EXR_WRITER_H */
