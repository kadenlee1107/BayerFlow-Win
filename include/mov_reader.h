#ifndef MOV_READER_H
#define MOV_READER_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *pipe;           /* popen/ffmpeg path (CLI only, NULL in sandboxed app) */
    void *asset_reader;   /* AVAssetReader* retained ref (sandboxed app only) */
    void *track_output;   /* AVAssetReaderTrackOutput* retained ref (sandboxed app only) */
    /* Native ProRes RAW decoder path (no FFmpeg subprocess) */
    FILE *file;           /* direct file handle for native decode */
    uint32_t *frame_sizes;    /* per-frame compressed sizes from stsz */
    uint64_t *frame_offsets;  /* per-frame file offsets from co64/stco */
    uint8_t  *comp_buf;       /* reusable buffer for compressed frame data */
    int       comp_buf_size;
    int width, height;
    int frame_count;
    int frames_read;
    char filename[1024];
} MovReader;

/* Open a ProRes RAW MOV file for reading.
 * In CLI builds uses ffprobe/ffmpeg pipes; in sandboxed builds uses AVFoundation. */
int mov_reader_open(MovReader *r, const char *filename);

/* Read the next frame into bayer_out (must be width*height*2 bytes).
 * Returns 0 on success, -1 on EOF or error. */
int mov_reader_read_frame(MovReader *r, uint16_t *bayer_out);

/* Close the reader and FFmpeg pipe. */
void mov_reader_close(MovReader *r);

/* Probe frame count from a MOV file using ffprobe. Returns -1 on error. */
int mov_reader_probe_frame_count(const char *filename);

/* Probe width and height from a MOV file using ffprobe. Returns 0 on success. */
int mov_reader_probe_dimensions(const char *filename, int *width, int *height);

/* Probe white balance gains from ProRes RAW metadata (per-CCT table).
 * Returns R/G and B/G gain factors interpolated at 5000K.
 * Sets both to 1.0 on failure. Returns 0 on success, -1 on error. */
int mov_reader_probe_wb_gains(const char *filename, float *r_gain, float *b_gain);

#endif
