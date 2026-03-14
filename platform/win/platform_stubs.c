/* platform_stubs.c — stubs for Mac/Swift-only features not yet on Windows
 *
 * ProRes 4444 writer: on Mac this is implemented in Swift via AVAssetWriter.
 * On Windows, a future implementation could use Media Foundation or ffmpeg.
 *
 * RED R3D SDK: proprietary, Windows SDK available from RED.
 * Stub returns 0 (unavailable) — wire up when SDK is obtained.
 */

#include "denoise_bridge.h"
#include <stdio.h>

/* ---- ProRes 4444 writer (Mac: Swift/AVFoundation) ---- */
int prores444_writer_open(const char *path, int width, int height, float fps) {
    (void)path; (void)width; (void)height; (void)fps;
    fprintf(stderr, "prores444_writer: not yet implemented on Windows\n");
    return -1;
}

int prores444_writer_write_frame(const uint16_t *rgb_planar) {
    (void)rgb_planar;
    return -1;
}

void prores444_writer_close(void) {}

int prores444_writer_available(void) { return 0; }

/* ---- RED R3D SDK ---- */
int r3d_sdk_available(void) { return 0; }
