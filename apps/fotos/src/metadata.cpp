#include "metadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTransform>

#include <exiv2/exiv2.hpp>

namespace Metadata
{

static void quiet()
{
    static const bool once = [] {
        Exiv2::LogMsg::setLevel(Exiv2::LogMsg::mute);
        return true;
    }();
    Q_UNUSED(once)
}

static std::unique_ptr<Exiv2::Image> openImage(const QString &path)
{
    quiet();
    try {
        auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
        if (image) {
            image->readMetadata();
        }
        return image;
    } catch (const std::exception &) {
        return nullptr;
    }
}

static QString text(const Exiv2::ExifData &exif, const char *key)
{
    try {
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end()) {
            return {};
        }
        return QString::fromStdString(it->toString()).trimmed();
    } catch (const std::exception &) {
        return {};
    }
}

static double number(const Exiv2::ExifData &exif, const char *key, int component = 0)
{
    try {
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end() || it->count() <= size_t(component)) {
            return 0;
        }
        return double(it->toFloat(component));
    } catch (const std::exception &) {
        return 0;
    }
}

static QDateTime exifDate(const Exiv2::ExifData &exif)
{
    for (const char *key : {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized", "Exif.Image.DateTime"}) {
        const QDateTime date = QDateTime::fromString(text(exif, key).left(19), QStringLiteral("yyyy:MM:dd HH:mm:ss"));
        if (date.isValid() && date.date().year() > 1970) {
            return date;
        }
    }
    return {};
}

static QString cameraName(const Exiv2::ExifData &exif)
{
    const QString make = text(exif, "Exif.Image.Make");
    const QString model = text(exif, "Exif.Image.Model");
    if (model.isEmpty()) {
        return make;
    }
    // «Canon» + «Canon EOS R6» is said once.
    const QString first = make.section(QLatin1Char(' '), 0, 0);
    return first.isEmpty() || model.startsWith(first, Qt::CaseInsensitive) ? model : make + QLatin1Char(' ') + model;
}

static QString ffTool(const QString &name)
{
    static QHash<QString, QString> found;
    if (!found.contains(name)) {
        found.insert(name, QStandardPaths::findExecutable(name));
    }
    return found.value(name);
}

struct Probe {
    qint64 duration = 0;
    int width = 0;
    int height = 0;
    QDateTime created;
    QString codec;
    double fps = 0;
};

static Probe probe(const QString &path)
{
    Probe out;
    const QString tool = ffTool(QStringLiteral("ffprobe"));
    if (tool.isEmpty()) {
        return out;
    }
    QProcess process;
    process.start(tool, {QStringLiteral("-v"), QStringLiteral("quiet"), QStringLiteral("-print_format"), QStringLiteral("json"), QStringLiteral("-show_format"),
                         QStringLiteral("-show_streams"), QStringLiteral("-select_streams"), QStringLiteral("v:0"), path});
    if (!process.waitForFinished(8000)) {
        process.kill();
        process.waitForFinished(1000);
        return out;
    }
    const QJsonObject root = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    const QJsonObject format = root.value(QLatin1String("format")).toObject();
    const QJsonObject stream = root.value(QLatin1String("streams")).toArray().first().toObject();
    out.duration = qint64(format.value(QLatin1String("duration")).toString().toDouble() * 1000);
    out.width = stream.value(QLatin1String("width")).toInt();
    out.height = stream.value(QLatin1String("height")).toInt();
    out.codec = stream.value(QLatin1String("codec_name")).toString();
    const QStringList rate = stream.value(QLatin1String("avg_frame_rate")).toString().split(QLatin1Char('/'));
    if (rate.size() == 2 && rate.at(1).toDouble() > 0) {
        out.fps = rate.at(0).toDouble() / rate.at(1).toDouble();
    }
    int turn = stream.value(QLatin1String("tags")).toObject().value(QLatin1String("rotate")).toString().toInt();
    for (const QJsonValue &side : stream.value(QLatin1String("side_data_list")).toArray()) {
        if (side.toObject().contains(QLatin1String("rotation"))) {
            turn = side.toObject().value(QLatin1String("rotation")).toInt();
        }
    }
    if (qAbs(turn) % 180 == 90) {
        std::swap(out.width, out.height);
    }
    const QString created = format.value(QLatin1String("tags")).toObject().value(QLatin1String("creation_time")).toString();
    if (!created.isEmpty()) {
        const QDateTime date = QDateTime::fromString(created, Qt::ISODateWithMs);
        // Cameras that never had their clock set write 1904 or 1970.
        if (date.isValid() && date.date().year() > 1990) {
            out.created = date.toLocalTime();
        }
    }
    return out;
}

int orientation(const QString &path)
{
    const auto image = openImage(path);
    if (!image) {
        return 1;
    }
    const int value = int(number(image->exifData(), "Exif.Image.Orientation"));
    return value >= 1 && value <= 8 ? value : 1;
}

void fill(MediaItem &item)
{
    const QString name = QFileInfo(item.path).fileName();
    item.video = Media::isVideo(item.path);
    item.screenshot = !item.video && Media::looksLikeScreenshot(item.path);
    QDateTime taken;
    if (item.video) {
        const Probe video = probe(item.path);
        item.duration = video.duration;
        item.width = video.width;
        item.height = video.height;
        taken = video.created;
    } else {
        if (const auto image = openImage(item.path)) {
            const Exiv2::ExifData &exif = image->exifData();
            taken = exifDate(exif);
            item.camera = cameraName(exif);
            item.width = int(image->pixelWidth());
            item.height = int(image->pixelHeight());
            if (int(number(exif, "Exif.Image.Orientation")) >= 5) {
                std::swap(item.width, item.height);
            }
        }
        if (item.width <= 0 || item.height <= 0) {
            QImageReader reader(item.path);
            reader.setAutoTransform(true);
            QSize size = reader.size();
            if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
                size.transpose();
            }
            item.width = size.width();
            item.height = size.height();
        }
    }
    if (!taken.isValid()) {
        taken = Media::dateFromName(name);
    }
    // A date in the future is a camera with the wrong clock or a number that
    // only looked like a date.
    if (!taken.isValid() || taken.toMSecsSinceEpoch() > item.mtime + 86400000LL * 2) {
        taken = QDateTime::fromMSecsSinceEpoch(item.mtime);
    }
    item.taken = taken.toMSecsSinceEpoch();
}

