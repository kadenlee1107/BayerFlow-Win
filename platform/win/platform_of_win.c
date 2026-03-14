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

#ifdef BAYERFLOW_WINDOWS

/* ---- RAFT optical flow via Python/ONNX Runtime ----
 * Calls raft_flow.py which runs RAFT-small on GPU via ONNX Runtime.
 * Communication via temp files (green u16 in, float32 flow out).
 * Quality matches Apple Vision OF; speed ~50-100ms per pair on RTX 5070. */

static int g_of_init_w = 0, g_of_init_h = 0;

int platform_of_init(int width, int height) {
    g_of_init_w = width >> 1;
    g_of_init_h = height >> 1;
    fprintf(stderr, "OF: RAFT (ONNX Runtime GPU) %dx%d\n", g_of_init_w, g_of_init_h);
    return 0;
}

void platform_of_destroy(void) {
    g_of_init_w = g_of_init_h = 0;
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

    /* Write all neighbors and build command */
    char cmd[8192];
    int pos = snprintf(cmd, sizeof(cmd),
        "python \"C:\\Users\\kaden\\BayerFlow-Win\\raft_flow_batch.py\" \"%s\" %d %d",
        center_path, green_w, green_h);

    for (int n = 0; n < num_neighbors; n++) {
        char n_path[256], fx_path[256], fy_path[256];
        snprintf(n_path,  sizeof(n_path),  "%s\\bf_n%d.raw",  tmp, n);
        snprintf(fx_path, sizeof(fx_path), "%s\\bf_fx%d.raw", tmp, n);
        snprintf(fy_path, sizeof(fy_path), "%s\\bf_fy%d.raw", tmp, n);

        FILE *f = fopen(n_path, "wb");
        if (!f) continue;
        fwrite(neighbors[n], sizeof(uint16_t), npix, f);
        fclose(f);

        pos += snprintf(cmd + pos, sizeof(cmd) - pos,
            " \"%s\" \"%s\" \"%s\"", n_path, fx_path, fy_path);
    }

    /* Single Python call for ALL pairs */
    FILE *p = popen(cmd, "r");
    if (p) {
        char line[512];
        while (fgets(line, sizeof(line), p))
            fprintf(stderr, "%s", line);
        pclose(p);
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
