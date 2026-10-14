#pragma once

#include <QColor>
#include <QFont>
#include <QRectF>

class QPainter;

namespace theme {

// Palette from the dock mockup.
inline const QColor ground{0x14, 0x15, 0x19};
inline const QColor surface{0x1c, 0x1d, 0x22};
inline const QColor surface2{0x24, 0x26, 0x2d};
inline const QColor border{0x2e, 0x30, 0x38};
inline const QColor borderHover{0x3d, 0x40, 0x4b};
inline const QColor textColor{0xec, 0xec, 0xf0};
inline const QColor muted{0xa0, 0xa3, 0xb1};
inline const QColor dim{0x7c, 0x7f, 0x8c};
inline const QColor accent{0x6b, 0x8c, 0xff};
inline const QColor accentText{0x9d, 0xb3, 0xff};
inline const QColor live{0x46, 0xd3, 0x9a};
inline const QColor warn{0xf0, 0xb4, 0x5a};
inline const QColor danger{0xf0, 0x6e, 0x6e};

constexpr int radius = 10;
constexpr int padding = 16;

// Registers the bundled Inter / Inter Display files. Safe to call more than once.
void loadFonts();

enum class Face { Text, Display };
QFont makeFont(int px, int weight, Face face = Face::Text, bool tabular = false);

enum class Icon { Play, External, Check, Sliders, ChevronDown, ChevronLeft, Stop, Warning, Close, Folder, Spinner, Download };
void drawIcon(QPainter &p, Icon icon, const QRectF &rect, const QColor &color);

QColor mix(const QColor &a, const QColor &b, double t);

} // namespace theme
