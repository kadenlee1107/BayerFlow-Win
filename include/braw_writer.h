/*
 * BRAW MOV Container Writer
 *
 * Writes BRAW-encoded frame packets into a QuickTime MOV container
 * compatible with DaVinci Resolve. Streaming pattern: ftyp→mdat→moov.
 */

#ifndef BRAW_WRITER_H
#define BRAW_WRITER_H

#include <stdint.h>
#include <stdio.h>

typedef struct {
    FILE *fp;
    int width, height;
    int frame_count;
    uint32_t *frame_sizes;
    uint64_t *frame_offsets;
    long mdat_start;
    double fps;
    int timescale;
    int sample_duration;
    char codec_tag[4];        /* e.g., "brhq", "brvm" */

    /* Atoms copied from source BRAW */
    uint8_t *meta_atom;
    int meta_atom_size;
    uint8_t *udta_atom;
    int udta_atom_size;
    uint8_t *stsd_entry;      /* Full sample description entry (includes bver/ctrn/bfdn) */
    int stsd_entry_size;
    uint8_t *tref_atom;       /* Track reference atom (tmcd) */
    int tref_atom_size;
    uint8_t *mdia_hdlr;       /* Media handler atom */
    int mdia_hdlr_size;
    uint8_t *minf_hdlr;       /* Data handler atom */
    int minf_hdlr_size;
} BrawWriter;

/* Open output file. Returns 0 on success, -1 on error. */
int braw_writer_open(BrawWriter *w, const char *filename,
                     int width, int height, double fps);

/* Add one encoded BRAW frame packet. Returns 0 on success, -1 on error. */
int braw_writer_add_frame(BrawWriter *w, const uint8_t *packet, int packet_size);

/* Copy metadata (meta, udta) from source BRAW MOV file. Returns 0 on success. */
int braw_writer_copy_metadata(BrawWriter *w, const char *source_braw);

/* Close and finalize (writes moov atom). Returns 0 on success. */
int braw_writer_close(BrawWriter *w);

#endif /* BRAW_WRITER_H */
