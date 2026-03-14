/*
 * Z CAM ZRAW Decoder
 *
 * Decodes ZRAW compressed Bayer RAW video from Z CAM E2 series cameras.
 * Supports pre-v0.94 firmware format (compressed 12-bit CFA).
 *
 * ZRAW uses:
 * - MOV container with 'zraw' FourCC
 * - Block-based variable-length compression (custom Huffman + adaptive context)
 * - 12-bit Bayer CFA output
 *
 * v0.94+ firmware uses HEVC (not raw Bayer) — NOT supported here.
 *
 * References:
 *   - github.com/storyboardcreativity/zraw-decoder-lib (GPL)
 *   - github.com/storyboardcreativity/zraw-parser (GPL)
 *   - Studied as reference for format understanding; clean-room reimplementation.
 */

#ifndef ZRAW_DEC_H
#define ZRAW_DEC_H

#include <stdint.h>

/* Return codes */
#define ZRAW_OK        0
#define ZRAW_ERR_IO   -1
#define ZRAW_ERR_FMT  -2
#define ZRAW_ERR_MEM  -3
#define ZRAW_ERR_VER  -4  /* Unsupported ZRAW version (e.g. HEVC-based) */

typedef struct {
    int   width, height;
    int   frame_count;
    float fps;
    int   bit_depth;
    int   bayer_pattern;   /* 0=RGGB, 1=GRBG, 2=GBRG, 3=BGGR */
} ZrawInfo;

/* Opaque reader handle */
typedef struct ZrawReader ZrawReader;

int  zraw_reader_open(ZrawReader **out, const char *path);
int  zraw_reader_get_info(const ZrawReader *r, ZrawInfo *info);
int  zraw_reader_read_frame(ZrawReader *r, int frame_idx, uint16_t *bayer_out);
void zraw_reader_close(ZrawReader *r);

int zraw_reader_probe_frame_count(const char *path);
int zraw_reader_probe_dimensions(const char *path, int *width, int *height);

#endif /* ZRAW_DEC_H */
