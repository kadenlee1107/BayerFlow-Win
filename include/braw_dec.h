/*
 * Blackmagic RAW (BRAW) Decoder
 *
 * Decodes BRAW-compressed frames to 12-bit Bayer RGGB.
 * Based on reverse-engineering by Paul B Mahol (FFmpeg patch, 2019).
 *
 * BRAW uses DCT-based compression with VLC (Huffman) entropy coding:
 *   - Luma: 2x 8x8 DCT blocks per 16-pixel-wide unit (zigzag scan)
 *   - Chroma: 2x 8x4 DCT blocks (half_scan)
 *   - Decorrelation step to reconstruct Bayer pattern from luma/chroma
 *   - 12-bit output, optionally expanded to 16-bit
 */

#ifndef BRAW_DEC_H
#define BRAW_DEC_H

#include <stdint.h>
#include <stddef.h>

/* Return codes */
#define BRAW_OK              0
#define BRAW_ERR_INVALID    (-1)
#define BRAW_ERR_ALLOC      (-2)
#define BRAW_ERR_IO         (-3)
#define BRAW_ERR_BITSTREAM  (-4)

/* Maximum tile grid dimensions */
#define BRAW_MAX_TILES_W    256
#define BRAW_MAX_TILES_H    256
#define BRAW_MAX_TILES      1024

/* VLC lookup table entry */
typedef struct {
    uint8_t symbol;   /* decoded symbol index */
    uint8_t bits;     /* number of bits consumed (0 = invalid) */
} BrawVlcEntry;

/* Frame-level info parsed from the braw picture header */
typedef struct {
    int version;                          /* braw version (1 or 2) */
    int width;                            /* frame width in pixels */
    int height;                           /* frame height in pixels */
    int nb_tiles_w;                       /* number of tile columns */
    int nb_tiles_h;                       /* number of tile rows */
    int tile_size_h;                      /* tile height in pixels */
    int tile_size_w[BRAW_MAX_TILES_W];    /* per-column tile width */
    int tile_offset_w[BRAW_MAX_TILES_W+1]; /* cumulative x offsets */
    int qscale[3];                        /* quantization scales */
    int quant_luma[64];                   /* luma quant table (scaled) */
    int quant_chroma[32];                 /* chroma quant table (scaled) */
} BrawFrameInfo;

/* Decoder context */
typedef struct {
    /* VLC lookup tables (built once at init) */
    BrawVlcEntry *dc_vlc;     /* 2^13 = 8192 entries */
    BrawVlcEntry *ac_vlc;     /* 2^18 = 262144 entries */

    /* Working buffers */
    int16_t block[4][64];     /* DCT coefficient blocks */
    uint16_t chroma_out[2][64]; /* chroma iDCT output (8x4) */

    /* Current frame info */
    BrawFrameInfo info;

    int initialized;
} BrawDecContext;

/* Initialize decoder (builds VLC tables). Call once. */
int braw_dec_init(BrawDecContext *ctx);

/* Free decoder resources. */
void braw_dec_free(BrawDecContext *ctx);

/* Decode a single BRAW frame packet.
 *
 * packet:      raw frame data from MOV container (metadata header + braw block)
 * packet_size: size of packet in bytes
 * output:      output buffer, must hold width*height uint16_t values
 *              Output is 12-bit Bayer RGGB, values in [0, 4095]
 * out_stride:  output row stride in uint16_t units (>= width)
 * info:        if non-NULL, filled with parsed frame info
 *
 * Returns BRAW_OK on success, negative on error.
 */
int braw_dec_decode_frame(BrawDecContext *ctx,
                          const uint8_t *packet, size_t packet_size,
                          uint16_t *output, int out_stride,
                          BrawFrameInfo *info);

/* Probe a BRAW frame packet for dimensions without full decode.
 * Returns BRAW_OK on success, negative on error. */
int braw_dec_probe(const uint8_t *packet, size_t packet_size,
                   int *width, int *height);

/*
 * Simple MOV parser for BRAW files.
 * Extracts frame packet offsets and sizes.
 */

typedef struct {
    int frame_count;
    int width, height;
    double fps;
    int64_t *frame_offsets;    /* file offset of each frame packet */
    int32_t *frame_sizes;      /* size of each frame packet */
} BrawMovInfo;

/* Parse a .braw file's MOV container.
 * Returns BRAW_OK on success, negative on error.
 * Caller must call braw_mov_free() when done. */
int braw_mov_parse(const char *path, BrawMovInfo *mov);

/* Free parsed MOV info. */
void braw_mov_free(BrawMovInfo *mov);

/* Read a single frame packet from a .braw file.
 * Allocates *packet_out (caller must free).
 * Returns BRAW_OK on success, negative on error. */
int braw_mov_read_frame(const char *path, const BrawMovInfo *mov,
                        int frame_idx, uint8_t **packet_out, size_t *size_out);

#endif /* BRAW_DEC_H */
