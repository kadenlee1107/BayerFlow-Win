/*
 * RED R3D Reader
 *
 * Decodes RED R3D (.r3d) files using the RED SDK.
 * Also handles Nikon N-RAW (.nraw) which uses the same codec.
 *
 * IMPORTANT: Unlike other readers that output raw Bayer, the RED SDK
 * only provides debayered RGB. Output is 16-bit planar RGB (3 planes,
 * each width*height uint16_t values: R plane, G plane, B plane).
 *
 * The caller must use the RGB denoising path (not Bayer) for R3D input.
 */

#ifndef R3D_READER_H
#define R3D_READER_H

#include <stdint.h>

/* Return codes */
#define R3D_OK        0
#define R3D_ERR_IO   -1
#define R3D_ERR_FMT  -2
#define R3D_ERR_SDK  -3   /* RED SDK not available or failed */

typedef struct {
    int   width, height;
    int   frame_count;
    float fps;
    int   iso;
    int   white_balance_kelvin;
    char  camera_model[64];
    int   is_rgb_output;         /* always 1 — signals RGB pipeline */
} R3dInfo;

/* Opaque reader handle */
typedef struct R3dReader R3dReader;

/* Open an R3D (or N-RAW) file.
 * Allocates and returns reader via *out.
 * Returns R3D_OK on success, negative on error. */
int r3d_reader_open(R3dReader **out, const char *path);

/* Get file metadata. */
int r3d_reader_get_info(const R3dReader *r, R3dInfo *info);

/* Decode frame at frame_idx to 16-bit planar RGB.
 * rgb_out must hold 3 * width * height uint16_t values.
 * Layout: [R plane: w*h] [G plane: w*h] [B plane: w*h]
 * Returns R3D_OK on success. */
int r3d_reader_read_frame_rgb(R3dReader *r, int frame_idx,
                               uint16_t *rgb_out);

/* Close and free all resources. */
void r3d_reader_close(R3dReader *r);

/* Probe functions (open file, read metadata, close). */
int r3d_reader_probe_frame_count(const char *path);
int r3d_reader_probe_dimensions(const char *path, int *width, int *height);

/* Returns 1 if RED SDK was linked at compile time, 0 if stub. */
int r3d_sdk_available(void);

#endif /* R3D_READER_H */
