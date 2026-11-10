#pragma once

#include "queue.h"
#include "track.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class Library;
class QAudioOutput;
class QMediaPlayer;
class Store;

/// Plays the queue. The window and the desktop's media keys (MPRIS) both talk to this.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.player")
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)
    /// 0 off, 1 the whole queue, 2 the current track.
    Q_PROPERTY(int repeat READ repeat WRITE setRepeat NOTIFY modeChanged)
    Q_PROPERTY(QVariantList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int queuePosition READ queuePosition NOTIFY queueChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY queueChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY queueChanged)

public:
    /// `silent`: no audio device is opened (pictures of the window, for tests).
    Player(Library *library, Store *store, const QString &cacheDir, bool silent, QObject *parent = nullptr);
    ~Player() override;

    bool playing() const { return m_playing; }
    bool hasTrack() const { return m_current.isValid(); }
    QVariantMap current() const { return m_current.toMap(); }
    const Track &track() const { return m_current; }
    QString currentPath() const { return m_current.path; }
    qint64 position() const;
    qint64 duration() const;
    qreal volume() const { return m_volume; }
    void setVolume(qreal volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    bool shuffle() const { return m_queue.shuffle(); }
    void setShuffle(bool shuffle);
    int repeat() const { return int(m_queue.repeat()); }
    void setRepeat(int repeat);
    QVariantList queue() const;
    int queuePosition() const { return m_queue.position(); }
    bool canNext() const { return m_queue.hasNext(); }
    bool canPrevious() const { return hasTrack(); }
    /// The current track's cover as a file, for the desktop's media widget.
    QUrl artUrl() const { return m_artUrl; }

    Q_INVOKABLE void playTracks(const QStringList &paths, int start = 0, bool autoplay = true);
    Q_INVOKABLE void shuffleTracks(const QStringList &paths);
    Q_INVOKABLE void playNext(const QStringList &paths);
    Q_INVOKABLE void addToQueue(const QStringList &paths);
    Q_INVOKABLE void jumpTo(int position);
    Q_INVOKABLE void removeFromQueue(int position);
    Q_INVOKABLE void clearUpcoming();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    Q_INVOKABLE void cycleRepeat();
    /// Remembers the queue and where it was, for the next start.
    void saveSession();

Q_SIGNALS:
    void playingChanged();
    void currentChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void modeChanged();
    void queueChanged();
    void seeked(qint64 position);
    void failed(const QString &message);

private:
    void load(bool autoplay, qint64 startAt = 0);
    void ended();
    void trackFailed(const QString &message);
    void setPlaying(bool playing);
    void applyVolume();
    QUrl exportCover(const Track &track) const;

    Library *m_library;
    Store *m_store;
    QString m_cacheDir;
    Queue m_queue;
    QMediaPlayer *m_media = nullptr;
    QAudioOutput *m_audio = nullptr;
    Track m_current;
    QUrl m_artUrl;
    QTimer m_saveLater;
    qreal m_volume = 0.8;
    bool m_muted = false;
    bool m_playing = false;
    qint64 m_pendingSeek = 0;
    qint64 m_silentPosition = 0;
    int m_failures = 0;
};
