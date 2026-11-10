#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

struct FolderLook;

/// image://icon/[@<rrggbb>/]<name>[|<fallback>…] — a theme icon. Folders are drawn here,
/// so they look the same under any icon theme; `@rrggbb/` is the tint of those with no colour
/// of their own (in the URL, so a new tint is a new picture and not the remembered one).
class IconProvider : public QQuickImageProvider
{
public:
    IconProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
    /// A plain folder wearing a look: the picture kept for programs that can't draw it themselves.
    static QImage folderImage(const FolderLook &look, int extent);
};

/// image://symbol/<name>/<rrggbb|aarrggbb> — a toolbar or sidebar glyph in one colour.
class SymbolProvider : public QQuickImageProvider
{
public:
    SymbolProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
    static QPixmap render(const QString &name, const QColor &color, int extent);
};

/// image://thumb/<path>?<stamp> — a file's preview, from KIO's thumbnailers.
class ThumbProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
