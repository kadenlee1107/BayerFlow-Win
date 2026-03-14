#ifndef DNG_READER_H
#define DNG_READER_H

#include <stdint.h>

/* Bayer CFA pattern codes */
#define DNG_CFA_RGGB  0
#define DNG_CFA_GRBG  1
#define DNG_CFA_GBRG  2
#define DNG_CFA_BGGR  3

typedef struct {
    char    dir_path[1024];     /* directory containing DNG files */
    char  **file_list;          /* sorted array of full paths */
    int     file_count;         /* number of DNG files */
    int     width, height;
    int     bits_per_sample;    /* 12, 14, or 16 */
    int     compression;        /* 1=uncompressed, 7=LJPEG */
    int     cfa_pattern;        /* DNG_CFA_RGGB etc. */
    uint16_t black_level;
    uint16_t white_level;
    int     frame_count;
    int     frames_read;
} DngReader;

/* Open a CinemaDNG sequence.
 * path can be a directory of .dng files or a single .dng file (will find siblings).
 * Returns 0 on success, -1 on error. */
int dng_reader_open(DngReader *r, const char *path);

/* Read the next frame into bayer_out (must be width*height*2 bytes).
 * Output is always uint16 RGGB layout regardless of source CFA pattern.
 * Returns 0 on success, -1 on EOF or error. */
int dng_reader_read_frame(DngReader *r, uint16_t *bayer_out);

/* Close the reader and free file list. */
void dng_reader_close(DngReader *r);

/* Probe frame count from a DNG sequence (directory or single file).
 * Returns frame count, or -1 on error. */
int dng_reader_probe_frame_count(const char *path);

/* Probe width and height from first DNG in sequence.
 * Returns 0 on success, -1 on error. */
int dng_reader_probe_dimensions(const char *path, int *width, int *height);

/* Probe white balance gains from DNG AsShotNeutral tag.
 * Returns 0 on success, -1 if not found. */
int dng_reader_probe_wb_gains(const char *path, float *r_gain, float *b_gain);

#endif /* DNG_READER_H */
