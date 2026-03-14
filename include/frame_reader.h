#ifndef FRAME_READER_H
#define FRAME_READER_H

#include <stdint.h>

/* Input format type */
typedef enum {
    FORMAT_UNKNOWN    = -1,
    FORMAT_MOV        = 0,
    FORMAT_CINEMADNG  = 1,
    FORMAT_BRAW       = 2,
    FORMAT_CRM        = 3,   /* Canon Cinema RAW Light (.crm) */
    FORMAT_ARRIRAW    = 4,   /* ARRI ARRIRAW (.ari) */
    FORMAT_MXF        = 5,   /* MXF/ARRIRAW (.mxf) */
    FORMAT_R3D        = 6,   /* RED R3D (.r3d) / Nikon N-RAW (.nraw) */
    FORMAT_CINEFORM   = 7,   /* GoPro CineForm (.mov with CFHD) */
    FORMAT_ZRAW       = 8,   /* Z CAM ZRAW (.mov with zraw) */
} InputFormat;

/* Unified frame reader — dispatches to MovReader or DngReader via function pointers. */
typedef struct FrameReader {
    int width, height;
    int frame_count;
    int frames_read;
    InputFormat format;
    int is_rgb;   /* 1 if format outputs RGB (not Bayer) — e.g. R3D */
    float fps;    /* frame rate (0 = unknown, use default) */

    /* Implementation-specific storage (MovReader* or DngReader*) */
    void *impl;

    /* Virtual methods (set by frame_reader_open) */
    int  (*read_frame)(struct FrameReader *r, uint16_t *bayer_out);
    void (*close)(struct FrameReader *r);

    /* RGB virtual method — for formats that output debayered RGB.
     * rgb_out: 3 planar channels, each width*height uint16_t.
     * Only set when is_rgb == 1. */
    int  (*read_frame_rgb)(struct FrameReader *r, uint16_t *rgb_out);
} FrameReader;

/* Open any supported input format (auto-detects MOV vs CinemaDNG).
 * Returns 0 on success, -1 on error. */
int frame_reader_open(FrameReader *r, const char *path);

/* Read next frame into bayer_out. Returns 0 on success, -1 on EOF/error. */
int frame_reader_read_frame(FrameReader *r, uint16_t *bayer_out);

/* Read next frame as planar RGB (for is_rgb formats like R3D).
 * rgb_out must hold 3 * width * height uint16_t values.
 * Returns 0 on success, -1 on EOF/error. */
int frame_reader_read_frame_rgb(FrameReader *r, uint16_t *rgb_out);

/* Close and free resources. */
void frame_reader_close(FrameReader *r);

/* Probe functions (auto-detect format). */
int frame_reader_probe_frame_count(const char *path);
int frame_reader_probe_dimensions(const char *path, int *width, int *height);
int frame_reader_probe_wb_gains(const char *path, float *r_gain, float *b_gain);

/* Detect input format from path. */
InputFormat detect_input_format(const char *path);

/* Get DNG-specific metadata needed for DNG output.
 * Returns 0 on success, -1 if not CinemaDNG format.
 * first_file_path returns a pointer to internal storage (do not free). */
int frame_reader_get_dng_info(const FrameReader *r,
                              int *bits_per_sample,
                              int *cfa_pattern,
                              const char **first_file_path);

#endif /* FRAME_READER_H */
