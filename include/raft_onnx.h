/* raft_onnx.h — RAFT optical flow via ONNX Runtime C API */
#ifndef RAFT_ONNX_H
#define RAFT_ONNX_H

#include <stdint.h>

/* Initialize RAFT ONNX session. Call once at startup.
 * model_path: path to raft_small.onnx
 * width, height: green channel dimensions */
int raft_onnx_init(const char *model_path, int width, int height);

/* Compute optical flow between center and neighbor green channels.
 * fx_out, fy_out: pre-allocated float arrays of size green_w * green_h */
int raft_onnx_compute(const uint16_t *center, const uint16_t *neighbor,
                       int green_w, int green_h,
                       float *fx_out, float *fy_out);

/* Clean up ONNX session */
void raft_onnx_destroy(void);

#endif
