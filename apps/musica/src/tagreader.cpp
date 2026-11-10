#include "tagreader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>
#include <tvariant.h>

namespace
{
QString text(const TagLib::String &string)
{
    return QString::fromUtf8(string.toCString(true)).trimmed();
}

QString property(const TagLib::PropertyMap &map, const char *key)
{
    const TagLib::StringList values = map.value(key);
    return values.isEmpty() ? QString() : text(values.front());
}

/// «3/12» → 3.
int leadingNumber(const QString &value)
{
    return value.section(QLatin1Char('/'), 0, 0).trimmed().toInt();
}
}

QString TagReader::unknownArtist()
{
    return QStringLiteral("Artista desconocido");
}

QString TagReader::unknownAlbum()
{
    return QStringLiteral("Álbum desconocido");
}

bool TagReader::isAudio(const QString &path)
{
    static const QSet<QString> suffixes = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("m4b"), QStringLiteral("aac"),
        QStringLiteral("wav"), QStringLiteral("wma"), QStringLiteral("aiff"), QStringLiteral("aif"),
        QStringLiteral("ape"), QStringLiteral("wv"), QStringLiteral("mpc"), QStringLiteral("mka"),
    };
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

Track TagReader::read(const QString &path)
{
    const QFileInfo info(path);
    Track track;
    track.path = info.absoluteFilePath();
    track.modified = info.lastModified().toSecsSinceEpoch();
    track.size = info.size();

    const TagLib::FileRef file(QFile::encodeName(track.path).constData());
    if (!file.isNull()) {
        if (const TagLib::Tag *tag = file.tag()) {
            track.title = text(tag->title());
            track.artist = text(tag->artist());
            track.album = text(tag->album());
            track.genre = text(tag->genre());
            track.year = int(tag->year());
            track.number = int(tag->track());
        }
        const TagLib::PropertyMap properties = file.properties();
        track.albumArtist = property(properties, "ALBUMARTIST");
        track.disc = leadingNumber(property(properties, "DISCNUMBER"));
        if (const TagLib::AudioProperties *audio = file.audioProperties()) {
            track.durationMs = audio->lengthInMilliseconds();
        }
    }
    if (track.title.isEmpty()) {
        track.title = info.completeBaseName();
    }
    if (track.artist.isEmpty()) {
        track.artist = track.albumArtist.isEmpty() ? unknownArtist() : track.albumArtist;
    }
    if (track.album.isEmpty()) {
        track.album = unknownAlbum();
    }
    return track;
}

QImage TagReader::cover(const QString &path)
{
    const TagLib::FileRef file(QFile::encodeName(path).constData(), false);
    if (!file.isNull()) {
        QImage first;
        const auto pictures = file.complexProperties("PICTURE");
        for (const TagLib::VariantMap &picture : pictures) {
            const TagLib::ByteVector data = picture.value("data").toByteVector();
            const QImage image = QImage::fromData(QByteArrayView(data.data(), qsizetype(data.size())));
            if (image.isNull()) {
                continue;
            }
            if (text(picture.value("pictureType").toString()) == QLatin1String("Front Cover")) {
                return image;
            }
            if (first.isNull()) {
                first = image;
            }
        }
        if (!first.isNull()) {
            return first;
        }
    }

    const QDir folder = QFileInfo(path).absoluteDir();
    const QStringList pictures = folder.entryList(
        {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png"), QStringLiteral("*.webp")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    static const QStringList wanted = {QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"), QStringLiteral("album")};
    for (const QString &stem : wanted) {
        for (const QString &name : pictures) {
            if (QFileInfo(name).completeBaseName().compare(stem, Qt::CaseInsensitive) == 0) {
                const QImage image(folder.filePath(name));
                if (!image.isNull()) {
                    return image;
                }
            }
        }
    }
    return {};
}
