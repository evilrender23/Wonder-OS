#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

/// One audio file and what its tags say.
struct Track {
    QString path;
    QString title;
    QString artist;
    QString album;
    QString albumArtist;
    QString genre;
    int year = 0;
    int number = 0;
    int disc = 0;
    qint64 durationMs = 0;
    /// Seconds since the epoch; with `size`, how a rescan tells an unchanged file.
    qint64 modified = 0;
    qint64 size = 0;

    bool isValid() const { return !path.isEmpty(); }
    /// Who the album belongs to: a compilation keeps together under its album artist.
    QString albumOwner() const { return albumArtist.isEmpty() ? artist : albumArtist; }
    QString albumKey() const { return albumOwner() + QChar(0x1f) + album; }

    QVariantMap toMap() const;
    QJsonObject toJson() const;
    static Track fromJson(const QJsonObject &object);
};

/// Lower case and without accents, so «bjork» finds «Björk».
QString folded(const QString &text);
