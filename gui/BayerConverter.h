#pragma once
#include <QImage>
#include <cstdint>

class BayerConverter {
public:
    static QImage toQImage(const uint16_t *bayer, int width, int height);
};
