#include "pixelart.h"

#include <QImage>

namespace {

QImage gridToImage(const QStringList &grid, const QMap<QChar, QColor> &palette) {
    const int h = grid.size();
    const int w = h > 0 ? grid.first().size() : 0;
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        const QString &row = grid.at(y);
        for (int x = 0; x < w && x < row.size(); ++x) {
            const QChar c = row.at(x);
            if (c == QLatin1Char('.'))
                continue; // прозрачный
            img.setPixelColor(x, y, palette.value(c, QColor(Qt::magenta)));
        }
    }
    return img;
}

QPixmap scalePixmap(const QImage &img, int scale) {
    return QPixmap::fromImage(img.scaled(img.width() * scale, img.height() * scale,
                                         Qt::IgnoreAspectRatio, Qt::FastTransformation));
}

} // namespace

QPixmap pixelArt(const QStringList &grid, const QMap<QChar, QColor> &palette, int scale) {
    return scalePixmap(gridToImage(grid, palette), scale);
}

// ---------- Травяной блок (8x8) ----------

QPixmap grassBlockPixmap(int scale) {
    const QStringList grid = {
        QStringLiteral("GLGDGLGD"),
        QStringLiteral("DGLGDGLG"),
        QStringLiteral("GDGLGDGL"),
        QStringLiteral("BKBKBKBK"),
        QStringLiteral("KBKBKBKB"),
        QStringLiteral("BKBKBKBK"),
        QStringLiteral("KBKBKBKB"),
        QStringLiteral("BKBKBKBK"),
    };
    QMap<QChar, QColor> palette;
    palette.insert(QLatin1Char('G'), QColor(0x6a, 0xbe, 0x30));
    palette.insert(QLatin1Char('L'), QColor(0x7b, 0xd0, 0x47));
    palette.insert(QLatin1Char('D'), QColor(0x58, 0xa4, 0x28));
    palette.insert(QLatin1Char('B'), QColor(0x8b, 0x5a, 0x2b));
    palette.insert(QLatin1Char('K'), QColor(0x6e, 0x44, 0x20));
    return pixelArt(grid, palette, scale);
}

// ---------- Голова Стива (8x8) ----------

QPixmap steveHeadPixmap(int scale) {
    const QStringList grid = {
        QStringLiteral("HHHHHHHH"),
        QStringLiteral("HHHHHHHH"),
        QStringLiteral("HSSSSSSH"),
        QStringLiteral("HSESSESH"),
        QStringLiteral("HSSSSSSH"),
        QStringLiteral("HSNNNNSH"),
        QStringLiteral("HSSSSSSH"),
        QStringLiteral("HHHHHHHH"),
    };
    QMap<QChar, QColor> palette;
    palette.insert(QLatin1Char('H'), QColor(0x3b, 0x2a, 0x1f)); // волосы
    palette.insert(QLatin1Char('S'), QColor(0xc6, 0x8e, 0x5f)); // кожа
    palette.insert(QLatin1Char('E'), QColor(0x26, 0x20, 0x1b)); // глаза
    palette.insert(QLatin1Char('N'), QColor(0x8a, 0x5a, 0x35)); // нос
    return pixelArt(grid, palette, scale);
}

// ---------- Лицо крипера (8x8) ----------

QPixmap creeperFacePixmap(int scale) {
    const QStringList grid = {
        QStringLiteral("........"),
        QStringLiteral("..##..##"),
        QStringLiteral("..##..##"),
        QStringLiteral("........"),
        QStringLiteral(".#....#."),
        QStringLiteral("########"),
        QStringLiteral("########"),
        QStringLiteral("........"),
    };
    QMap<QChar, QColor> palette;
    palette.insert(QLatin1Char('#'), QColor(0x55, 0xff, 0x55));
    return pixelArt(grid, palette, scale);
}
