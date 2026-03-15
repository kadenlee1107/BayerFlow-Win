/* platform_of_win.c — NVIDIA Optical Flow SDK 5.x implementation
 *
 * SDK 5 changed to a function-pointer table API loaded from nvofapi64.dll
 * (shipped with the NVIDIA driver — no separate .lib file needed).
 *
 * Entry point: NvOFAPICreateInstanceCuda() populates NV_OF_CUDA_API_FUNCTION_LIST.
 * All subsequent calls go through that function table.
 *
 * Expected performance on RTX 5070 at 2880x1520 (half-res green channel):
 *   ~2-5ms per frame pair → 4 neighbors = ~8-20ms total
 */

#include "platform_of.h"
#include "motion_est.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "raft_onnx.h"

#ifdef BAYERFLOW_WINDOWS

/* ---- RAFT optical flow: ONNX C API (primary) or Python server (fallback) ---- */

static int g_of_init_w = 0, g_of_init_h = 0;
static int g_use_onnx = 0;

int platform_of_init(int width, int height) {
    g_of_init_w = width >> 1;
    g_of_init_h = height >> 1;
    /* Try ONNX C API first — no Python, no file I/O */
    if (raft_onnx_init("C:\\Users\\kaden\\BayerFlow-Win\\raft_small.onnx", g_of_init_w, g_of_init_h) == 0) {
        fprintf(stderr, "OF: RAFT ONNX C API %dx%d\n", g_of_init_w, g_of_init_h);
        g_use_onnx = 1;
    } else {
        fprintf(stderr, "OF: RAFT Python fallback %dx%d\n", g_of_init_w, g_of_init_h);
        g_use_onnx = 0;
    }
    return 0;
}

void platform_of_destroy(void) {
    if (g_use_onnx) raft_onnx_destroy();
    g_of_init_w = g_of_init_h = 0;
    g_use_onnx = 0;
}

int platform_of_compute_batch(
    const uint16_t *center,
    const uint16_t **neighbors,
    int num_neighbors,
    int green_w, int green_h,
    float **fx_out, float **fy_out)
{
    if (g_of_init_w == 0)
        platform_of_init(green_w * 2, green_h * 2);

    size_t npix = (size_t)green_w * green_h;

    /* ONNX C API path — no Python, no file I/O */
    if (g_use_onnx) {
        for (int n = 0; n < num_neighbors; n++) {
            if (raft_onnx_compute(center, neighbors[n], green_w, green_h,
                                  fx_out[n], fy_out[n]) != 0) {
                memset(fx_out[n], 0, npix * sizeof(float));
                memset(fy_out[n], 0, npix * sizeof(float));
            }
        }
        return 0;
    }

    /* Python server fallback */
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = ".";
    char center_path[256];
    snprintf(center_path, sizeof(center_path), "%s\\bf_center.raw", tmp);

    /* Write center */
    {
        FILE *f = fopen(center_path, "wb");
        if (!f) { fprintf(stderr, "RAFT: cannot write center\n"); return -1; }
        fwrite(center, sizeof(uint16_t), npix, f);
        fclose(f);
    }

    /* Write all neighbor files */
    for (int n = 0; n < num_neighbors; n++) {
        char n_path[256];
        snprintf(n_path, sizeof(n_path), "%s\\bf_n%d.raw", tmp, n);
        FILE *f = fopen(n_path, "wb");
        if (!f) continue;
        fwrite(neighbors[n], sizeof(uint16_t), npix, f);
        fclose(f);
    }

    /* Write command file for persistent RAFT server */
    {
        char cmd_path[256];
        snprintf(cmd_path, sizeof(cmd_path), "%s\\bf_raft_cmd.txt", tmp);
        FILE *cf = fopen(cmd_path, "w");
        if (cf) {
            fprintf(cf, "%s %d %d", center_path, green_w, green_h);
            for (int nn = 0; nn < num_neighbors; nn++) {
                char np2[256], fxp[256], fyp[256];
                snprintf(np2, sizeof(np2), "%s\\bf_n%d.raw", tmp, nn);
                snprintf(fxp, sizeof(fxp), "%s\\bf_fx%d.raw", tmp, nn);
                snprintf(fyp, sizeof(fyp), "%s\\bf_fy%d.raw", tmp, nn);
                fprintf(cf, " %s %s %s", np2, fxp, fyp);
            }
            fprintf(cf, "\n");
            fclose(cf);
        }

        /* Trigger server */
        char go_path[256], done_path[256];
        snprintf(go_path, sizeof(go_path), "%s\\bf_raft_go", tmp);
        snprintf(done_path, sizeof(done_path), "%s\\bf_raft_done", tmp);

        remove(done_path);

        FILE *gf = fopen(go_path, "w");
        if (gf) { fprintf(gf, "go"); fclose(gf); }

        /* Wait for done (poll) */
        int timeout_ms = 120000;
        int waited = 0;
        while (waited < timeout_ms) {
            FILE *df = fopen(done_path, "r");
            if (df) { fclose(df); remove(done_path); break; }
            Sleep(10);
            waited += 10;
        }
        if (waited >= timeout_ms)
            fprintf(stderr, "RAFT server timeout!\n");
    }

    /* Read all flow outputs */
    for (int n = 0; n < num_neighbors; n++) {
        char fx_path[256], fy_path[256];
        snprintf(fx_path, sizeof(fx_path), "%s\\bf_fx%d.raw", tmp, n);
        snprintf(fy_path, sizeof(fy_path), "%s\\bf_fy%d.raw", tmp, n);

        FILE *f = fopen(fx_path, "rb");
        if (f) { fread(fx_out[n], sizeof(float), npix, f); fclose(f); }
        else   { memset(fx_out[n], 0, npix * sizeof(float)); }

        f = fopen(fy_path, "rb");
        if (f) { fread(fy_out[n], sizeof(float), npix, f); fclose(f); }
        else   { memset(fy_out[n], 0, npix * sizeof(float)); }
    }

    return 0;
}


#else  /* Non-Windows stub */

int  platform_of_init(int w, int h) { (void)w; (void)h; return 0; }
void platform_of_destroy(void) {}
int  platform_of_compute_batch(
    const uint16_t *c, const uint16_t **nbrs, int nn,
    int gw, int gh, float **fx, float **fy) {
    size_t np = (size_t)gw * gh;
    for (int i = 0; i < nn; i++) {
        (void)nbrs[i];
        memset(fx[i], 0, np * sizeof(float));
        memset(fy[i], 0, np * sizeof(float));
    }
    (void)c; return 0;
}

#endif
