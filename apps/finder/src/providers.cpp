#include "providers.h"

#include "folderlook.h"

#include <KFileItem>
#include <KIO/PreviewJob>

#include <QRegularExpression>

#include <QCoreApplication>
#include <QFile>
#include <QIcon>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QSvgRenderer>
#include <QUrl>

static const QString resources = QStringLiteral(":/qt/qml/WonderlandFinder/");

static int extentOf(const QSize &requested, int fallback)
{
    const int extent = qMax(requested.width(), requested.height());
    return extent > 0 ? qMin(extent, 1024) : fallback;
}

static QPixmap renderSvg(const QByteArray &svg, int extent)
{
    QPixmap pixmap(extent, extent);
    pixmap.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    if (renderer.isValid()) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter, QRectF(0, 0, extent, extent));
    }
    return pixmap;
}

static QByteArray resource(const QString &path)
{
    QFile file(resources + path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

SymbolProvider::SymbolProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap SymbolProvider::render(const QString &name, const QColor &color, int extent)
{
    QByteArray svg = resource(QStringLiteral("symbols/") + name + QStringLiteral(".svg"));
    svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
    QPixmap pixmap = renderSvg(svg, extent);
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

IconProvider::IconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

static QColor mixed(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount, from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

static QPixmap folderPixmap(const QString &name, int extent, const QString &tint = QString())
{
    static const QHash<QString, QString> glyphs = {
        {QStringLiteral("folder-desktop"), QStringLiteral("monitor")},
        {QStringLiteral("folder-documents"), QStringLiteral("file-text")},
        {QStringLiteral("folder-downloads"), QStringLiteral("circle-arrow-down")},
        {QStringLiteral("folder-music"), QStringLiteral("music")},
        {QStringLiteral("folder-pictures"), QStringLiteral("image")},
        {QStringLiteral("folder-movies"), QStringLiteral("film")},
        {QStringLiteral("folder-public"), QStringLiteral("user")},
        {QStringLiteral("folder-applications"), QStringLiteral("layout-grid")},
        {QStringLiteral("folder-home"), QStringLiteral("house")},
    };
    QString base;
    FolderLook look = FolderLook::fromIconName(name, &base);
    if (look.color.isEmpty()) {
        look.color = tint;
    }
    QByteArray svg = resource(QStringLiteral("icons/folder.svg"));
    QColor ink(0x12, 0x5f, 0xb8, 0xb0);
    if (!look.color.isEmpty()) {
        // The artwork's four blues, taken from the chosen colour instead.
        const QColor color(QLatin1Char('#') + look.color);
        const auto shade = [&color](const QColor &towards, qreal amount) {
            return mixed(color, towards, amount).name(QColor::HexRgb).toLatin1();
        };
        svg.replace("#3da5f4", shade(Qt::black, 0.05));
        svg.replace("#2587dd", shade(Qt::black, 0.2));
        svg.replace("#86d3ff", shade(Qt::white, 0.45));
        svg.replace("#3fa4f1", shade(Qt::white, 0.05));
        ink = mixed(color, Qt::black, 0.45);
        ink.setAlpha(0xb0);
    }
    QPixmap pixmap = renderSvg(svg, extent);
    if (extent < 24) {
        return pixmap;
    }
    // Embossed into the front of the folder, as macOS marks its special folders.
    const int side = qRound(extent * 0.34);
    const QRectF place((extent - side) / 2.0, extent * 0.57 - side / 2.0, side, side);
    const QString glyph = look.symbol.isEmpty() ? glyphs.value(base) : look.symbol;
    QPainter painter(&pixmap);
    if (!look.emoji.isEmpty()) {
        QFont font(QStringLiteral("Noto Color Emoji"));
        font.setPixelSize(qMax(1, qRound(side * 0.86)));
        painter.setFont(font);
        painter.setPen(ink);
        painter.drawText(place.adjusted(-side, -side, side, side), Qt::AlignCenter, look.emoji);
    } else if (!glyph.isEmpty()) {
        painter.drawPixmap(place.topLeft(), SymbolProvider::render(glyph, ink, side));
    }
    return pixmap;
}

QImage IconProvider::folderImage(const FolderLook &look, int extent)
{
    static QMutex oneAtATime;
    const QMutexLocker locker(&oneAtATime);
    return folderPixmap(look.iconName(QStringLiteral("folder")), extent).toImage();
}

QPixmap IconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int extent = extentOf(requestedSize, 64);
    // An asynchronous Image asks from the loader's thread while the others ask from
    // the GUI's, and the icon theme loader takes one caller at a time.
    static QMutex oneAtATime;
    const QMutexLocker locker(&oneAtATime);
    QPixmap pixmap;
    // The id arrives as written in the URL: "|" and spaces come percent-encoded.
    QString wanted = QUrl::fromPercentEncoding(id.toUtf8());
    QString tint;
    static const QRegularExpression tinted(QStringLiteral("^@([0-9a-f]{6})/"));
    if (const QRegularExpressionMatch match = tinted.match(wanted); match.hasMatch()) {
        tint = match.captured(1);
        wanted.remove(0, match.capturedLength());
    }
    const QStringList names = wanted.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        if (name == QLatin1String("folder") || name.startsWith(QLatin1String("folder-")) || name.startsWith(QLatin1String("folder~"))) {
            pixmap = folderPixmap(name, extent, tint);
            break;
        }
        const QIcon icon = name.startsWith(QLatin1Char('/')) ? QIcon(name) : QIcon::fromTheme(name);
        if (!icon.isNull()) {
            // `extent` is in pixels already: on a scaled screen the icon would come back
            // larger, and tagged with that scale, which shrinks it when it is painted.
            pixmap = icon.pixmap(QSize(extent, extent), 1.0);
            pixmap.setDevicePixelRatio(1.0);
            if (!pixmap.isNull()) {
                break;
            }
        }
    }
    if (pixmap.isNull()) {
        pixmap = renderSvg(resource(QStringLiteral("icons/document.svg")), extent);
    }
    if (pixmap.width() != extent || pixmap.height() != extent) {
        // Themes hand back the nearest size they have; centre it on the square asked for.
        const QPixmap scaled = pixmap.scaled(extent, extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap square(extent, extent);
        square.fill(Qt::transparent);
        QPainter painter(&square);
        painter.drawPixmap((extent - scaled.width()) / 2, (extent - scaled.height()) / 2, scaled);
        painter.end();
        pixmap = square;
    }
    if (size) {
        *size = pixmap.size();
    }
    return pixmap;
}

class ThumbResponse : public QQuickImageResponse
{
public:
    ThumbResponse(const QString &path, const QSize &size)
    {
        // KIO jobs belong to the GUI thread; image responses are made on the loader's.
        moveToThread(QCoreApplication::instance()->thread());
        QMetaObject::invokeMethod(
            this,
            [this, path, size] {
                start(path, size);
            },
            Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("no preview") : QString();
    }

    void cancel() override
    {
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (m_job) {
                    m_job->kill();
                }
                finish();
            },
            Qt::QueuedConnection);
    }

private:
    void start(const QString &path, const QSize &size)
    {
        if (m_done) {
            return;
        }
        static const QStringList plugins = KIO::PreviewJob::availablePlugins();
        const KFileItem item(QUrl::fromLocalFile(path));
        m_job = KIO::filePreview({item}, size, &plugins);
        m_job->setScaleType(KIO::PreviewJob::Scaled);
        m_job->setIgnoreMaximumSize(true);
        connect(m_job, &KIO::PreviewJob::gotPreview, this, [this](const KFileItem &, const QPixmap &preview) {
            m_image = preview.toImage();
        });
        connect(m_job, &KJob::result, this, &ThumbResponse::finish);
    }

    void finish()
    {
        if (!m_done) {
            m_done = true;
            Q_EMIT finished();
        }
    }

    QPointer<KIO::PreviewJob> m_job;
    QImage m_image;
    bool m_done = false;
};

QQuickImageResponse *ThumbProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const int query = id.lastIndexOf(QLatin1Char('?'));
    const int extent = extentOf(requestedSize, 256);
    return new ThumbResponse(query < 0 ? id : id.left(query), QSize(extent, extent));
}
