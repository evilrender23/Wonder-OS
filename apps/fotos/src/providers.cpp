#include "providers.h"

#include "media.h"
#include "thumbnailer.h"

#include <QAtomicInt>
#include <QFile>
#include <QPainter>
#include <QRunnable>
#include <QSvgRenderer>
#include <QUrl>

static const QString resources = QStringLiteral(":/qt/qml/WonderlandFotos/");

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

namespace
{
class Response : public QQuickImageResponse, public QRunnable
{
public:
    Response(PictureProvider::Kind kind, const QString &path, const QSize &requested)
        : m_kind(kind)
        , m_path(path)
        , m_requested(requested)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        // Scrolled past before its turn came: not worth the reading.
        if (!m_cancelled.loadRelaxed()) {
            if (m_kind == PictureProvider::Kind::Thumbnail) {
                m_image = Thumbnailer::thumbnail(m_path);
            } else {
                const int limit = qMax(m_requested.width(), m_requested.height());
                m_image = Media::isVideo(m_path) ? Thumbnailer::videoFrame(m_path, limit > 0 ? limit : 1920) : Thumbnailer::picture(m_path, limit);
            }
        }
        Q_EMIT finished();
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }
    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("No se pudo leer la imagen.") : QString();
    }
    void cancel() override
    {
        m_cancelled.storeRelaxed(1);
    }

private:
    PictureProvider::Kind m_kind;
    QString m_path;
    QSize m_requested;
    QImage m_image;
    QAtomicInt m_cancelled;
};
}

PictureProvider::PictureProvider(Kind kind)
    : m_kind(kind)
{
    // Enough to fill a screen of thumbnails quickly, few enough to leave the
    // machine to whatever else it is doing.
    m_pool.setMaxThreadCount(kind == Kind::Thumbnail ? qBound(2, QThread::idealThreadCount() / 2, 6) : 2);
}

QQuickImageResponse *PictureProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // <stamp>/<path>, the path percent-encoded as written in the URL.
    const int slash = id.indexOf(QLatin1Char('/'));
    const QString path = QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());
    auto *response = new Response(m_kind, path, requestedSize);
    m_pool.start(response);
    return response;
}
