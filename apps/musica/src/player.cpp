#include "player.h"

#include "library.h"
#include "store.h"
#include "tagreader.h"

#include <QAudio>
#include <QAudioOutput>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMediaPlayer>

/// Past this, «previous» goes back to the start of the track instead of to the one before.
static const qint64 restartAfterMs = 3000;
static const int sessionLimit = 3000;

Player::Player(Library *library, Store *store, const QString &cacheDir, bool silent, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_store(store)
    , m_cacheDir(cacheDir)
{
    m_volume = std::clamp(m_store->value(QStringLiteral("volume"), 0.8).toDouble(), 0.0, 1.0);
    m_queue.setShuffle(m_store->value(QStringLiteral("shuffle"), false).toBool());
    m_queue.setRepeat(Queue::Repeat(std::clamp(m_store->value(QStringLiteral("repeat"), 0).toInt(), 0, 2)));

    connect(&m_queue, &Queue::itemsChanged, this, &Player::queueChanged);
    connect(&m_queue, &Queue::positionChanged, this, &Player::queueChanged);
    connect(&m_queue, &Queue::modeChanged, this, &Player::queueChanged);
    connect(&m_queue, &Queue::modeChanged, this, &Player::modeChanged);

    m_saveLater.setSingleShot(true);
    m_saveLater.setInterval(600);
    connect(&m_saveLater, &QTimer::timeout, this, [this] {
        m_store->set(QStringLiteral("volume"), m_volume);
        m_store->set(QStringLiteral("shuffle"), m_queue.shuffle());
        m_store->set(QStringLiteral("repeat"), int(m_queue.repeat()));
    });
    connect(this, &Player::modeChanged, &m_saveLater, qOverload<>(&QTimer::start));
    connect(this, &Player::volumeChanged, &m_saveLater, qOverload<>(&QTimer::start));

    if (!silent) {
        m_media = new QMediaPlayer(this);
        m_audio = new QAudioOutput(this);
        m_media->setAudioOutput(m_audio);
        applyVolume();
        connect(m_media, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
        connect(m_media, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
        connect(m_media, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            if (state == QMediaPlayer::PlayingState) {
                m_failures = 0;
            }
            // «Stopped» between two tracks is not a pause: ended() and load() decide.
            if (state != QMediaPlayer::StoppedState) {
                setPlaying(state == QMediaPlayer::PlayingState);
            }
        });
        // Queued: the next track's setSource() must not run inside this one's signal.
        connect(m_media, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::EndOfMedia) {
                ended();
            } else if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) && m_pendingSeek > 0) {
                const qint64 to = m_pendingSeek;
                m_pendingSeek = 0;
                m_media->setPosition(to);
            }
        }, Qt::QueuedConnection);
        connect(m_media, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &message) {
            if (error != QMediaPlayer::NoError) {
                trackFailed(message);
            }
        }, Qt::QueuedConnection);
    }

    // The queue of the last run, paused where it was.
    QStringList paths;
    for (const QJsonValue &value : m_store->value(QStringLiteral("queue")).toArray()) {
        const QString path = value.toString();
        if (QFileInfo(path).isFile()) {
            paths << path;
        }
    }
    if (!paths.isEmpty()) {
        const QString last = m_store->value(QStringLiteral("queueCurrent")).toString();
        const int start = int(qMax(qsizetype(0), paths.indexOf(last)));
        const bool shuffled = m_queue.shuffle();
        m_queue.setShuffle(false);
        m_queue.setTracks(paths, start);
        m_queue.setShuffle(shuffled);
        load(false, paths.value(start) == last ? qint64(m_store->value(QStringLiteral("queuePosition")).toDouble()) : 0);
    }
}

Player::~Player() = default;

qint64 Player::position() const
{
    if (!m_media) {
        return m_silentPosition;
    }
    return m_pendingSeek > 0 ? m_pendingSeek : m_media->position();
}

qint64 Player::duration() const
{
    const qint64 media = m_media ? m_media->duration() : 0;
    return media > 0 ? media : m_current.durationMs;
}

QVariantList Player::queue() const
{
    QVariantList list;
    const QStringList items = m_queue.items();
    list.reserve(items.size());
    for (const QString &path : items) {
        list << m_library->track(path).toMap();
    }
    return list;
}

void Player::applyVolume()
{
    if (m_audio) {
        // A slider is heard evenly when it moves loudness, not amplitude.
        m_audio->setVolume(QAudio::convertVolume(float(m_volume), QAudio::LogarithmicVolumeScale, QAudio::LinearVolumeScale));
        m_audio->setMuted(m_muted);
    }
}

void Player::setVolume(qreal volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume + 1, m_volume + 1)) {
        return;
    }
    m_volume = volume;
    m_muted = false;
    applyVolume();
    Q_EMIT volumeChanged();
}

void Player::setMuted(bool muted)
{
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    applyVolume();
    Q_EMIT volumeChanged();
}

void Player::setShuffle(bool shuffle)
{
    m_queue.setShuffle(shuffle);
}

void Player::setRepeat(int repeat)
{
    m_queue.setRepeat(Queue::Repeat(std::clamp(repeat, 0, 2)));
}

void Player::cycleRepeat()
{
    setRepeat((repeat() + 1) % 3);
}

void Player::setPlaying(bool playing)
{
    if (m_playing != playing) {
        m_playing = playing;
        Q_EMIT playingChanged();
    }
}

