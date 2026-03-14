/*
 * Canon Cinema RAW Light (CRM) Decoder
 *
 * Decodes CRX-compressed frames from .CRM files to 12-bit Bayer RGGB.
 * Ported from LibRaw's crx.cpp (Alexey Danilchenko, 2018-2019).
 *
 * CRX codec: Le Gall 5/3 integer wavelet + adaptive Rice-Golomb entropy coding
 * Container: ISOBMFF (QuickTime-like) with Canon-specific boxes (CNCV, CMP1, CDI1)
 */

#ifndef CRM_DEC_H
#define CRM_DEC_H

#include <stdint.h>
#include <stddef.h>

/* Return codes */
#define CRM_OK              0
#define CRM_ERR_INVALID    (-1)
#define CRM_ERR_ALLOC      (-2)
#define CRM_ERR_IO         (-3)
#define CRM_ERR_BITSTREAM  (-4)

/* Limits */
#define CRM_MAX_PLANES       4
#define CRM_MAX_LEVELS       3
#define CRM_MAX_SUBBANDS    10  /* 3*levels + 1 */
#define CRM_MAX_TILES       64
#define CRX_BUF_SIZE     0x10000

/* ---- CMP1 Codec Parameters ---- */
typedef struct {
    int version;               /* 0x100 or 0x200 */
    int f_width, f_height;     /* plane dimensions (half of full Bayer res) */
    int tile_width, tile_height;
    int n_bits;                /* bits per sample (10, 12, or 14) */
    int n_planes;              /* color planes (4 for Bayer) */
    int cfa_layout;            /* 0=RGGB */
    int enc_type;              /* 0=raw, 1=signed, 3=decorrelated */
    int image_levels;          /* wavelet levels (0=lossless, 3=lossy) */
    int has_tile_cols;
    int has_tile_rows;
    int mdat_hdr_size;
    int median_bits;
    int sample_precision;
} CrmCodecParams;

/* ---- CRM ISOBMFF Container ---- */
typedef struct {
    int frame_count;
    int width, height;         /* full Bayer dimensions */
    double fps;
    int64_t *frame_offsets;
    int32_t *frame_sizes;
    CrmCodecParams codec;

    /* Metadata for container writer (Phase 2) */
    uint8_t *stsd_entry;
    int stsd_entry_size;
} CrmMovInfo;

int crm_mov_parse(const char *path, CrmMovInfo *mov);
void crm_mov_free(CrmMovInfo *mov);
int crm_mov_read_frame(const char *path, const CrmMovInfo *mov,
                       int frame_idx, uint8_t **packet_out, size_t *size_out);

/* ---- CRX Codec Decoder ---- */

/* Opaque decoder context (defined in crm_dec.c) */
typedef struct CrmDecContext CrmDecContext;

/* Allocate decoder context. Caller must free with crm_dec_free(). */
CrmDecContext *crm_dec_alloc(void);

/* Initialize decoder from codec params. Call once per clip. */
int crm_dec_init(CrmDecContext *ctx, const CrmCodecParams *params);

/* Free decoder resources. */
void crm_dec_free(CrmDecContext *ctx);

/* Decode a single CRM frame.
 *
 * packet:      raw frame data from CRM container (mdat payload for one sample)
 * packet_size: size in bytes
 * output:      must hold width*height uint16_t values
 *              Output is Bayer (CFA layout from codec params), values 0..4095 for 12-bit
 *              Expanded to 16-bit: (val << 4) | (val >> 8)
 * out_stride:  row stride in uint16_t units (>= width)
 *
 * Returns CRM_OK on success. */
int crm_dec_decode_frame(CrmDecContext *ctx,
                         const uint8_t *packet, size_t packet_size,
                         uint16_t *output, int out_stride);

/* Probe frame dimensions without decoding. */
int crm_dec_probe(const uint8_t *packet, size_t packet_size,
                  int *width, int *height);

#endif /* CRM_DEC_H */
