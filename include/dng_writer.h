#ifndef DNG_WRITER_H
#define DNG_WRITER_H

#include <stdint.h>

/* Opaque DNG writer handle.
 * Captures ALL IFD metadata from a template DNG, then replays it
 * for each output frame with only image-data tags patched. */
typedef struct DngWriter DngWriter;

/* Open a DNG writer using the first source DNG as a metadata template.
 * Captures all IFD entries (IFD0 + SubIFD) including external data blobs.
 * Returns an allocated DngWriter, or NULL on error. */
DngWriter *dng_writer_open(const char *template_dng_path);

/* Write one denoised frame as a DNG file.
 * bayer_data: uint16 Bayer buffer in RGGB layout (pipeline 16-bit).
 * width, height: frame dimensions.
 * output_path: full path to output .dng file.
 * source_bits_per_sample: original bit depth (12, 14, or 16) for BlackLevel scaling.
 * source_cfa_pattern: DNG_CFA_* code (to undo RGGB remap before writing).
 * Returns 0 on success, -1 on error. */
int dng_writer_write_frame(DngWriter *w, const uint16_t *bayer_data,
                           int width, int height,
                           const char *output_path,
                           int source_bits_per_sample,
                           int source_cfa_pattern);

/* Free all captured IFD data. */
void dng_writer_close(DngWriter *w);

#endif /* DNG_WRITER_H */