void Player::load(bool autoplay, qint64 startAt)
{
    const QString path = m_queue.current();
    const Track track = path.isEmpty() ? Track() : m_library->track(path);
    if (!path.isEmpty() && !track.isValid()) {
        m_current = Track();
        trackFailed(QStringLiteral("No se encuentra «%1».").arg(QFileInfo(path).fileName()));
        return;
    }
    m_current = track;
    m_artUrl = track.isValid() ? exportCover(track) : QUrl();
    m_pendingSeek = startAt;
    m_silentPosition = startAt;
    if (m_media) {
        m_media->setSource(track.isValid() ? QUrl::fromLocalFile(track.path) : QUrl());
        if (track.isValid() && autoplay) {
            m_media->play();
        } else {
            setPlaying(false);
        }
    } else {
        setPlaying(track.isValid() && autoplay);
    }
    Q_EMIT currentChanged();
    Q_EMIT durationChanged();
    Q_EMIT positionChanged();
}

void Player::ended()
{
    if (m_queue.next(true)) {
        load(true);
    } else {
        // The end of the queue: back to its first track, ready and quiet.
        m_queue.jumpTo(0);
        load(false);
    }
}

void Player::trackFailed(const QString &message)
{
    Q_EMIT failed(m_current.isValid() ? QStringLiteral("No se puede reproducir «%1»: %2").arg(m_current.title, message) : message);
    // One bad file is skipped; a queue of nothing but bad files stops.
    if (++m_failures < m_queue.size() && m_queue.position() + 1 < m_queue.size() && m_queue.next()) {
        load(true);
    } else {
        m_failures = 0;
        if (m_media) {
            m_media->stop();
        }
        setPlaying(false);
        Q_EMIT currentChanged();
    }
}

void Player::playTracks(const QStringList &paths, int start, bool autoplay)
{
    if (paths.isEmpty()) {
        return;
    }
    m_failures = 0;
    m_queue.setTracks(paths, start);
    load(autoplay);
}

void Player::shuffleTracks(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    setShuffle(true);
    playTracks(paths, int(QRandomGenerator::global()->bounded(paths.size())));
}

void Player::playNext(const QStringList &paths)
{
    const bool wasEmpty = m_queue.isEmpty();
    m_queue.playNext(paths);
    if (wasEmpty && !m_queue.isEmpty()) {
        load(true);
    }
}

void Player::addToQueue(const QStringList &paths)
{
    const bool wasEmpty = m_queue.isEmpty();
    m_queue.append(paths);
    if (wasEmpty && !m_queue.isEmpty()) {
        load(true);
    }
}

void Player::jumpTo(int position)
{
    if (m_queue.jumpTo(position)) {
        m_failures = 0;
        load(true);
    }
}

void Player::removeFromQueue(int position)
{
    m_queue.removeAt(position);
}

void Player::clearUpcoming()
{
    m_queue.clearUpcoming();
}

void Player::play()
{
    if (!hasTrack()) {
        return;
    }
    if (m_media) {
        m_media->play();
    } else {
        setPlaying(true);
    }
}

void Player::pause()
{
    if (m_media) {
        m_media->pause();
    } else {
        setPlaying(false);
    }
}

void Player::toggle()
{
    if (m_playing) {
        pause();
    } else {
        play();
    }
}

void Player::stop()
{
    if (m_media) {
        m_media->stop();
    }
    m_pendingSeek = 0;
    m_silentPosition = 0;
    setPlaying(false);
    Q_EMIT positionChanged();
}

void Player::next()
{
    if (m_queue.next()) {
        m_failures = 0;
        load(true);
    }
}

void Player::previous()
{
    if (!hasTrack()) {
        return;
    }
    if (position() > restartAfterMs || !m_queue.previous()) {
        seek(0);
        return;
    }
    m_failures = 0;
    load(true);
}

void Player::seek(qint64 position)
{
    if (!hasTrack()) {
        return;
    }
    position = std::clamp(position, qint64(0), qMax(qint64(0), duration()));
    m_pendingSeek = 0;
    if (m_media) {
        m_media->setPosition(position);
    } else {
        m_silentPosition = position;
        Q_EMIT positionChanged();
    }
    Q_EMIT seeked(position);
}

QUrl Player::exportCover(const Track &track) const
{
    const QString name = QString::fromLatin1(QCryptographicHash::hash(track.albumKey().toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString file = m_cacheDir + QStringLiteral("/covers/") + name + QStringLiteral(".jpg");
    if (!QFileInfo::exists(file)) {
        QImage image = TagReader::cover(track.path);
        if (image.isNull()) {
            return {};
        }
        if (qMax(image.width(), image.height()) > 640) {
            image = image.scaled(640, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QDir().mkpath(QFileInfo(file).absolutePath());
        if (!image.convertToFormat(QImage::Format_RGB32).save(file, "JPG", 88)) {
            return {};
        }
    }
    return QUrl::fromLocalFile(file);
}

void Player::saveSession()
{
    QJsonArray paths;
    const bool shuffled = m_queue.shuffle();
    const QString current = m_queue.current();
    // Saved in the order the tracks were queued: the next run shuffles them anew.
    m_queue.blockSignals(true);
    m_queue.setShuffle(false);
    const QStringList items = m_queue.items().mid(0, sessionLimit);
    m_queue.setShuffle(shuffled);
    m_queue.blockSignals(false);
    for (const QString &path : items) {
        paths << path;
    }
    m_store->set(QStringLiteral("queue"), paths);
    m_store->set(QStringLiteral("queueCurrent"), current);
    m_store->set(QStringLiteral("queuePosition"), double(position()));
    m_store->set(QStringLiteral("volume"), m_volume);
    m_store->set(QStringLiteral("shuffle"), shuffled);
    m_store->set(QStringLiteral("repeat"), int(m_queue.repeat()));
}
