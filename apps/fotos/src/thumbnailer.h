#pragma once

#include <QImage>
#include <QString>

/// Small pictures of the library's pictures and videos, kept on disk so the
/// grid of a library of thousands opens at once.
namespace Thumbnailer
{
/// Where they are kept. Set once, before the first request.
void setCacheDir(const QString &dir);
QString cacheDir();

/// The shortest side of a thumbnail, in pixels.
constexpr int kSide = 360;

/// The thumbnail of a picture or a video: from the cache, or made now and
/// stored. Null when the file cannot be read. Safe from any thread.
QImage thumbnail(const QString &path);

/// A picture as the screen shows it: turned as its camera says and no larger
/// than `limit` on its longest side (0: whole).
QImage picture(const QString &path, int limit = 0);

/// One frame of a video, no larger than `limit` on its longest side (ffmpeg).
QImage videoFrame(const QString &path, int limit);

/// Drops the cached thumbnails nobody has asked for in `days` days.
/// Returns how many files it removed.
int prune(int days = 90);
}
