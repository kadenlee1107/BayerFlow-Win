#include "BayerConverter.h"

QImage BayerConverter::toQImage(const uint16_t *bayer, int width, int height)
{
    QImage img(width / 2, height / 2, QImage::Format_RGB888);

    for (int y = 0; y < height - 1; y += 2) {
        uchar *line = img.scanLine(y / 2);
        for (int x = 0; x < width - 1; x += 2) {
            /* RGGB Bayer: (y,x)=R, (y,x+1)=Gr, (y+1,x)=Gb, (y+1,x+1)=B */
            uint16_t r  = bayer[y       * width + x];
            uint16_t gr = bayer[y       * width + x + 1];
            uint16_t gb = bayer[(y + 1) * width + x];
            uint16_t b  = bayer[(y + 1) * width + x + 1];
            uint16_t g  = (gr + gb) / 2;

            /* 16-bit to 8-bit with gamma-like curve for better preview */
            int px = (x / 2) * 3;
            line[px + 0] = (uchar)(r >> 8);
            line[px + 1] = (uchar)(g >> 8);
            line[px + 2] = (uchar)(b >> 8);
        }
    }
    return img;
}
