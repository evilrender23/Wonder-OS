#pragma once

#include "track.h"

#include <QImage>

namespace TagReader
{
/// Whether the file name is one of the audio formats the library takes in.
bool isAudio(const QString &path);
/// The file's tags. A file without them still gets a title (its name) and the
/// «unknown» artist and album, so it has a place in every view.
Track read(const QString &path);
/// The cover inside the file, else the picture beside it (cover.jpg, folder.png…).
QImage cover(const QString &path);

QString unknownArtist();
QString unknownAlbum();
}
