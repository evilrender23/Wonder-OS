#include "thumbnails.h"

#include "editor.h"

#include <QPainter>

TemplateProvider::TemplateProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TemplateProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Editor editor;
    editor.newDocument(id);
    const double width = requestedSize.width() > 0 ? requestedSize.width() : 240;
    const double scale = width / editor.pageWidth();
    QImage sheet(QSize(qRound(width), qRound(editor.pageHeight() * scale)), QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::white);
    {
        QPainter painter(&sheet);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.scale(scale, scale);
        editor.paintPage(&painter, 0, false);
    }
    if (size) {
        *size = sheet.size();
    }
    return sheet;
}
