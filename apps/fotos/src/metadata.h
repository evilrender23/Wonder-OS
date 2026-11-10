#pragma once

#include "media.h"

#include <QVariantList>

/// What the files say about themselves: EXIF for pictures (Exiv2), the
/// container for videos (ffprobe).
namespace Metadata
{
/// Fills in the date taken, the size in pixels, the camera and the duration
/// of `item`, whose path, mtime and size are already set.
void fill(MediaItem &item);

/// The rows of the information panel: [{label, value}].
QVariantList details(const MediaItem &item);

/// The EXIF orientation of a picture (1 when it has none).
int orientation(const QString &path);

/// Whether a quarter turn can be written to this file without losing quality.
bool canRotate(const QString &path);
/// Turns a picture a quarter. JPEG and TIFF only get their orientation tag
/// changed; PNG and BMP, which have no loss, are written again turned.
/// The file's date moves one second, so the turn is noticed but the picture
/// stays in its place in the library.
bool rotate(const QString &path, bool clockwise, QString *error = nullptr);

/// 2,4 MB, 812 KB.
QString sizeText(qint64 bytes);
}
