#pragma once

#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <exiv2/exiv2.hpp>

/// Pictures made for the tests: small, of one colour with a darker left half
/// (so a turn can be told), and with the EXIF a camera would have written.
namespace Sample
{
inline QString picture(const QString &path, int width = 64, int height = 48, const QColor &color = Qt::red)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            image.setPixelColor(x, y, Qt::black);
        }
    }
    image.save(path);
    return path;
}

/// `when` as the camera writes it: "2024:01:31 10:15:00". Orientation 0: none written.
inline void exif(const QString &path, const QString &when, int orientation = 0, const QString &make = {}, const QString &model = {})
{
    auto image = Exiv2::ImageFactory::open(QFile::encodeName(path).toStdString());
    image->readMetadata();
    Exiv2::ExifData &data = image->exifData();
    if (!when.isEmpty()) {
        data["Exif.Photo.DateTimeOriginal"] = when.toStdString();
    }
    if (orientation > 0) {
        data["Exif.Image.Orientation"] = uint16_t(orientation);
    }
    if (!make.isEmpty()) {
        data["Exif.Image.Make"] = make.toStdString();
    }
    if (!model.isEmpty()) {
        data["Exif.Image.Model"] = model.toStdString();
    }
    image->writeMetadata();
}

/// Sets the date of the file itself.
inline void dated(const QString &path, const QDateTime &when)
{
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(when, QFileDevice::FileModificationTime);
    }
}
}
