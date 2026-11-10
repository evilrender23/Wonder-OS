#include "providers.h"

#include "tagreader.h"

#include <QCache>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QUrl>

static const QString resources = QStringLiteral(":/qt/qml/WonderlandMusica/");

static int extentOf(const QSize &requested, int fallback)
{
    const int extent = qMax(requested.width(), requested.height());
    return extent > 0 ? qMin(extent, 1024) : fallback;
}

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
    const QPixmap pixmap = render(name, color, extentOf(requestedSize, 32));
    if (size) {
        *size = pixmap.size();
    }
    return pixmap;
}

CoverProvider::CoverProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

/// The picture of an album is read once however many of its tracks ask for it:
/// tracks of one folder share it unless a file carries its own.
static QImage sourceCover(const QString &path)
{
    static QMutex mutex;
    static QCache<QString, QImage> cache(48);
    const QFileInfo info(path);
    const QString key = info.absoluteFilePath() + QLatin1Char('|') + QString::number(info.lastModified().toSecsSinceEpoch());
    {
        const QMutexLocker locker(&mutex);
        if (const QImage *hit = cache.object(key)) {
            return *hit;
        }
    }
    QImage image = TagReader::cover(path);
    if (qMax(image.width(), image.height()) > 1024) {
        image = image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    const QMutexLocker locker(&mutex);
    cache.insert(key, new QImage(image));
    return image;
}

QImage CoverProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // The id arrives as written in the URL, percent-encoded.
    const int slash = id.indexOf(QLatin1Char('/'));
    const int corner = std::clamp(id.left(slash).toInt(), 0, 50);
    const QString path = QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());
    const QImage source = path.isEmpty() ? QImage() : sourceCover(path);
    if (source.isNull()) {
        QImage none(1, 1, QImage::Format_ARGB32_Premultiplied);
        none.fill(Qt::transparent);
        if (size) {
            *size = none.size();
        }
        return none;
    }
    const int extent = extentOf(requestedSize, 512);
    const int side = qMin(source.width(), source.height());
    const QImage square = source.copy((source.width() - side) / 2, (source.height() - side) / 2, side, side)
                              .scaled(extent, extent, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage out(extent, extent, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    if (corner > 0) {
        QPainterPath clip;
        const qreal radius = extent * corner / 100.0;
        clip.addRoundedRect(QRectF(0, 0, extent, extent), radius, radius);
        painter.setClipPath(clip);
    }
    painter.drawImage(0, 0, square);
    painter.end();
    if (size) {
        *size = out.size();
    }
    return out;
}
