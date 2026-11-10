#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/// One picture or one video of the library, as read from the disk.
struct MediaItem {
    QString path;
    bool video = false;
    /// When it was taken, in milliseconds since the epoch: the camera's date,
    /// else the date written in the file's name, else the file's own date.
    qint64 taken = 0;
    /// The file's date (milliseconds) and size: what tells a changed file apart.
    qint64 mtime = 0;
    qint64 size = 0;
    /// As shown, that is, after the camera's rotation.
    int width = 0;
    int height = 0;
    /// Videos: milliseconds.
    qint64 duration = 0;
    QString camera;
    bool screenshot = false;

    QString name() const;
    QString folder() const;
    QJsonObject toJson() const;
    static MediaItem fromJson(const QJsonObject &object);
    /// What the window reads of it.
    QVariantMap toMap() const;
};

namespace Media
{
bool isImage(const QString &path);
bool isVideo(const QString &path);
inline bool isMedia(const QString &path)
{
    return isImage(path) || isVideo(path);
}
bool isAnimated(const QString &path);

/// The date many cameras, phones and screenshot tools write in the name:
/// IMG_20240131_101500.jpg, 2024-01-31 10.15.00.png, Screenshot_20240131-101500.png,
/// IMG-20240131-WA0007.jpg, VID20240131101500.mp4. Invalid when the name has none.
QDateTime dateFromName(const QString &name);
/// A capture of the screen, going by its name or the folder it is in.
bool looksLikeScreenshot(const QString &path);

/// The EXIF orientation (1…8) a picture has after a quarter turn.
int turned(int orientation, bool clockwise);

/// Lower case and without accents: «Cumpleaños» matches «cumpleanos».
QString fold(const QString &text);
}
