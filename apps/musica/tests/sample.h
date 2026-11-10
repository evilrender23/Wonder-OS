#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

/// A second of silence as a WAV file, with the tags given. Returns its path.
inline QString writeSample(const QString &path, const QString &title = {}, const QString &artist = {}, const QString &album = {},
                           int number = 0, int year = 0, const QString &albumArtist = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const quint32 rate = 8000;
    const quint32 dataSize = rate; // 8-bit mono: one byte per sample.
    QByteArray wav;
    const auto u32 = [&wav](quint32 v) {
        for (int i = 0; i < 4; ++i) {
            wav.append(char((v >> (8 * i)) & 0xff));
        }
    };
    const auto u16 = [&wav](quint16 v) {
        wav.append(char(v & 0xff));
        wav.append(char(v >> 8));
    };
    wav.append("RIFF");
    u32(36 + dataSize);
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(rate);
    u32(rate);
    u16(1);
    u16(8);
    wav.append("data");
    u32(dataSize);
    wav.append(QByteArray(int(dataSize), char(0x80)));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(wav);
    file.close();

    if (!title.isEmpty() || !artist.isEmpty() || !album.isEmpty()) {
        TagLib::FileRef ref(QFile::encodeName(path).constData());
        const auto text = [](const QString &s) {
            return TagLib::String(s.toUtf8().constData(), TagLib::String::UTF8);
        };
        ref.tag()->setTitle(text(title));
        ref.tag()->setArtist(text(artist));
        ref.tag()->setAlbum(text(album));
        ref.tag()->setTrack(unsigned(number));
        ref.tag()->setYear(unsigned(year));
        if (!albumArtist.isEmpty()) {
            TagLib::PropertyMap properties = ref.properties();
            properties.replace("ALBUMARTIST", TagLib::StringList(text(albumArtist)));
            ref.setProperties(properties);
        }
        ref.save();
    }
    return path;
}
