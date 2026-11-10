#pragma once

#include <QQuickImageProvider>

/// image://symbol/<name>/<rrggbb|aarrggbb> — a glyph in one colour.
class SymbolProvider : public QQuickImageProvider
{
public:
    SymbolProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
    static QPixmap render(const QString &name, const QColor &color, int extent);
};

/// image://cover/<corner>/<path> — the cover of an audio file, cropped square
/// with its corners rounded by `corner` percent of its side (50 is a circle).
/// A file without a cover gives a 1×1 picture, which the window reads as «none».
class CoverProvider : public QQuickImageProvider
{
public:
    CoverProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
