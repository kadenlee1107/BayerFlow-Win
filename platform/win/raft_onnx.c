/* raft_onnx.c — RAFT optical flow via ONNX Runtime C API.
 * Replaces the Python subprocess approach for 3-4x speedup.
 * Links against onnxruntime.lib (CUDA execution provider). */

#include "raft_onnx.h"
#include <onnxruntime_c_api.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static const OrtApi *g_ort = NULL;
static OrtEnv *g_env = NULL;
static OrtSession *g_session = NULL;
static OrtSessionOptions *g_opts = NULL;
static OrtMemoryInfo *g_mem_info = NULL;

static int g_width = 0, g_height = 0;
static int g_rw = 0, g_rh = 0;  /* padded to mult of 8 */

/* Check ORT status and print error */
static int ort_check(OrtStatus *status, const char *msg) {
    if (status != NULL) {
        const char *err = g_ort->GetErrorMessage(status);
        fprintf(stderr, "ONNX error (%s): %s\n", msg, err);
        g_ort->ReleaseStatus(status);
        return -1;
    }
    return 0;
}

int raft_onnx_init(const char *model_path, int width, int height) {
    g_ort = OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (!g_ort) { fprintf(stderr, "RAFT ONNX: failed to get API\n"); return -1; }

    if (ort_check(g_ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "bayerflow", &g_env), "CreateEnv")) return -1;
    if (ort_check(g_ort->CreateSessionOptions(&g_opts), "CreateSessionOptions")) return -1;

    /* Enable CUDA */
    OrtCUDAProviderOptionsV2 *cuda_opts = NULL;
    if (ort_check(g_ort->CreateCUDAProviderOptions(&cuda_opts), "CreateCUDAOptions")) {
        fprintf(stderr, "RAFT ONNX: CUDA provider not available, using CPU\n");
    } else {
        if (ort_check(g_ort->SessionOptionsAppendExecutionProvider_CUDA_V2(g_opts, cuda_opts), "AppendCUDA")) {
            fprintf(stderr, "RAFT ONNX: CUDA append failed, using CPU\n");
        }
        g_ort->ReleaseCUDAProviderOptions(cuda_opts);
    }

    g_ort->SetSessionGraphOptimizationLevel(g_opts, ORT_ENABLE_ALL);

    /* Load model — convert path to wide string for Windows */
    size_t pathlen = strlen(model_path) + 1;
    wchar_t *wpath = (wchar_t *)malloc(pathlen * sizeof(wchar_t));
    mbstowcs(wpath, model_path, pathlen);

    if (ort_check(g_ort->CreateSession(g_env, wpath, g_opts, &g_session), "CreateSession")) {
        free(wpath);
        return -1;
    }
    free(wpath);

    if (ort_check(g_ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &g_mem_info), "MemInfo"))
        return -1;

    g_width = width;
    g_height = height;
    /* Pad to multiple of 8 (half res) */
    g_rh = ((height / 2) + 7) & ~7;
    g_rw = ((width / 2) + 7) & ~7;

    fprintf(stderr, "RAFT ONNX: initialized %dx%d (inference at %dx%d)\n", width, height, g_rw, g_rh);
    return 0;
}

void raft_onnx_destroy(void) {
    if (g_mem_info) { g_ort->ReleaseMemoryInfo(g_mem_info); g_mem_info = NULL; }
    if (g_session)  { g_ort->ReleaseSession(g_session);     g_session = NULL; }
    if (g_opts)     { g_ort->ReleaseSessionOptions(g_opts);  g_opts = NULL; }
    if (g_env)      { g_ort->ReleaseEnv(g_env);             g_env = NULL; }
}

/* Resize uint16 green channel to float32 [0,1] at half res, replicate to 3ch */
static float *prepare_input(const uint16_t *green, int gw, int gh, int rw, int rh) {
    size_t out_size = (size_t)3 * rh * rw;
    float *buf = (float *)malloc(out_size * sizeof(float));
    if (!buf) return NULL;

    float scale = 1.0f / 16383.0f;

    /* Simple bilinear downsample 2x + pad */
    for (int y = 0; y < rh; y++) {
        int sy = y * 2;
        if (sy >= gh) sy = gh - 1;
        int sy1 = sy + 1;
        if (sy1 >= gh) sy1 = gh - 1;

        for (int x = 0; x < rw; x++) {
            int sx = x * 2;
            if (sx >= gw) sx = gw - 1;
            int sx1 = sx + 1;
            if (sx1 >= gw) sx1 = gw - 1;

            float v = ((float)green[sy * gw + sx] +
                       (float)green[sy * gw + sx1] +
                       (float)green[sy1 * gw + sx] +
                       (float)green[sy1 * gw + sx1]) * 0.25f * scale;

            /* Replicate to 3 channels (NCHW layout: [1, 3, H, W]) */
            buf[0 * rh * rw + y * rw + x] = v;  /* R */
            buf[1 * rh * rw + y * rw + x] = v;  /* G */
            buf[2 * rh * rw + y * rw + x] = v;  /* B */
        }
    }
    return buf;
}

