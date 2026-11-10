#include "mpris.h"

#include "app.h"
#include "player.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QUrl>

static const QString service = QStringLiteral("org.mpris.MediaPlayer2.wonderland-musica");
static const QString objectPath = QStringLiteral("/org/mpris/MediaPlayer2");
static const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

bool Mpris::publish(App *app)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return true;
    }
    if (!bus.registerService(service)) {
        return false;
    }
    new MprisRoot(app);
    new MprisPlayer(app, app->player());
    new MusicaAdaptor(app);
    bus.registerObject(objectPath, app);
    return true;
}

void Mpris::forward(const QStringList &uris)
{
    QDBusInterface running(service, objectPath, QStringLiteral("org.wonderland.Musica"), QDBusConnection::sessionBus());
    running.call(QStringLiteral("Open"), uris);
}

MprisRoot::MprisRoot(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

QStringList MprisRoot::mimeTypes() const
{
    return {
        QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"), QStringLiteral("audio/opus"),
        QStringLiteral("audio/mp4"), QStringLiteral("audio/aac"), QStringLiteral("audio/wav"), QStringLiteral("audio/x-wav"),
        QStringLiteral("audio/x-ms-wma"), QStringLiteral("audio/aiff"),
    };
}

void MprisRoot::Raise()
{
    m_app->raise();
}

void MprisRoot::Quit()
{
    QCoreApplication::quit();
}

MusicaAdaptor::MusicaAdaptor(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

void MusicaAdaptor::Open(const QStringList &uris)
{
    QList<QUrl> urls;
    for (const QString &uri : uris) {
        urls << QUrl(uri);
    }
    m_app->open(urls);
    m_app->raise();
}

MprisPlayer::MprisPlayer(App *app, Player *player)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
    , m_player(player)
{
    connect(player, &Player::playingChanged, this, [this] {
        changed({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
    });
    connect(player, &Player::currentChanged, this, [this] {
        changed({
            {QStringLiteral("Metadata"), metadata()},
            {QStringLiteral("PlaybackStatus"), playbackStatus()},
            {QStringLiteral("CanPlay"), hasTrack()},
            {QStringLiteral("CanPause"), hasTrack()},
            {QStringLiteral("CanSeek"), hasTrack()},
            {QStringLiteral("CanGoPrevious"), hasTrack()},
            {QStringLiteral("CanGoNext"), canGoNext()},
        });
    });
    connect(player, &Player::queueChanged, this, [this] {
        changed({{QStringLiteral("CanGoNext"), canGoNext()}});
    });
    connect(player, &Player::modeChanged, this, [this] {
        changed({{QStringLiteral("LoopStatus"), loopStatus()}, {QStringLiteral("Shuffle"), shuffle()}});
    });
    connect(player, &Player::volumeChanged, this, [this] {
        changed({{QStringLiteral("Volume"), volume()}});
    });
    connect(player, &Player::seeked, this, [this](qint64 position) {
        Q_EMIT Seeked(position * 1000);
    });
}

void MprisPlayer::changed(const QVariantMap &properties) const
{
    QDBusMessage signal = QDBusMessage::createSignal(objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    signal << playerInterface << properties << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

QString MprisPlayer::playbackStatus() const
{
    if (m_player->playing()) {
        return QStringLiteral("Playing");
    }
    return m_player->hasTrack() ? QStringLiteral("Paused") : QStringLiteral("Stopped");
}

QString MprisPlayer::loopStatus() const
{
    switch (m_player->repeat()) {
    case 1:
        return QStringLiteral("Playlist");
    case 2:
        return QStringLiteral("Track");
    }
    return QStringLiteral("None");
}

void MprisPlayer::setLoopStatus(const QString &status)
{
    m_player->setRepeat(status == QLatin1String("Playlist") ? 1 : status == QLatin1String("Track") ? 2 : 0);
}

bool MprisPlayer::shuffle() const
{
    return m_player->shuffle();
}

void MprisPlayer::setShuffle(bool shuffle)
{
    m_player->setShuffle(shuffle);
}

QVariantMap MprisPlayer::metadata() const
{
    const Track &track = m_player->track();
    if (!track.isValid()) {
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")))}};
    }
    const QString id = QString::fromLatin1(QCryptographicHash::hash(track.path.toUtf8(), QCryptographicHash::Md5).toHex());
    QVariantMap map{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/wonderland/Musica/track/") + id))},
        {QStringLiteral("mpris:length"), qlonglong(m_player->duration() * 1000)},
        {QStringLiteral("xesam:title"), track.title},
        {QStringLiteral("xesam:artist"), QStringList{track.artist}},
        {QStringLiteral("xesam:album"), track.album},
        {QStringLiteral("xesam:albumArtist"), QStringList{track.albumOwner()}},
        {QStringLiteral("xesam:url"), QUrl::fromLocalFile(track.path).toString()},
    };
    if (track.number > 0) {
        map.insert(QStringLiteral("xesam:trackNumber"), track.number);
    }
    if (!track.genre.isEmpty()) {
        map.insert(QStringLiteral("xesam:genre"), QStringList{track.genre});
    }
    if (!m_player->artUrl().isEmpty()) {
        map.insert(QStringLiteral("mpris:artUrl"), m_player->artUrl().toString());
    }
    return map;
}

double MprisPlayer::volume() const
{
    return m_player->muted() ? 0.0 : m_player->volume();
}

void MprisPlayer::setVolume(double volume)
{
    m_player->setVolume(volume);
}

qlonglong MprisPlayer::position() const
{
    return m_player->position() * 1000;
}

bool MprisPlayer::canGoNext() const
{
    return m_player->canNext();
}

bool MprisPlayer::hasTrack() const
{
    return m_player->hasTrack();
}

void MprisPlayer::Next()
{
    m_player->next();
}

void MprisPlayer::Previous()
{
    m_player->previous();
}

void MprisPlayer::Pause()
{
    m_player->pause();
}

void MprisPlayer::PlayPause()
{
    m_player->toggle();
}

void MprisPlayer::Stop()
{
    m_player->stop();
}

void MprisPlayer::Play()
{
    m_player->play();
}

void MprisPlayer::Seek(qlonglong offset)
{
    m_player->seek(m_player->position() + offset / 1000);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &track, qlonglong position)
{
    // A position meant for a track that already ended must not move the next one.
    if (track.path() == metadata().value(QStringLiteral("mpris:trackid")).value<QDBusObjectPath>().path()) {
        m_player->seek(position / 1000);
    }
}

void MprisPlayer::OpenUri(const QString &uri)
{
    m_app->open({QUrl(uri)});
}
