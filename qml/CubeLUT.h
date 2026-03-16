#pragma once
#include <QImage>
#include <QString>
#include <QVector>
#include <QVector3D>

/* .cube LUT loader and applicator (matches Mac CubeLUTLoader + LUTProcessor) */

struct CubeLUT {
    enum Type { OneD, ThreeD };
    Type type = ThreeD;
    int size = 0;
    QVector3D domainMin{0, 0, 0};
    QVector3D domainMax{1, 1, 1};
    QVector<QVector3D> data;  /* flat RGB triplets (R-fastest for 3D) */
    QString title;

    bool isValid() const { return size > 0 && !data.isEmpty(); }

    /* Load from .cube file */
    static CubeLUT load(const QString &path);

    /* Apply to QImage (in-place), with blend factor [0,1] */
    void apply(QImage &image, float blend) const;
};
