/*
 * GoPro CineForm (CFHD) Encoder — Public API
 *
 * Encodes YUV 4:2:2 10-bit video into CineForm HD bitstream.
 * Self-contained pure C, no external dependencies.
 */

#ifndef CINEFORM_ENC_H
#define CINEFORM_ENC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CF_ENC_OK        0
#define CF_ENC_ERR_PARAM -1
#define CF_ENC_ERR_MEM   -2
#define CF_ENC_ERR_SIZE  -3

/* Quality presets (matches FFmpeg cfhdenc quality levels) */
#define CF_QUALITY_FILM3P   0   /* film3+  — highest quality */
#define CF_QUALITY_FILM3    1
#define CF_QUALITY_FILM2P   2
#define CF_QUALITY_FILM2    3
#define CF_QUALITY_FILM1PP  4
#define CF_QUALITY_FILM1P   5
#define CF_QUALITY_FILM1    6
#define CF_QUALITY_HIGHP    7
#define CF_QUALITY_HIGH     8   /* good default */
#define CF_QUALITY_MEDIUMP  9
#define CF_QUALITY_MEDIUM  10
#define CF_QUALITY_LOWP    11
#define CF_QUALITY_LOW     12

typedef struct CfEncoder CfEncoder;

typedef struct {
    int width, height;      /* frame dimensions (width must be multiple of 16, height >= 32) */
    int quality;            /* CF_QUALITY_* (0=best, 12=worst) */
} CfEncConfig;

/*
 * Initialize encoder. Allocates wavelet buffers and builds lookup tables.
 * Returns CF_ENC_OK on success.
 */
int cf_enc_init(CfEncoder **enc, const CfEncConfig *cfg);

/*
 * Encode one frame.
 * Input: YUV 4:2:2 10-bit planar (yuv422p10le).
 *   planes[0] = Y  (width × height, uint16_t)
 *   planes[1] = Cb (width/2 × height, uint16_t)
 *   planes[2] = Cr (width/2 × height, uint16_t)
 *   strides[] = stride in samples (not bytes) for each plane
 *
 * Output: CFHD bitstream written to `output`. Returns packet size in bytes,
 * or negative error code.
 */
int cf_enc_encode_frame(CfEncoder *enc,
                        const uint16_t *planes[3], const int strides[3],
                        uint8_t *output, int output_size);

/*
 * Free encoder resources.
 */
void cf_enc_close(CfEncoder *enc);

/* ------------------------------------------------------------------ */
/* High-level MOV writer (encoder + container)                         */
/* ------------------------------------------------------------------ */

typedef struct CfMovWriter CfMovWriter;

/*
 * Open a CineForm MOV file for writing.
 * Writes ftyp + wide + mdat header, then frames can be appended.
 */
int cf_mov_writer_open(CfMovWriter **w, const char *filename,
                       int width, int height, double fps, int quality);

/*
 * Encode and append one YUV422P10 frame.
 * Returns CF_ENC_OK on success.
 */
int cf_mov_writer_add_frame(CfMovWriter *w,
                            const uint16_t *planes[3], const int strides[3]);

/*
 * Finalize the MOV file (patches mdat size, writes moov atom).
 * Frees all resources.
 */
void cf_mov_writer_close(CfMovWriter *w);

#ifdef __cplusplus
}
#endif

#endif /* CINEFORM_ENC_H */
