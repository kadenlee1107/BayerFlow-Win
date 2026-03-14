/*
 * ari_reader.h — ARRIRAW (.ari) frame reader
 *
 * Reads uncompressed 12-bit (ALEXA) or 13-bit (ALEXA 35) packed Bayer data
 * from individual .ari frame files (folder-based, like CinemaDNG).
 *
 * Header: 4096 bytes, little-endian (SMPTE RDD 30:2014)
 * Pixel data: packed 12-bit MSB-first in 32-bit LE words, col-pair swapped
 * Bayer pattern: GRBG on sensor; stream order without col-swap = RGGB
 */

#ifndef ARI_READER_H
#define ARI_READER_H

#include <stdint.h>
#include <stddef.h>

#define ARI_OK         0
#define ARI_ERR_IO    -1
#define ARI_ERR_FMT   -2

/* ── Per-frame header info ───────────────────────────────────────────── */
typedef struct {
    int      width, height;           /* full sensor dimensions */
    int      active_x, active_y;      /* active image offset */
    int      active_w, active_h;      /* active image size */
    int      bits_per_pixel;          /* 12 or 13 */
    uint32_t data_offset;             /* pixel data start (usually 4096) */
    uint32_t data_size;               /* pixel data bytes */
    uint32_t white_balance_kelvin;
    float    wb_r, wb_g, wb_b;        /* white balance gains */
    uint32_t exposure_index;          /* ISO / EI */
    float    black_level, white_level;
    char     camera_model[64];
    uint32_t sensor_fps;              /* fps × 1000 */
    uint32_t project_fps;             /* fps × 1000 */
} AriFrameInfo;

/* ── Sequence reader (folder of .ari files) ──────────────────────────── */
typedef struct {
    char     dir_path[4096];
    char   **file_list;               /* natural-sorted full paths */
    int      file_count;
    int      width, height;
    int      bits_per_pixel;
    int      frame_count;
    int      frames_read;
    AriFrameInfo info;                /* metadata from first frame */
} AriReader;

/* Single-frame operations */
int  ari_parse_header(const char *path, AriFrameInfo *info);
int  ari_read_frame(const char *path, const AriFrameInfo *info,
                    uint16_t *bayer_out, int out_stride);

/* Sequence operations */
int  ari_reader_open(AriReader *r, const char *path);
int  ari_reader_read_frame(AriReader *r, uint16_t *bayer_out);
void ari_reader_close(AriReader *r);

/* Probes (lightweight, no decode) */
int  ari_reader_probe_frame_count(const char *path);
int  ari_reader_probe_dimensions(const char *path, int *width, int *height);

/* Buffer-based operations (used by MXF reader) */
int  ari_parse_header_buf(const uint8_t *hdr_4096, AriFrameInfo *info);
int  ari_unpack_pixels(const uint8_t *packed, size_t packed_size,
                       const AriFrameInfo *info,
                       uint16_t *bayer_out, int out_stride);

#endif /* ARI_READER_H */