int raft_onnx_compute(const uint16_t *center, const uint16_t *neighbor,
                       int green_w, int green_h,
                       float *fx_out, float *fy_out)
{
    if (!g_session) return -1;

    int rw = g_rw, rh = g_rh;

    /* Prepare inputs */
    float *inp1 = prepare_input(center, green_w, green_h, rw, rh);
    float *inp2 = prepare_input(neighbor, green_w, green_h, rw, rh);
    if (!inp1 || !inp2) { free(inp1); free(inp2); return -1; }

    /* Create tensors */
    int64_t input_shape[] = {1, 3, rh, rw};
    size_t input_size = (size_t)3 * rh * rw * sizeof(float);

    OrtValue *input_tensors[2] = {NULL, NULL};
    if (ort_check(g_ort->CreateTensorWithDataAsOrtValue(g_mem_info, inp1, input_size,
                  input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensors[0]), "Tensor1"))
        goto fail;
    if (ort_check(g_ort->CreateTensorWithDataAsOrtValue(g_mem_info, inp2, input_size,
                  input_shape, 4, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input_tensors[1]), "Tensor2"))
        goto fail;

    /* Run inference */
    const char *input_names[] = {"image1", "image2"};
    const char *output_names[] = {"flow"};
    OrtValue *output_tensor = NULL;

    if (ort_check(g_ort->Run(g_session, NULL, input_names, (const OrtValue *const *)input_tensors,
                  2, output_names, 1, &output_tensor), "Run"))
        goto fail;

    /* Read output: [1, 2, rh, rw] */
    float *flow_data = NULL;
    if (ort_check(g_ort->GetTensorMutableData(output_tensor, (void **)&flow_data), "GetData"))
        goto fail;

    /* Upscale flow from (rh, rw) to (green_h, green_w) */
    size_t flow_plane = (size_t)rh * rw;
    for (int gy = 0; gy < green_h; gy++) {
        float fy_f = (float)gy * (float)rh / (float)green_h;
        int ry0 = (int)fy_f;
        float fy_frac = fy_f - (float)ry0;
        int ry1 = ry0 + 1;
        if (ry0 >= rh) ry0 = rh - 1;
        if (ry1 >= rh) ry1 = rh - 1;

        for (int gx = 0; gx < green_w; gx++) {
            float fx_f = (float)gx * (float)rw / (float)green_w;
            int rx0 = (int)fx_f;
            float fx_frac = fx_f - (float)rx0;
            int rx1 = rx0 + 1;
            if (rx0 >= rw) rx0 = rw - 1;
            if (rx1 >= rw) rx1 = rw - 1;

            /* Bilinear interpolation of flow_x (channel 0) */
            float f00 = flow_data[0 * flow_plane + ry0 * rw + rx0];
            float f10 = flow_data[0 * flow_plane + ry0 * rw + rx1];
            float f01 = flow_data[0 * flow_plane + ry1 * rw + rx0];
            float f11 = flow_data[0 * flow_plane + ry1 * rw + rx1];
            float vx = (1-fx_frac)*(1-fy_frac)*f00 + fx_frac*(1-fy_frac)*f10 +
                        (1-fx_frac)*fy_frac*f01 + fx_frac*fy_frac*f11;

            /* Scale from half-res pixels to green pixels */
            vx *= (float)green_w / (float)rw;

            /* Bilinear interpolation of flow_y (channel 1) */
            f00 = flow_data[1 * flow_plane + ry0 * rw + rx0];
            f10 = flow_data[1 * flow_plane + ry0 * rw + rx1];
            f01 = flow_data[1 * flow_plane + ry1 * rw + rx0];
            f11 = flow_data[1 * flow_plane + ry1 * rw + rx1];
            float vy = (1-fx_frac)*(1-fy_frac)*f00 + fx_frac*(1-fy_frac)*f10 +
                        (1-fx_frac)*fy_frac*f01 + fx_frac*fy_frac*f11;
            vy *= (float)green_h / (float)rh;

            int gi = gy * green_w + gx;
            fx_out[gi] = vx;
            fy_out[gi] = vy;
        }
    }

    g_ort->ReleaseValue(output_tensor);
    g_ort->ReleaseValue(input_tensors[0]);
    g_ort->ReleaseValue(input_tensors[1]);
    free(inp1);
    free(inp2);
    return 0;

fail:
    if (input_tensors[0]) g_ort->ReleaseValue(input_tensors[0]);
    if (input_tensors[1]) g_ort->ReleaseValue(input_tensors[1]);
    free(inp1);
    free(inp2);
    return -1;
}
