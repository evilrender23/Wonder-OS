#include "thumbnailer.h"

#include "media.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

namespace Thumbnailer
{

static QMutex s_mutex;
static QString s_dir;

void setCacheDir(const QString &dir)
{
    const QMutexLocker locker(&s_mutex);
    s_dir = dir;
}

QString cacheDir()
{
    const QMutexLocker locker(&s_mutex);
    return s_dir;
}

/// The cached file of a picture as it is now: a changed file gets a new one.
static QString cachePath(const QFileInfo &info)
{
    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        return {};
    }
    const QByteArray key = info.absoluteFilePath().toUtf8() + '|' + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '|' + QByteArray::number(info.size());
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    // Two letters of folder keep any one folder small.
    return dir + QLatin1Char('/') + name.left(2) + QLatin1Char('/') + name.mid(2) + QStringLiteral(".jpg");
}

QImage picture(const QString &path, int limit)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    // By what the file holds, not by its name: a PNG called .jpg still opens.
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    if (limit > 0 && size.isValid() && qMax(size.width(), size.height()) > limit) {
        // Asked of the decoder, a JPEG is read at an eighth of the work.
        reader.setScaledSize(size.scaled(limit, limit, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (!image.isNull() && limit > 0 && qMax(image.width(), image.height()) > limit) {
        image = image.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

static QImage frameAt(const QString &tool, const QString &path, int limit, const QString &seek)
{
    QStringList arguments = {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin")};
    if (!seek.isEmpty()) {
        arguments << QStringLiteral("-ss") << seek;
    }
    arguments << QStringLiteral("-i") << path << QStringLiteral("-frames:v") << QStringLiteral("1") << QStringLiteral("-an") << QStringLiteral("-vf")
              << QStringLiteral("scale=w=%1:h=%1:force_original_aspect_ratio=decrease").arg(limit) << QStringLiteral("-f") << QStringLiteral("image2pipe")
              << QStringLiteral("-vcodec") << QStringLiteral("mjpeg") << QStringLiteral("-q:v") << QStringLiteral("3") << QStringLiteral("-");
    QProcess process;
    process.start(tool, arguments);
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QImage::fromData(process.readAllStandardOutput(), "JPG");
}

QImage videoFrame(const QString &path, int limit)
{
    static const QString tool = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (tool.isEmpty()) {
        return {};
    }
    // A moment in: the very first frame is black more often than not.
    QImage frame = frameAt(tool, path, limit, QStringLiteral("1"));
    if (frame.isNull()) {
        // Shorter than that.
        frame = frameAt(tool, path, limit, QString());
    }
    return frame;
}

QImage thumbnail(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {};
    }
    const QString cached = cachePath(info);
    if (!cached.isEmpty() && QFileInfo::exists(cached)) {
        QImage image(cached);
        if (!image.isNull()) {
            return image;
        }
    }

    QImage image;
    if (Media::isVideo(path)) {
        image = videoFrame(path, kSide * 2);
    } else {
        // The shortest side is what fills a square of the grid; a panorama is
        // not kept wider than three squares.
        QImageReader reader(path);
        reader.setAutoTransform(true);
        reader.setDecideFormatFromContent(true);
        const QSize size = reader.size();
        if (size.isValid() && qMin(size.width(), size.height()) > kSide) {
            QSize target = size.scaled(kSide, kSide, Qt::KeepAspectRatioByExpanding);
            target = target.boundedTo(target.scaled(kSide * 3, kSide * 3, Qt::KeepAspectRatio));
            reader.setScaledSize(target);
        }
        image = reader.read();
    }
    if (image.isNull()) {
        return {};
    }
    if (qMin(image.width(), image.height()) > kSide) {
        image = image.scaled(kSide, kSide, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }
    if (image.hasAlphaChannel()) {
        // JPEG has no transparency: a drawing with it gets the paper it expects.
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter painter(&flat);
        painter.drawImage(0, 0, image);
        painter.end();
        image = flat;
    }
    if (!cached.isEmpty()) {
        QDir().mkpath(QFileInfo(cached).absolutePath());
        QSaveFile file(cached);
        if (file.open(QIODevice::WriteOnly)) {
            image.save(&file, "JPG", 84);
            file.commit();
        }
    }
    return image;
}

int prune(int days)
{
    const QString dir = cacheDir();
    if (dir.isEmpty()) {
        return 0;
    }
    const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
    int removed = 0;
    QDirIterator it(dir, {QStringLiteral("*.jpg")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        // Read moves the access date where the disk keeps one; the date it
        // was written is what is left where it does not.
        if (qMax(info.lastRead(), info.lastModified()) < limit && QFile::remove(info.filePath())) {
            ++removed;
        }
    }
    return removed;
}

}
