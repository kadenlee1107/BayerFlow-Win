/* BayerFlow CLI — Windows / Linux entry point
 * Mirrors the --headless interface of the Mac BayerFlow app.
 *
 * Usage:
 *   bayerflow --input <file> --output <file> [options]
 *
 * Options:
 *   --input  <path>     Input file (ProRes RAW .mov, BRAW, CineForm, ARRIRAW, ZRAW, DNG folder)
 *   --output <path>     Output file (.mov = ProRes RAW, .mov BRAW, .dng = DNG sequence)
 *   --frames <N>        Process only first N frames (0 = all)
 *   --start  <N>        Start frame (0-based, default 0)
 *   --end    <N>        End frame exclusive (0 = all)
 *   --window <N>        Temporal window size (default 15)
 *   --strength <F>      Filter strength (default 1.5)
 *   --spatial <F>       Spatial denoise strength (default 0 = off)
 *   --tf-mode <N>       0 = NLM (default), 2 = VST+Bilateral (GPU when available)
 *   --dark-frame <path> Dark frame .mov for hot pixel subtraction
 *   --hotpixel <path>   Hot pixel profile .bin
 *   --iso <N>           Sensor ISO (used to select hot pixel profile)
 *   --output-format <N> 0 = auto, 1 = MOV, 2 = DNG, 3 = BRAW, 4 = EXR
 *   --black-level <F>   Sensor black level in 16-bit ADU (default 6032)
 *   --shot-gain <F>     Shot noise gain (default 180)
 *   --read-noise <F>    Read noise floor in 16-bit ADU (default 616)
 *   --protect-subjects  Enable subject/person protection (requires CNN)
 *   --cnn               Enable CNN post-filter (currently unsupported on Windows)
 */

#include "denoise_bridge.h"
#include "platform_gpu.h"
#include "platform_of.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "BayerFlow CLI — RAW video denoiser\n"
        "Usage: %s --input <file> --output <file> [options]\n"
        "\n"
        "  --input  <path>      Input video (ProRes RAW, BRAW, CineForm, ARRIRAW, ZRAW, DNG)\n"
        "  --output <path>      Output video\n"
        "  --frames <N>         Process only first N frames (0 = all)\n"
        "  --start  <N>         Start frame (0-based)\n"
        "  --end    <N>         End frame exclusive\n"
        "  --window <N>         Temporal window size (default 15)\n"
        "  --strength <F>       Filter strength (default 1.5)\n"
        "  --spatial <F>        Spatial strength (default 0 = off)\n"
        "  --tf-mode <N>        0=NLM, 2=VST+Bilateral GPU (default 2)\n"
        "  --dark-frame <path>  Dark frame .mov\n"
        "  --hotpixel <path>    Hot pixel profile .bin\n"
        "  --iso <N>            Sensor ISO\n"
        "  --output-format <N>  0=auto 1=MOV 2=DNG 3=BRAW 4=EXR\n"
        "  --black-level <F>    Black level 16-bit ADU (default 6032)\n"
        "  --shot-gain <F>      Shot noise gain (default 180)\n"
        "  --read-noise <F>     Read noise floor (default 616)\n"
        "\n", prog);
}

