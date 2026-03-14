/* platform_stubs.c — stubs for Mac/Swift-only features not yet on Windows */

#include "denoise_bridge.h"
#include "rgb_temporal_filter.h"
#include "training_data.h"
#include "temporal_filter.h"
#include "r3d_reader.h"
#include <stdio.h>
#include <string.h>

/* ---- ProRes 4444 writer (Mac: Swift/AVFoundation) ---- */
int prores444_writer_open(const char *path, int width, int height, float fps) {
    (void)path; (void)width; (void)height; (void)fps;
    fprintf(stderr, "prores444_writer: not yet implemented on Windows\n");
    return -1;
}
int prores444_writer_write_frame(const uint16_t *rgb_planar) {
    (void)rgb_planar; return -1;
}
void prores444_writer_close(void) {}
int  prores444_writer_available(void) { return 0; }

/* ---- RED R3D SDK (Windows SDK available from RED, not yet linked) ---- */
int r3d_sdk_available(void) { return 0; }

int r3d_reader_open(R3dReader **out, const char *path) {
    (void)out; (void)path; return -1;
}
int r3d_reader_get_info(const R3dReader *r, R3dInfo *info) {
    (void)r; (void)info; return -1;
}
int r3d_reader_read_frame_rgb(R3dReader *r, int frame_idx,
                               uint16_t *rgb_out, int width, int height) {
    (void)r; (void)frame_idx; (void)rgb_out; (void)width; (void)height;
    return -1;
}
void r3d_reader_close(R3dReader *r) { (void)r; }
int  r3d_reader_probe_frame_count(const char *path) { (void)path; return -1; }
int  r3d_reader_probe_dimensions(const char *path, int *w, int *h) {
    (void)path; (void)w; (void)h; return -1;
}

/* ---- RGB temporal filter (CPU implementation — same algo as Bayer) ---- */
void rgb_temporal_filter_init(RgbTemporalFilterConfig *cfg) {
    if (!cfg) return;
    cfg->window_size = 7;
    cfg->strength    = 1.5f;
    cfg->noise_sigma = 0.0f;
}

float rgb_temporal_filter_estimate_noise(const uint16_t *rgb_planar,
                                          int width, int height) {
    (void)rgb_planar; (void)width; (void)height;
    return 1000.0f; /* fallback estimate */
}

void rgb_temporal_filter_frame(
    uint16_t *output, const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    const RgbTemporalFilterConfig *cfg)
{
    /* Fallback: copy center frame unmodified */
    (void)flows_x; (void)flows_y; (void)num_frames; (void)cfg;
    size_t sz = (size_t)width * height * 3 * sizeof(uint16_t);
    memcpy(output, frames[center_idx], sz);
}

/* ---- Legacy NLM GPU path (replaced by VST+Bilateral in tf_mode==2) ---- */
void temporal_filter_frame_gpu(
    uint16_t *output,
    const uint16_t **frames,
    const float **flows_x, const float **flows_y,
    int num_frames, int center_idx,
    int width, int height,
    float strength, float noise_sigma,
    float chroma_boost, float dist_sigma, float flow_tightening,
    const uint16_t *guide, float center_weight)
{
    /* Fallback: copy center frame — callers should use tf_mode==2 instead */
    (void)flows_x; (void)flows_y; (void)num_frames;
    (void)strength; (void)noise_sigma; (void)chroma_boost;
    (void)dist_sigma; (void)flow_tightening; (void)guide; (void)center_weight;
    size_t sz = (size_t)width * height * sizeof(uint16_t);
    memcpy(output, frames[center_idx], sz);
}

/* ---- Training data collector (Mac/research only) ---- */
int  training_data_enabled(void) { return 0; }
void training_data_submit_patch(
    const uint16_t *noisy, const uint16_t *denoised,
    int32_t patch_w, int32_t patch_h,
    int32_t frame_w, int32_t frame_h,
    int32_t patch_x, int32_t patch_y,
    int32_t frame_idx,
    float noise_sigma, float flow_mag,
    const char *camera_model, int32_t iso)
{
    (void)noisy; (void)denoised; (void)patch_w; (void)patch_h;
    (void)frame_w; (void)frame_h; (void)patch_x; (void)patch_y;
    (void)frame_idx; (void)noise_sigma; (void)flow_mag;
    (void)camera_model; (void)iso;
}
