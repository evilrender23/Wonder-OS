#pragma once

#include <QQuickImageProvider>

/// image://template/<id> — the first sheet of a template, small.
class TemplateProvider : public QQuickImageProvider
{
public:
    TemplateProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
