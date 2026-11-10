#pragma once

#include "playlist.h"
#include "subtitles.h"

#include <QObject>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class History;
class QAudioOutput;
class QMediaPlayer;
class Store;

/// Plays what the list says: one film at a time, with its sound, its
/// subtitles, and the place it was left at.
class Player : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(Playlist *playlist READ playlist CONSTANT)
    /// The VideoOutput the picture goes to.
    Q_PROPERTY(QObject *videoOutput READ videoOutput WRITE setVideoOutput NOTIFY videoOutputChanged)
    Q_PROPERTY(QString path READ path NOTIFY currentChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(bool hasMedia READ hasMedia NOTIFY currentChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY tracksChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    /// 0…1, as the ear hears it.
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(qreal rate READ rate WRITE setRate NOTIFY rateChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY queueChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY queueChanged)
    /// [{index, label}], when the film has more than one sound.
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(int audioTrack READ audioTrack WRITE setAudioTrack NOTIFY tracksChanged)
    /// [{key, label}]: «» for none, «t:<n>» for one inside the film, «f:<path>» for a file.
    Q_PROPERTY(QVariantList subtitleChoices READ subtitleChoices NOTIFY tracksChanged)
    Q_PROPERTY(QString subtitle READ subtitle WRITE setSubtitle NOTIFY subtitleChanged)
    /// What is being said now, when the subtitles come from a file.
    Q_PROPERTY(QString subtitleText READ subtitleText NOTIFY subtitleTextChanged)

public:
    /// `silent`: no sound comes out (test runs).
    Player(Store *store, History *history, bool silent, QObject *parent = nullptr);

    Playlist *playlist() const { return m_playlist; }
    QObject *videoOutput() const { return m_videoOutput; }
    void setVideoOutput(QObject *output);
    QString path() const { return m_path; }
    QString title() const;
    bool hasMedia() const { return !m_path.isEmpty(); }
    bool hasVideo() const;
    bool playing() const;
    qint64 position() const;
    qint64 duration() const;
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    qreal rate() const;
    void setRate(qreal rate);
    bool canNext() const { return m_playlist->next(true) >= 0; }
    bool canPrevious() const { return hasMedia(); }
    QVariantList audioTracks() const { return m_audioTracks; }
    int audioTrack() const;
    void setAudioTrack(int index);
    QVariantList subtitleChoices() const { return m_subtitleChoices; }
    QString subtitle() const { return m_subtitle; }
    void setSubtitle(const QString &key);
    QString subtitleText() const { return m_subtitleText; }

    /// Replaces the list and plays `paths[index]`.
    void open(const QStringList &paths, int index);
    /// Adds to the end of the list; plays the first of them if nothing is on.
    void enqueue(const QStringList &paths);
    /// Notes where the film is and keeps the settings. Before leaving.
    void shutdown();

    Q_INVOKABLE void playAt(int index);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    /// Stops and lets go of the film: the window goes back to its first page.
    Q_INVOKABLE void close();
    Q_INVOKABLE void next();
    /// The start of this one, or the one before when already near its start.
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void step(qint64 ms);
    /// The next choice of subtitles, round and round.
    Q_INVOKABLE void cycleSubtitles();
    Q_INVOKABLE void useSubtitleFile(const QUrl &file);
    /// Saves the picture on screen now among the user's pictures; returns its path, empty if it could not.
    Q_INVOKABLE QString snapshot();

Q_SIGNALS:
    void videoOutputChanged();
    void currentChanged();
    void tracksChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void rateChanged();
    void queueChanged();
    void subtitleChanged();
    void subtitleTextChanged();
    void seeked(qint64 position);
    /// Picked up where it was left.
    void resumed(qint64 position);
    void failed(const QString &message);

private:
    void load(const QString &path);
    void unload();
    void record();
    void refreshTracks();
    void refreshSubtitleText();
    void applyVolume();

    Store *m_store;
    History *m_history;
    Playlist *m_playlist;
    QMediaPlayer *m_media;
    QAudioOutput *m_audio = nullptr;
    QObject *m_videoOutput = nullptr;
    QTimer m_recordTimer;
    QString m_path;
    qint64 m_resumeAt = 0;
    qreal m_volume = 1.0;
    bool m_muted = false;
    QVariantList m_audioTracks;
    QVariantList m_subtitleChoices;
    QStringList m_subtitleFiles;
    QString m_subtitle;
    QString m_subtitleText;
    Subtitles m_subtitles;
};
