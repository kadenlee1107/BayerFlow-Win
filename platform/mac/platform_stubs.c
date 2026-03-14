/* Mac CLI stubs — same as Windows since the CMake build doesn't link Swift */
#include "denoise_bridge.h"
#include <stdio.h>

int prores444_writer_open(const char *path, int width, int height, float fps)
{ (void)path;(void)width;(void)height;(void)fps; return -1; }
int prores444_writer_write_frame(const uint16_t *rgb_planar) { (void)rgb_planar; return -1; }
void prores444_writer_close(void) {}
int prores444_writer_available(void) { return 0; }
int r3d_sdk_available(void) { return 0; }
