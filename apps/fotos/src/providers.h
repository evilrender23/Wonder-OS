#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>
#include <QThreadPool>

/// image://symbol/<name>/<rrggbb|aarrggbb> — a glyph in one colour.
class SymbolProvider : public QQuickImageProvider
{
public:
    SymbolProvider();
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
    static QPixmap render(const QString &name, const QColor &color, int extent);
};

/// image://thumb/<stamp>/<path> — the small picture of a picture or video.
/// image://photo/<stamp>/<path> — the picture itself, turned as its camera
/// says and no larger than the size asked for.
/// `stamp` is the file's date: a changed file is a new address, so nothing
/// stale is drawn. Both are read off the window's thread.
class PictureProvider : public QQuickAsyncImageProvider
{
public:
    enum class Kind { Thumbnail, Photo };
    explicit PictureProvider(Kind kind);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    Kind m_kind;
    QThreadPool m_pool;
};
