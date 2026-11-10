#include "providers.h"

#include <QFile>
#include <QPainter>
#include <QSvgRenderer>

static const QString resources = QStringLiteral(":/qt/qml/WonderlandPeliculas/");

SymbolProvider::SymbolProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap SymbolProvider::render(const QString &name, const QColor &color, int extent)
{
    QByteArray svg;
    QFile file(resources + QStringLiteral("symbols/") + name + QStringLiteral(".svg"));
    if (file.open(QIODevice::ReadOnly)) {
        svg = file.readAll();
    }
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(0, 0, extent, extent));
    }
    if (color.alpha() < 255) {
        // The stroke colour carries no alpha in SVG 1.1 Tiny: fade the result instead.
        QPixmap faded(pixmap.size());
        faded.fill(Qt::transparent);
        QPainter painter(&faded);
        painter.setOpacity(color.alphaF());
        painter.drawPixmap(0, 0, pixmap);
        painter.end();
        pixmap = faded;
    }
    return pixmap;
}

QPixmap SymbolProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int slash = id.lastIndexOf(QLatin1Char('/'));
    const QString name = slash < 0 ? id : id.left(slash);
    QColor color(QLatin1Char('#') + id.mid(slash + 1));
    if (slash < 0 || !color.isValid()) {
        color = Qt::black;
    }
    const int asked = qMax(requestedSize.width(), requestedSize.height());
    const QPixmap pixmap = render(name, color, asked > 0 ? qMin(asked, 1024) : 32);
    if (size) {
        *size = pixmap.size();
    }
    return pixmap;
}
