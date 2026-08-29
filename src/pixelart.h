#pragma once

#include <QColor>
#include <QMap>
#include <QPixmap>
#include <QStringList>

// Хелпер для отрисовки пиксель-арта в стиле Minecraft.
// Сетка: список строк одинаковой длины, каждая клетка — символ цвета из палитры.
// Символ '.' — прозрачный пиксель.

QPixmap pixelArt(const QStringList &grid, const QMap<QChar, QColor> &palette, int scale = 8);

// Готовые картинки лаунчера
QPixmap grassBlockPixmap(int scale);   // логотип (травяной блок)
QPixmap steveHeadPixmap(int scale);    // аватар (голова Стива)
QPixmap creeperFacePixmap(int scale);  // пасхалка (лицо крипера)