QString sizeText(qint64 bytes)
{
    const QLocale locale(QLocale::Spanish, QLocale::Spain);
    if (bytes >= 1000LL * 1000 * 1000) {
        return locale.toString(bytes / 1e9, 'f', 2) + QStringLiteral(" GB");
    }
    if (bytes >= 1000 * 1000) {
        return locale.toString(bytes / 1e6, 'f', 1) + QStringLiteral(" MB");
    }
    if (bytes >= 1000) {
        return locale.toString(qRound64(bytes / 1e3)) + QStringLiteral(" KB");
    }
    return locale.toString(bytes) + QStringLiteral(" bytes");
}

static QString coordinate(const Exiv2::ExifData &exif, const char *key, const char *refKey, const QString &positive, const QString &negative)
{
    try {
        const auto it = exif.findKey(Exiv2::ExifKey(key));
        if (it == exif.end() || it->count() < 3) {
            return {};
        }
        const double value = double(it->toFloat(0)) + double(it->toFloat(1)) / 60.0 + double(it->toFloat(2)) / 3600.0;
        const QString ref = text(exif, refKey).toUpper();
        const bool minus = ref.startsWith(QLatin1Char('S')) || ref.startsWith(QLatin1Char('W'));
        return QLocale(QLocale::Spanish, QLocale::Spain).toString(value, 'f', 5) + QStringLiteral("° ") + (minus ? negative : positive);
    } catch (const std::exception &) {
        return {};
    }
}

