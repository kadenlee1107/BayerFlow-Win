#ifndef PRORES_RAW_ENC_H
#define PRORES_RAW_ENC_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define FRAME_WIDTH  5760
#define FRAME_HEIGHT 3040

#define TILE_WIDTH  256
#define TILE_HEIGHT 16

#define BLOCK_SIZE 8

#define COMP_BUF_SIZE 131072  /* 128KB per component bitstream buffer */

#define DC_CB_MAX 12
extern const int16_t prores_raw_dc_cb[DC_CB_MAX + 1];

#define AC_CB_MAX 94
extern const int16_t prores_raw_ac_cb[AC_CB_MAX + 1];

#define RN_CB_MAX 27
extern const int16_t prores_raw_rn_cb[RN_CB_MAX + 1];

#define LN_CB_MAX 14
extern const int16_t prores_raw_ln_cb[LN_CB_MAX + 1];

extern const uint8_t zigzag_scan[64];
extern const uint8_t default_qmat[64];

/* BitWriter — accumulator-based for word-granularity bit packing.
 * Bits accumulate in a 64-bit register and flush as complete bytes,
 * eliminating per-bit byte masking and the 128KB memset per init. */
typedef struct {
    uint8_t *buffer;
    int buffer_size;
    uint64_t acc;       /* bit accumulator (MSB-first) */
    int acc_bits;       /* number of valid bits in accumulator (0-56) */
    int byte_pos;       /* next byte position to write */
} BitWriter;

void init_bitwriter(BitWriter *bw, uint8_t *buffer, int size);
void put_bits(BitWriter *bw, int n, uint32_t value);
void flush_bitwriter(BitWriter *bw);
int bitwriter_tell(BitWriter *bw);

void put_value(BitWriter *bw, uint32_t value, int16_t codebook);

void init_fwd_matrices(void);
void forward_dct_8x8(int32_t *block, const uint16_t *src, int stride, int component);
void quantize_block(int32_t *block, const int32_t *qmat);
void clamp_quantized_block(int32_t *block, const int32_t *qmat,
                           int32_t input_min, int32_t input_max);

/* Pre-allocated scratch buffers for one tile encoding (one per thread).
 * Eliminates ~33K malloc/free calls per frame. */
typedef struct {
    uint8_t comp_buffers[4][COMP_BUF_SIZE];
    int32_t ac_sequence[16 * 63];   /* max 16 blocks × 63 AC coefficients */
} TileScratch;

int encode_component(BitWriter *bw, const uint16_t *bayer_data,
                     int width, int height, int tile_x, int tile_y,
                     int component, const int32_t *qmat,
                     float noise_sigma, int32_t *ac_buf);

int encode_tile(const uint16_t *bayer_data, int width, int height,
                int tile_x, int tile_y, uint8_t *output,
                const int32_t *qmat, int scale, TileScratch *scratch);

int encode_frame(const uint16_t *bayer_data, int width, int height,
                 uint8_t *output, int output_size);

/* Set frame header template from a source ProRes RAW MOV file.
 * Reads the first video frame's 86-byte header payload and uses it
 * for all subsequent encode_frame() calls. */
int set_frame_header_from_mov(const char *source_mov);

int write_mov_file(const char *filename, const uint8_t *frame_data,
                   int frame_size, int width, int height);

void write_be16(uint8_t *buf, uint16_t val);
void write_be32(uint8_t *buf, uint32_t val);
void write_be64(uint8_t *buf, uint64_t val);

/* Streaming multi-frame MOV writer */
typedef struct {
    FILE *fp;
    int width, height;
    int frame_count;
    uint32_t *frame_sizes;
    uint64_t *frame_offsets;
    long mdat_start;
    /* Metadata atoms copied from source file (included in moov) */
    uint8_t *meta_atom;     /* raw 'meta' atom bytes (including size+tag) */
    int meta_atom_size;
    uint8_t *udta_atom;     /* raw 'udta' atom bytes (including size+tag) */
    int udta_atom_size;
} MovWriter;

int mov_writer_open(MovWriter *w, const char *filename, int width, int height);
int mov_writer_add_frame(MovWriter *w, const uint8_t *frame_data, int frame_size);
int mov_writer_close(MovWriter *w);

/* Copy metadata atoms (meta, udta) from a source MOV into the writer.
 * Call after mov_writer_open() and before mov_writer_close(). */
int mov_writer_copy_metadata(MovWriter *w, const char *source_mov);

#endif
