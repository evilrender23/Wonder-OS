#include "media.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>

QString MediaItem::name() const
{
    return QFileInfo(path).fileName();
}

QString MediaItem::folder() const
{
    return QFileInfo(path).absolutePath();
}

QJsonObject MediaItem::toJson() const
{
    QJsonObject object{
        {QLatin1String("path"), path},
        {QLatin1String("taken"), double(taken)},
        {QLatin1String("mtime"), double(mtime)},
        {QLatin1String("size"), double(size)},
        {QLatin1String("width"), width},
        {QLatin1String("height"), height},
    };
    if (video) {
        object.insert(QLatin1String("video"), true);
        object.insert(QLatin1String("duration"), double(duration));
    }
    if (!camera.isEmpty()) {
        object.insert(QLatin1String("camera"), camera);
    }
    if (screenshot) {
        object.insert(QLatin1String("screenshot"), true);
    }
    return object;
}

MediaItem MediaItem::fromJson(const QJsonObject &object)
{
    MediaItem item;
    item.path = object.value(QLatin1String("path")).toString();
    item.video = object.value(QLatin1String("video")).toBool();
    item.taken = qint64(object.value(QLatin1String("taken")).toDouble());
    item.mtime = qint64(object.value(QLatin1String("mtime")).toDouble());
    item.size = qint64(object.value(QLatin1String("size")).toDouble());
    item.width = object.value(QLatin1String("width")).toInt();
    item.height = object.value(QLatin1String("height")).toInt();
    item.duration = qint64(object.value(QLatin1String("duration")).toDouble());
    item.camera = object.value(QLatin1String("camera")).toString();
    item.screenshot = object.value(QLatin1String("screenshot")).toBool();
    return item;
}

QVariantMap MediaItem::toMap() const
{
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("name"), name()},
        {QStringLiteral("video"), video},
        {QStringLiteral("animated"), Media::isAnimated(path)},
        {QStringLiteral("taken"), taken},
        {QStringLiteral("mtime"), mtime},
        {QStringLiteral("size"), size},
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("duration"), duration},
    };
}

namespace Media
{

static QString suffixOf(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash + 1 ? path.mid(dot + 1).toLower() : QString();
}

/// The pictures this desktop can show: what is a photo by its name, and that
/// Qt (with the image plugins installed) can read.
static const QSet<QString> &imageSuffixes()
{
    static const QSet<QString> suffixes = [] {
        // name on disk → the format name Qt gives its reader.
        const QList<QPair<QString, QString>> wanted = {
            {QStringLiteral("jpg"), QStringLiteral("jpg")},   {QStringLiteral("jpeg"), QStringLiteral("jpeg")}, {QStringLiteral("jpe"), QStringLiteral("jpeg")},
            {QStringLiteral("png"), QStringLiteral("png")},   {QStringLiteral("gif"), QStringLiteral("gif")},   {QStringLiteral("webp"), QStringLiteral("webp")},
            {QStringLiteral("bmp"), QStringLiteral("bmp")},   {QStringLiteral("tif"), QStringLiteral("tif")},   {QStringLiteral("tiff"), QStringLiteral("tiff")},
            {QStringLiteral("heic"), QStringLiteral("heic")}, {QStringLiteral("heif"), QStringLiteral("heif")}, {QStringLiteral("avif"), QStringLiteral("avif")},
            {QStringLiteral("jxl"), QStringLiteral("jxl")},   {QStringLiteral("dng"), QStringLiteral("dng")},   {QStringLiteral("cr2"), QStringLiteral("cr2")},
            {QStringLiteral("cr3"), QStringLiteral("cr3")},   {QStringLiteral("nef"), QStringLiteral("nef")},   {QStringLiteral("arw"), QStringLiteral("arw")},
            {QStringLiteral("orf"), QStringLiteral("orf")},   {QStringLiteral("rw2"), QStringLiteral("rw2")},   {QStringLiteral("raf"), QStringLiteral("raf")},
        };
        QSet<QString> readable;
        for (const QByteArray &format : QImageReader::supportedImageFormats()) {
            readable.insert(QString::fromLatin1(format).toLower());
        }
        QSet<QString> out;
        for (const auto &[suffix, format] : wanted) {
            if (readable.contains(format)) {
                out.insert(suffix);
            }
        }
        return out;
    }();
    return suffixes;
}

bool isImage(const QString &path)
{
    return imageSuffixes().contains(suffixOf(path));
}

bool isVideo(const QString &path)
{
    static const QSet<QString> suffixes = {
        QStringLiteral("mp4"), QStringLiteral("m4v"),  QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("wmv"), QStringLiteral("flv"), QStringLiteral("3gp"),  QStringLiteral("ogv"),
        QStringLiteral("mts"), QStringLiteral("m2ts"),
    };
    return suffixes.contains(suffixOf(path));
}

bool isAnimated(const QString &path)
{
    return suffixOf(path) == QLatin1String("gif");
}

QDateTime dateFromName(const QString &name)
{
    // A date, then perhaps a time, with or without separators. The digits must
    // not be part of a longer number (a size, a counter).
    static const QRegularExpression pattern(QStringLiteral("(?<!\\d)((?:19|20)\\d{2})[-_.]?(\\d{2})[-_.]?(\\d{2})(?:[-_ .T]?(\\d{2})[-_.:h]?(\\d{2})[-_.:m]?(\\d{2})(?!\\d))?"));
    auto matches = pattern.globalMatch(name);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        if (!date.isValid()) {
            continue;
        }
        QTime time(12, 0);
        if (!match.captured(4).isEmpty()) {
            const QTime written(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt());
            if (written.isValid()) {
                time = written;
            }
        } else if (match.capturedEnd(3) < name.size() && name.at(match.capturedEnd(3)).isDigit()) {
            // Eight digits at the head of a longer number are not a date.
            continue;
        }
        return QDateTime(date, time);
    }
    return {};
}

bool looksLikeScreenshot(const QString &path)
{
    static const QStringList words = {
        QStringLiteral("screenshot"), QStringLiteral("captura"), QStringLiteral("screen shot"), QStringLiteral("screen_shot"),
        QStringLiteral("pantallazo"), QStringLiteral("grim"),    QStringLiteral("spectacle"),
    };
    const QFileInfo info(path);
    const QString name = info.fileName().toLower();
    const QString folder = info.dir().dirName().toLower();
    for (const QString &word : words) {
        if (name.startsWith(word) || folder.startsWith(word)) {
            return true;
        }
    }
    return folder == QLatin1String("screenshots") || folder == QLatin1String("capturas de pantalla");
}

int turned(int orientation, bool clockwise)
{
    // Index: the orientation now. Mirrored ones turn among themselves.
    static const int right[9] = {6, 6, 7, 8, 5, 2, 3, 4, 1};
    static const int left[9] = {8, 8, 5, 6, 7, 4, 1, 2, 3};
    const int from = orientation >= 1 && orientation <= 8 ? orientation : 1;
    return clockwise ? right[from] : left[from];
}

QString fold(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}

}
