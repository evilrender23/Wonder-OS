#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// One line of dialogue and when it is on screen, in milliseconds.
struct Cue {
    qint64 start = 0;
    qint64 end = 0;
    QString text;
    bool operator==(const Cue &other) const = default;
};

/// Subtitles kept in a file next to the film: SubRip (.srt), WebVTT (.vtt)
/// and SubStation (.ass, .ssa), as plain text.
class Subtitles
{
public:
    /// Text in whatever the file was saved as: UTF-8, UTF-16 with its mark,
    /// or the Western European of old .srt files.
    static QString decode(const QByteArray &data);
    /// `kind` is the file's ending. Cues come back ordered by their start.
    static QList<Cue> parse(const QString &text, const QString &kind);
    /// The subtitle files that go with a film: same folder, same name,
    /// «Film.srt» first, then «Film.es.srt» and the like.
    static QStringList beside(const QString &videoPath);
    /// What to call one of those in a menu: «Español», «Film.forced.srt»…
    static QString label(const QString &videoPath, const QString &subtitlePath);

    bool load(const QString &path);
    void clear();
    bool isEmpty() const { return m_cues.isEmpty(); }
    int count() const { return int(m_cues.size()); }
    QString path() const { return m_path; }
    /// What is said at this moment; lines that overlap come one under another.
    QString textAt(qint64 ms) const;

private:
    QString m_path;
    QList<Cue> m_cues;
    /// The longest cue: how far back a search has to look.
    qint64 m_longest = 0;
};
