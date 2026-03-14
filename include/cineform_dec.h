/*
 * GoPro CineForm (CFHD) Decoder
 *
 * Decodes CineForm HD video from MOV containers.
 * Supports Bayer RAW CFA output (12-bit) and YUV 4:2:2 (10-bit).
 *
 * Based on the CineForm codec specification:
 * - Tag/value pair bitstream (SMPTE ST 2073)
 * - 2-6 wavelet, 3-level decomposition
 * - Huffman VLC + run-length coding for highpass bands
 *
 * References:
 *   - GoPro CineForm SDK (Apache 2.0 / MIT): github.com/gopro/cineform-sdk
 *   - FFmpeg libavcodec/cfhd.c (LGPL, used as reference only)
 */

#ifndef CINEFORM_DEC_H
#define CINEFORM_DEC_H

#include <stdint.h>

/* Return codes */
#define CF_OK        0
#define CF_ERR_IO   -1
#define CF_ERR_FMT  -2
#define CF_ERR_MEM  -3
#define CF_ERR_VLC  -4

/* Pixel format of decoded output */
typedef enum {
    CF_PIX_BAYER_RGGB16 = 0,   /* 16-bit Bayer RGGB (RAW) */
    CF_PIX_YUV422_10    = 1,   /* 10-bit YUV 4:2:2 */
    CF_PIX_RGB48        = 2,   /* 16-bit RGB 4:4:4 */
} CfPixelFormat;

typedef struct {
    int   width, height;
    int   frame_count;
    float fps;
    int   channels;            /* typically 4 for Bayer, 3 for RGB, 1 for Y */
    CfPixelFormat pixel_format;
    int   encoded_format;      /* internal CineForm format code */
    int   is_bayer;            /* 1 if Bayer RAW CFA */
} CfInfo;

/* Opaque reader handle */
typedef struct CfReader CfReader;

/* Open a MOV/AVI file containing CineForm video.
 * Allocates and returns reader via *out.
 * Returns CF_OK on success, negative on error. */
int cf_reader_open(CfReader **out, const char *path);

/* Get file metadata. */
int cf_reader_get_info(const CfReader *r, CfInfo *info);

/* Decode frame at frame_idx to 16-bit Bayer RGGB.
 * bayer_out must hold width * height uint16_t values.
 * For YUV content, outputs Y + Cb + Cr planes concatenated.
 * Returns CF_OK on success. */
int cf_reader_read_frame(CfReader *r, int frame_idx, uint16_t *bayer_out);

/* Decode frame to 16-bit RGB (for YUV CineForm — GoPro footage).
 * Converts YUV 4:2:2 → RGB 4:4:4 using inverse BT.709.
 * rgb_out must hold width * height * 3 uint16_t values (R,G,B planes). */
int cf_reader_read_frame_rgb(CfReader *r, int frame_idx, uint16_t *rgb_out);

/* Close and free all resources. */
void cf_reader_close(CfReader *r);

/* Probe functions */
int cf_reader_probe_frame_count(const char *path);
int cf_reader_probe_dimensions(const char *path, int *width, int *height);

#endif /* CINEFORM_DEC_H */
