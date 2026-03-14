/*
 * mxf_reader.h — MXF/ARRIRAW reader
 *
 * Reads ARRIRAW essence from MXF containers (SMPTE RDD 54:2022).
 * Modern ARRI cameras (ALEXA Mini LF, ALEXA 35, etc.) record ARRIRAW
 * as a single MXF file per clip rather than individual .ari frame files.
 *
 * The pixel data inside MXF is identical to standalone .ari files:
 * each frame contains a 4096-byte ARRI header + packed 12/13-bit Bayer data.
 */

#ifndef MXF_READER_H
#define MXF_READER_H

#include <stdint.h>
#include <stdio.h>
#include "ari_reader.h"   /* reuse AriFrameInfo for ARRI header parsing */

#define MXF_OK         0
#define MXF_ERR_IO    -1
#define MXF_ERR_FMT   -2

typedef struct {
    char     file_path[4096];
    FILE    *file;
    int64_t *frame_offsets;    /* byte offset of each ARRIRAW frame in MXF */
    int64_t *frame_sizes;      /* byte size of each frame's essence data */
    int      frame_count;
    int      frames_read;
    int      width, height;
    int      bits_per_pixel;
    AriFrameInfo info;         /* metadata from first frame's ARRI header */
} MxfReader;

/* Open an MXF file containing ARRIRAW essence. */
int  mxf_reader_open(MxfReader *r, const char *path);

/* Read next frame into bayer_out (16-bit, width stride). Returns 0 or -1. */
int  mxf_reader_read_frame(MxfReader *r, uint16_t *bayer_out);

/* Close and free resources. */
void mxf_reader_close(MxfReader *r);

/* Probes (lightweight). */
int  mxf_reader_probe_frame_count(const char *path);
int  mxf_reader_probe_dimensions(const char *path, int *width, int *height);

/* Check if a file is an MXF container (reads first 16 bytes). */
int  mxf_is_mxf_file(const char *path);

#endif /* MXF_READER_H */
