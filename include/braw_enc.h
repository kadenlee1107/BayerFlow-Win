/*
 * Blackmagic RAW (BRAW) Encoder
 *
 * Encodes 12-bit Bayer RGGB frames to BRAW-compressed packets.
 * Inverse of braw_dec.c — uses same DCT/VLC/quant tables.
 */

#ifndef BRAW_ENC_H
#define BRAW_ENC_H

#include <stdint.h>
#include <stddef.h>

/* Return codes */
#define BRAW_ENC_OK             0
#define BRAW_ENC_ERR_INVALID   (-1)
#define BRAW_ENC_ERR_ALLOC     (-2)
#define BRAW_ENC_ERR_OVERFLOW  (-3)

/* Encoder configuration — extracted from source BRAW or set manually */
typedef struct {
    int width;
    int height;
    int version;                  /* BRAW version (1 or 2, default 2) */
    int qscale[3];                /* quantization scales */
    int nb_tiles_w, nb_tiles_h;   /* tile grid */
    int tile_size_h;              /* tile height in pixels */
    int tile_widths[256];         /* per-column tile width */

    /* Raw base quant tables (before scaling) */
    uint16_t base_quant_luma[64];
    uint16_t base_quant_chroma[32];

    /* bmdf metadata header blob (copied from source) */
    uint8_t bmdf_blob[256];
    int bmdf_blob_size;

    /* Picture header raw bytes (offset 0x100..0x180 from packet start) */
    uint8_t pic_header[128];
    int pic_header_size;
} BrawEncConfig;

/* Encoder context */
typedef struct {
    BrawEncConfig config;
    int initialized;

    /* Tile grid (computed from config) */
    int nb_tiles_w, nb_tiles_h;
    int tile_size_h;
    int tile_widths[256];
    int tile_offsets_w[257];      /* cumulative x offsets */

    /* Scaled quantization tables (for forward quant: divisor) */
    int quant_luma[64];
    int quant_chroma[32];

    /* Forward DCT matrices */
    double fwd_col_8[8][8];      /* 8-point column (undoes COL_SHIFT=17) */
    double fwd_row_8[8][8];      /* 8-point row (undoes ROW_SHIFT=16) */
    double fwd_col_4[4][4];      /* 4-point column (undoes chroma 4-point iDCT) */

    /* Working buffers */
    int16_t block[4][64];         /* DCT coefficient blocks */
    uint16_t recon_luma[16 * 8];  /* reconstructed luma (from dequant+iDCT) */
    uint16_t chroma_buf[2][32];   /* chroma pixel buffers (8x4 each) */
    uint16_t luma_green[8 * 8];   /* green-interpolated block for luma DCT */
} BrawEncContext;

/* Initialize encoder from config. Call once. */
int braw_enc_init(BrawEncContext *ctx, const BrawEncConfig *cfg);

/* Free encoder resources. */
void braw_enc_free(BrawEncContext *ctx);

/* Encode one 12-bit Bayer RGGB frame into a BRAW packet.
 * input:       12-bit Bayer RGGB (uint16_t, values 0-4095)
 * in_stride:   input row stride in uint16_t units (>= width)
 * output:      output buffer (caller-allocated)
 * output_size: size of output buffer in bytes
 * Returns: packet size in bytes on success, negative on error. */
int braw_enc_encode_frame(BrawEncContext *ctx,
                          const uint16_t *input, int in_stride,
                          uint8_t *output, int output_size);

/* Extract encoder config from a decoded BRAW frame packet.
 * Copies quant tables, qscale, tile layout, bmdf metadata, and picture header.
 * width/height: frame dimensions (from braw_dec_probe). */
int braw_enc_config_from_packet(BrawEncConfig *cfg,
                                const uint8_t *packet, size_t packet_size,
                                int width, int height);

#endif /* BRAW_ENC_H */
