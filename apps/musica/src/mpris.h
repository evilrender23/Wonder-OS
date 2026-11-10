#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QStringList>
#include <QVariantMap>

class App;
class Player;

/// MPRIS: how the desktop's media keys, the bar's player and the lock screen
/// see and drive what is playing.
namespace Mpris
{
/// Puts the player on the session bus. False if another Música already holds the name.
bool publish(App *app);
/// Hands files to the Música that is already running.
void forward(const QStringList &uris);
}

class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ yes CONSTANT)
    Q_PROPERTY(bool CanRaise READ yes CONSTANT)
    Q_PROPERTY(bool HasTrackList READ no CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes CONSTANT)

public:
    explicit MprisRoot(App *app);
    bool yes() const { return true; }
    bool no() const { return false; }
    QString identity() const { return tr("Música"); }
    QString desktopEntry() const { return QStringLiteral("wonderland-musica"); }
    QStringList schemes() const { return {QStringLiteral("file")}; }
    QStringList mimeTypes() const;

public Q_SLOTS:
    void Raise();
    void Quit();

private:
    App *m_app;
};

class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate CONSTANT)
    Q_PROPERTY(double MaximumRate READ rate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ hasTrack)
    Q_PROPERTY(bool CanPlay READ hasTrack)
    Q_PROPERTY(bool CanPause READ hasTrack)
    Q_PROPERTY(bool CanSeek READ hasTrack)
    Q_PROPERTY(bool CanControl READ yes CONSTANT)

public:
    MprisPlayer(App *app, Player *player);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
    bool canGoNext() const;
    bool hasTrack() const;
    bool yes() const { return true; }

public Q_SLOTS:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &track, qlonglong position);
    void OpenUri(const QString &uri);

Q_SIGNALS:
    void Seeked(qlonglong position);

private:
    void changed(const QVariantMap &properties) const;

    App *m_app;
    Player *m_player;
};

/// What a second launch uses to pass its files on.
class MusicaAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wonderland.Musica")

public:
    explicit MusicaAdaptor(App *app);

public Q_SLOTS:
    void Open(const QStringList &uris);

private:
    App *m_app;
};