static int progress_cb(int current, int total, void *ctx) {
    (void)ctx;
    if (total > 0) {
        int pct = (int)((current * 100LL) / total);
        fprintf(stderr, "\r  Frame %d / %d  [%d%%]", current, total, pct);
        fflush(stderr);
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *input_path  = NULL;
    const char *output_path = NULL;
    const char *dark_frame  = NULL;
    const char *hotpixel    = NULL;

    DenoiseCConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.window_size         = 15;
    cfg.strength            = 1.5f;
    cfg.noise_sigma         = 0.0f;   /* auto */
    cfg.spatial_strength    = 0.0f;
    cfg.temporal_filter_mode = 2;     /* VST+Bilateral by default */
    cfg.auto_dark_frame     = 1;
    cfg.output_format       = 0;      /* auto */
    cfg.black_level         = 6032.0f;
    cfg.shot_gain           = 180.0f;
    cfg.read_noise          = 616.0f;

    int frames  = 0;   /* 0 = all */

    for (int i = 1; i < argc; i++) {
#define ARG(name) (strcmp(argv[i], name) == 0)
#define NEXT (++i < argc ? argv[i] : (fprintf(stderr,"Missing value for %s\n",argv[i-1]),exit(1),(char*)0))
        if      (ARG("--input"))         input_path  = NEXT;
        else if (ARG("--output"))        output_path = NEXT;
        else if (ARG("--frames"))        frames      = atoi(NEXT);
        else if (ARG("--start"))         cfg.start_frame = atoi(NEXT);
        else if (ARG("--end"))           cfg.end_frame   = atoi(NEXT);
        else if (ARG("--window"))        cfg.window_size  = atoi(NEXT);
        else if (ARG("--strength"))      cfg.strength     = (float)atof(NEXT);
        else if (ARG("--spatial"))       cfg.spatial_strength = (float)atof(NEXT);
        else if (ARG("--tf-mode"))       cfg.temporal_filter_mode = atoi(NEXT);
        else if (ARG("--dark-frame"))    dark_frame  = NEXT;
        else if (ARG("--hotpixel"))      hotpixel    = NEXT;
        else if (ARG("--iso"))           cfg.detected_iso = atoi(NEXT);
        else if (ARG("--output-format")) cfg.output_format = atoi(NEXT);
        else if (ARG("--black-level"))   cfg.black_level  = (float)atof(NEXT);
        else if (ARG("--shot-gain"))     cfg.shot_gain    = (float)atof(NEXT);
        else if (ARG("--read-noise"))    cfg.read_noise   = (float)atof(NEXT);
        else if (ARG("--protect-subjects")) cfg.protect_subjects = 1;
        else if (ARG("--cnn"))           cfg.use_cnn_postfilter = 1;
        else if (ARG("--help") || ARG("-h")) { print_usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown argument: %s\n", argv[i]); print_usage(argv[0]); return 1; }
#undef ARG
#undef NEXT
    }

    if (!input_path || !output_path) {
        print_usage(argv[0]);
        return 1;
    }

    cfg.dark_frame_path      = dark_frame;
    cfg.hotpixel_profile_path = hotpixel;

    /* Apply --frames as end_frame shorthand */
    if (frames > 0 && cfg.end_frame == 0)
        cfg.end_frame = cfg.start_frame + frames;

    /* Print startup banner */
    fprintf(stderr, "=== BayerFlow CLI ===\n");
    fprintf(stderr, "  Input:    %s\n", input_path);
    fprintf(stderr, "  Output:   %s\n", output_path);
    if (cfg.end_frame > cfg.start_frame)
        fprintf(stderr, "  Frames:   %d-%d (%d frames)\n",
                cfg.start_frame, cfg.end_frame, cfg.end_frame - cfg.start_frame);
    else
        fprintf(stderr, "  Frames:   all\n");
    fprintf(stderr, "  Window:   %d\n", cfg.window_size);
    fprintf(stderr, "  Strength: %.1f\n", cfg.strength);
    fprintf(stderr, "  TF Mode:  %d (%s)\n", cfg.temporal_filter_mode,
            cfg.temporal_filter_mode == 2 ? "VST+Bilateral" : "NLM");
    fprintf(stderr, "  GPU:      %s\n", platform_gpu_available() ? "CUDA" : "CPU");
    fprintf(stderr, "--------------------\n");

    /* Probe input */
    int width = 0, height = 0;
    if (denoise_probe_dimensions(input_path, &width, &height) == 0)
        fprintf(stderr, "  Dimensions: %dx%d\n", width, height);

    int total = denoise_probe_frame_count(input_path);
    if (total > 0) fprintf(stderr, "  Frame count: %d\n", total);

    /* Init GPU ring (platform layer) */
    if (width > 0 && height > 0)
        platform_gpu_ring_init(cfg.window_size, width, height);

    /* Run */
    fprintf(stderr, "\n");
    time_t t0 = time(NULL);

    int ret = denoise_file(input_path, output_path, &cfg, progress_cb, NULL);

    double elapsed = difftime(time(NULL), t0);
    fprintf(stderr, "\n");

    if (ret == DENOISE_OK) {
        fprintf(stderr, "\nDONE in %.0fs → %s\n", elapsed, output_path);
        return 0;
    } else {
        fprintf(stderr, "\nFAILED with code %d after %.0fs\n", ret, elapsed);
        return 1;
    }
}
