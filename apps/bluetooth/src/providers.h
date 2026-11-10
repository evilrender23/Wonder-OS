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
