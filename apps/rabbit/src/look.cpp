#include "look.h"

#include <QMimeDatabase>
#include <QMimeType>
#include <QSet>

QString Look::kindFor(const QString &mime, bool isDir)
{
    if (isDir) {
        return QStringLiteral("folder");
    }
    if (mime == QLatin1String("application/pdf")) {
        return QStringLiteral("pdf");
    }
    if (mime == QLatin1String("image/gif") || mime == QLatin1String("image/apng")) {
        return QStringLiteral("animation");
    }
    if (mime.startsWith(QLatin1String("image/"))) {
        // What Qt has no reader for (camera raw, Photoshop, DjVu) is shown as its thumbnail.
        static const QSet<QString> drawn = {
            QStringLiteral("image/png"),  QStringLiteral("image/jpeg"),    QStringLiteral("image/webp"), QStringLiteral("image/svg+xml"),
            QStringLiteral("image/bmp"),  QStringLiteral("image/x-icon"),  QStringLiteral("image/avif"), QStringLiteral("image/svg+xml-compressed"),
            QStringLiteral("image/tiff"), QStringLiteral("image/vnd.microsoft.icon"), QStringLiteral("image/heif"), QStringLiteral("image/jxl"),
            QStringLiteral("image/x-portable-pixmap"), QStringLiteral("image/x-portable-bitmap"), QStringLiteral("image/x-portable-graymap"),
            QStringLiteral("image/x-xpixmap"), QStringLiteral("image/x-xbitmap"), QStringLiteral("image/x-tga"),
        };
        return drawn.contains(mime) ? QStringLiteral("image") : QStringLiteral("other");
    }
    if (mime.startsWith(QLatin1String("audio/"))) {
        return QStringLiteral("audio");
    }
    if (mime.startsWith(QLatin1String("video/"))) {
        return QStringLiteral("video");
    }
    if (mime.startsWith(QLatin1String("text/"))) {
        return QStringLiteral("text");
    }
    static const QSet<QString> words = {
        QStringLiteral("application/json"),   QStringLiteral("application/xml"),       QStringLiteral("application/toml"),
        QStringLiteral("application/x-yaml"), QStringLiteral("application/yaml"),      QStringLiteral("application/x-desktop"),
        QStringLiteral("application/sql"),    QStringLiteral("application/javascript"), QStringLiteral("application/x-shellscript"),
    };
    if (words.contains(mime)) {
        return QStringLiteral("text");
    }
    // Source code and scripts of every other language say they are plain text underneath.
    const QMimeType type = QMimeDatabase().mimeTypeForName(mime);
    if (type.isValid() && type.inherits(QStringLiteral("text/plain"))) {
        return QStringLiteral("text");
    }
    return QStringLiteral("other");
}
