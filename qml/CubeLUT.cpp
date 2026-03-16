#include "CubeLUT.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <cmath>

/* ---- .cube file parser (matches Mac CubeLUTLoader) ---- */
CubeLUT CubeLUT::load(const QString &path)
{
    CubeLUT lut;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return lut;

    QTextStream in(&file);
    int size1D = 0, size3D = 0;
    lut.title = QFileInfo(path).completeBaseName();

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        if (line.startsWith("TITLE")) {
            int q1 = line.indexOf('"'), q2 = line.lastIndexOf('"');
            if (q1 >= 0 && q2 > q1) lut.title = line.mid(q1 + 1, q2 - q1 - 1);
            continue;
        }
        if (line.startsWith("LUT_1D_SIZE")) { size1D = line.split(' ').last().toInt(); continue; }
        if (line.startsWith("LUT_3D_SIZE")) { size3D = line.split(' ').last().toInt(); continue; }
        if (line.startsWith("DOMAIN_MIN")) {
            auto p = line.split(' '); if (p.size() >= 4) lut.domainMin = {p[1].toFloat(), p[2].toFloat(), p[3].toFloat()};
            continue;
        }
        if (line.startsWith("DOMAIN_MAX")) {
            auto p = line.split(' '); if (p.size() >= 4) lut.domainMax = {p[1].toFloat(), p[2].toFloat(), p[3].toFloat()};
            continue;
        }

        /* Try parse as "R G B" triplet */
        auto parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            bool ok1, ok2, ok3;
            float r = parts[0].toFloat(&ok1), g = parts[1].toFloat(&ok2), b = parts[2].toFloat(&ok3);
            if (ok1 && ok2 && ok3) lut.data.append({r, g, b});
        }
    }

    if (size3D > 0 && lut.data.size() == size3D * size3D * size3D) {
        lut.type = ThreeD; lut.size = size3D;
    } else if (size1D > 0 && lut.data.size() == size1D) {
        lut.type = OneD; lut.size = size1D;
    }
    return lut;
}

/* ---- 3D trilinear interpolation (matches Mac LUTProcessor) ---- */
static inline QVector3D lerp3(const QVector3D &a, const QVector3D &b, float t) {
    return a + (b - a) * t;
}

void CubeLUT::apply(QImage &image, float blend) const
{
    if (!isValid() || blend < 0.001f) return;
    if (image.format() != QImage::Format_RGB888)
        image = image.convertToFormat(QImage::Format_RGB888);

    int w = image.width(), h = image.height();
    QVector3D invDomain = {
        1.0f / qMax(domainMax.x() - domainMin.x(), 1e-6f),
        1.0f / qMax(domainMax.y() - domainMin.y(), 1e-6f),
        1.0f / qMax(domainMax.z() - domainMin.z(), 1e-6f)
    };
    float sF = (float)(size - 1);
    int s = size;
    blend = qBound(0.0f, blend, 1.0f);

    for (int y = 0; y < h; y++) {
        uchar *line = image.scanLine(y);
        for (int x = 0; x < w; x++) {
            int off = x * 3;
            float origR = line[off]     / 255.0f;
            float origG = line[off + 1] / 255.0f;
            float origB = line[off + 2] / 255.0f;

            if (type == ThreeD) {
                float tr = qBound(0.0f, (origR - domainMin.x()) * invDomain.x(), 1.0f) * sF;
                float tg = qBound(0.0f, (origG - domainMin.y()) * invDomain.y(), 1.0f) * sF;
                float tb = qBound(0.0f, (origB - domainMin.z()) * invDomain.z(), 1.0f) * sF;

                int r0 = qMin((int)tr, s - 2), r1 = r0 + 1; float fr = tr - r0;
                int g0 = qMin((int)tg, s - 2), g1 = g0 + 1; float fg = tg - g0;
                int b0 = qMin((int)tb, s - 2), b1 = b0 + 1; float fb = tb - b0;

                /* 8-corner trilinear (R-fastest) */
                auto c00 = lerp3(data[r0 + g0*s + b0*s*s], data[r1 + g0*s + b0*s*s], fr);
                auto c10 = lerp3(data[r0 + g1*s + b0*s*s], data[r1 + g1*s + b0*s*s], fr);
                auto c01 = lerp3(data[r0 + g0*s + b1*s*s], data[r1 + g0*s + b1*s*s], fr);
                auto c11 = lerp3(data[r0 + g1*s + b1*s*s], data[r1 + g1*s + b1*s*s], fr);
                auto c0 = lerp3(c00, c10, fg);
                auto c1 = lerp3(c01, c11, fg);
                auto lutOut = lerp3(c0, c1, fb);

                origR += (lutOut.x() - origR) * blend;
                origG += (lutOut.y() - origG) * blend;
                origB += (lutOut.z() - origB) * blend;
            } else {
                /* 1D per-channel */
                float tr = qBound(0.0f, (origR - domainMin.x()) * invDomain.x(), 1.0f) * sF;
                float tg = qBound(0.0f, (origG - domainMin.y()) * invDomain.y(), 1.0f) * sF;
                float tb = qBound(0.0f, (origB - domainMin.z()) * invDomain.z(), 1.0f) * sF;
                int ir = qMin((int)tr, s-2); float lutR = data[ir].x() + (data[ir+1].x() - data[ir].x()) * (tr - ir);
                int ig = qMin((int)tg, s-2); float lutG = data[ig].y() + (data[ig+1].y() - data[ig].y()) * (tg - ig);
                int ib = qMin((int)tb, s-2); float lutB = data[ib].z() + (data[ib+1].z() - data[ib].z()) * (tb - ib);
                origR += (lutR - origR) * blend;
                origG += (lutG - origG) * blend;
                origB += (lutB - origB) * blend;
            }

            line[off]     = (uchar)qBound(0.0f, origR * 255.0f, 255.0f);
            line[off + 1] = (uchar)qBound(0.0f, origG * 255.0f, 255.0f);
            line[off + 2] = (uchar)qBound(0.0f, origB * 255.0f, 255.0f);
        }
    }
}
