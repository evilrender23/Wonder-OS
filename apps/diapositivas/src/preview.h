#pragma once

#include <QQuickImageProvider>

/// image://preview/template/<theme> — the opening slide of a theme, for the start screen;
/// image://preview/file/<hex of the path>/<slide> — a slide of a file on disk, for the versions.
class PreviewProvider : public QQuickImageProvider
{
public:
    PreviewProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