QVariantList details(const MediaItem &item)
{
    const QLocale locale(QLocale::Spanish, QLocale::Spain);
    QVariantList rows;
    const auto add = [&rows](const QString &label, const QString &value) {
        if (!value.isEmpty()) {
            rows.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}});
        }
    };
    const QFileInfo info(item.path);
    add(QStringLiteral("Fecha"), locale.toString(QDateTime::fromMSecsSinceEpoch(item.taken), QStringLiteral("d 'de' MMMM 'de' yyyy, H:mm")));
    if (item.width > 0 && item.height > 0) {
        QString pixels = QStringLiteral("%1 × %2").arg(item.width).arg(item.height);
        const double mega = double(item.width) * item.height / 1e6;
        if (!item.video && mega >= 0.95) {
            pixels += QStringLiteral(" (%1 MP)").arg(locale.toString(mega, 'f', mega < 10 ? 1 : 0));
        }
        add(QStringLiteral("Dimensiones"), pixels);
    }
    add(QStringLiteral("Tamaño"), sizeText(item.size));
    add(QStringLiteral("Tipo"), info.suffix().toUpper());

    if (item.video) {
        const Probe video = probe(item.path);
        if (video.duration > 0) {
            const qint64 seconds = video.duration / 1000;
            add(QStringLiteral("Duración"), seconds >= 3600 ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                                                            : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
        }
        add(QStringLiteral("Códec"), video.codec.toUpper());
        if (video.fps > 0) {
            add(QStringLiteral("Fotogramas"), locale.toString(video.fps, 'f', qAbs(video.fps - qRound(video.fps)) < 0.01 ? 0 : 2) + QStringLiteral(" fps"));
        }
    } else if (const auto image = openImage(item.path)) {
        const Exiv2::ExifData &exif = image->exifData();
        add(QStringLiteral("Cámara"), cameraName(exif));
        add(QStringLiteral("Objetivo"), text(exif, "Exif.Photo.LensModel"));
        const double focal = number(exif, "Exif.Photo.FocalLength");
        if (focal > 0) {
            add(QStringLiteral("Focal"), locale.toString(focal, 'f', focal < 10 ? 1 : 0) + QStringLiteral(" mm"));
        }
        const double aperture = number(exif, "Exif.Photo.FNumber");
        if (aperture > 0) {
            add(QStringLiteral("Apertura"), QStringLiteral("ƒ/") + locale.toString(aperture, 'f', aperture == qRound(aperture) ? 0 : 1));
        }
        const double exposure = number(exif, "Exif.Photo.ExposureTime");
        if (exposure > 0) {
            add(QStringLiteral("Exposición"), exposure >= 1 ? locale.toString(exposure, 'f', exposure == qRound(exposure) ? 0 : 1) + QStringLiteral(" s")
                                                             : QStringLiteral("1/%1 s").arg(qRound(1.0 / exposure)));
        }
        const int iso = int(number(exif, "Exif.Photo.ISOSpeedRatings"));
        if (iso > 0) {
            add(QStringLiteral("ISO"), QString::number(iso));
        }
        const QString latitude = coordinate(exif, "Exif.GPSInfo.GPSLatitude", "Exif.GPSInfo.GPSLatitudeRef", QStringLiteral("N"), QStringLiteral("S"));
        const QString longitude = coordinate(exif, "Exif.GPSInfo.GPSLongitude", "Exif.GPSInfo.GPSLongitudeRef", QStringLiteral("E"), QStringLiteral("O"));
        if (!latitude.isEmpty() && !longitude.isEmpty()) {
            add(QStringLiteral("Lugar"), latitude + QStringLiteral(", ") + longitude);
        }
    }
    QString folder = info.absolutePath();
    if (folder.startsWith(QDir::homePath())) {
        folder.replace(0, QDir::homePath().size(), QStringLiteral("~"));
    }
    add(QStringLiteral("Carpeta"), folder);
    return rows;
}

static QString suffix(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

static bool byTag(const QString &path)
{
    static const QStringList tagged = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("jpe"), QStringLiteral("tif"), QStringLiteral("tiff")};
    return tagged.contains(suffix(path));
}

static bool byPixels(const QString &path)
{
    static const QStringList lossless = {QStringLiteral("png"), QStringLiteral("bmp")};
    return lossless.contains(suffix(path));
}

bool canRotate(const QString &path)
{
    return (byTag(path) || byPixels(path)) && QFileInfo(path).isWritable();
}

bool rotate(const QString &path, bool clockwise, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    const QFileInfo info(path);
    if (!info.isFile() || !info.isWritable()) {
        return fail(QStringLiteral("No se puede escribir en «%1».").arg(info.fileName()));
    }
    const QDateTime before = info.lastModified();
    if (byTag(path)) {
        quiet();
        try {
            auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
            image->readMetadata();
            Exiv2::ExifData &exif = image->exifData();
            int now = 1;
            const auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Orientation"));
            if (it != exif.end() && it->count() > 0) {
                now = int(it->toInt64());
            }
            exif["Exif.Image.Orientation"] = uint16_t(Media::turned(now, clockwise));
            image->writeMetadata();
        } catch (const std::exception &) {
            return fail(QStringLiteral("No se pudo girar «%1».").arg(info.fileName()));
        }
    } else if (byPixels(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            return fail(QStringLiteral("No se pudo leer «%1».").arg(info.fileName()));
        }
        QImage out = image.transformed(QTransform().rotate(clockwise ? 90 : -90));
        out.setDotsPerMeterX(image.dotsPerMeterY());
        out.setDotsPerMeterY(image.dotsPerMeterX());
        // Next to the original and renamed over it: a failure leaves the picture as it was.
        const QString part = info.absolutePath() + QStringLiteral("/.") + info.fileName() + QStringLiteral(".girando");
        QImageWriter writer(part, suffix(path).toLatin1());
        if (!writer.write(out)) {
            QFile::remove(part);
            return fail(QStringLiteral("No se pudo girar «%1».").arg(info.fileName()));
        }
        QFile::setPermissions(part, info.permissions());
        if (::rename(QFile::encodeName(part).constData(), QFile::encodeName(path).constData()) != 0) {
            QFile::remove(part);
            return fail(QStringLiteral("No se pudo girar «%1».").arg(info.fileName()));
        }
    } else {
        return fail(QStringLiteral("Las imágenes %1 no se pueden girar sin perder calidad.").arg(info.suffix().toUpper()));
    }
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(before.addSecs(1), QFileDevice::FileModificationTime);
    }
    return true;
}

}
