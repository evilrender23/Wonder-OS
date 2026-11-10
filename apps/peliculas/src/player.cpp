#include "player.h"

#include "history.h"
#include "media.h"
#include "store.h"

#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QStandardPaths>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtAudio>

static QString trackLabel(const QMediaMetaData &meta, int number)
{
    QString label = Media::languageName(meta.value(QMediaMetaData::Language).value<QLocale::Language>());
    const QString title = meta.stringValue(QMediaMetaData::Title);
    if (label.isEmpty()) {
        label = title.isEmpty() ? QStringLiteral("Pista %1").arg(number) : title;
    } else if (!title.isEmpty() && title.compare(label, Qt::CaseInsensitive) != 0) {
        label += QStringLiteral(" — ") + title;
    }
    return label;
}

Player::Player(Store *store, History *history, bool silent, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_history(history)
    , m_playlist(new Playlist(this))
    , m_media(new QMediaPlayer(this))
{
    m_volume = qBound(0.0, store->value(QStringLiteral("volume"), 1.0).toDouble(), 1.0);
    m_muted = store->value(QStringLiteral("muted"), false).toBool();
    m_playlist->setRepeat(store->value(QStringLiteral("repeat"), 0).toInt());
    m_playlist->setShuffle(store->value(QStringLiteral("shuffle"), false).toBool());
    if (!silent) {
        m_audio = new QAudioOutput(this);
        m_media->setAudioOutput(m_audio);
        applyVolume();
    }

    connect(m_media, &QMediaPlayer::playbackStateChanged, this, [this] {
        if (playing()) {
            m_recordTimer.start();
        } else {
            m_recordTimer.stop();
            record();
        }
        Q_EMIT playingChanged();
    });
    connect(m_media, &QMediaPlayer::positionChanged, this, [this] {
        refreshSubtitleText();
        Q_EMIT positionChanged();
    });
    connect(m_media, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(m_media, &QMediaPlayer::playbackRateChanged, this, &Player::rateChanged);
    connect(m_media, &QMediaPlayer::tracksChanged, this, &Player::refreshTracks);
    connect(m_media, &QMediaPlayer::hasVideoChanged, this, &Player::tracksChanged);
    connect(m_media, &QMediaPlayer::activeTracksChanged, this, &Player::tracksChanged);
    connect(m_media, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if ((status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) && m_resumeAt > 0) {
            const qint64 at = m_resumeAt;
            m_resumeAt = 0;
            if (at < duration()) {
                m_media->setPosition(at);
                Q_EMIT resumed(at);
            }
        } else if (status == QMediaPlayer::EndOfMedia) {
            m_history->record(m_path, duration(), duration(), QDateTime::currentMSecsSinceEpoch());
            const int following = m_playlist->next(false);
            if (following >= 0) {
                m_playlist->setCurrent(following);
                load(m_playlist->currentPath());
            }
        } else if (status == QMediaPlayer::InvalidMedia) {
            Q_EMIT failed(QStringLiteral("No se puede reproducir «%1».").arg(title()));
        }
    });
    connect(m_media, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &) {
        if (error != QMediaPlayer::NoError && hasMedia()) {
            Q_EMIT failed(QStringLiteral("No se puede reproducir «%1».").arg(title()));
        }
    });

    connect(m_playlist, &Playlist::countChanged, this, &Player::queueChanged);
    connect(m_playlist, &Playlist::currentChanged, this, &Player::queueChanged);
    connect(m_playlist, &Playlist::modeChanged, this, &Player::queueChanged);
    connect(m_playlist, &Playlist::currentRemoved, this, [this] {
        // What was playing is no longer in the list, so nothing of it is noted.
        m_path.clear();
        if (m_playlist->current() >= 0) {
            load(m_playlist->currentPath());
        } else {
            unload();
        }
    });

    m_recordTimer.setInterval(5000);
    connect(&m_recordTimer, &QTimer::timeout, this, &Player::record);
}

void Player::setVideoOutput(QObject *output)
{
    if (output == m_videoOutput) {
        return;
    }
    m_videoOutput = output;
    m_media->setVideoOutput(output);
    Q_EMIT videoOutputChanged();
}

QString Player::title() const
{
    return m_path.isEmpty() ? QString() : Media::title(m_path);
}

bool Player::hasVideo() const
{
    return hasMedia() && m_media->hasVideo();
}

bool Player::playing() const
{
    return m_media->playbackState() == QMediaPlayer::PlayingState;
}

qint64 Player::position() const
{
    return hasMedia() ? m_media->position() : 0;
}

qint64 Player::duration() const
{
    return hasMedia() ? m_media->duration() : 0;
}

void Player::applyVolume()
{
    if (m_audio) {
        m_audio->setVolume(QtAudio::convertVolume(float(m_volume), QtAudio::LogarithmicVolumeScale, QtAudio::LinearVolumeScale));
        m_audio->setMuted(m_muted);
    }
}

void Player::setVolume(qreal volume)
{
    volume = qBound(0.0, volume, 1.0);
    // Turning it up is asking to hear it.
    const bool muted = m_muted && volume <= m_volume;
    if (qFuzzyCompare(volume + 1.0, m_volume + 1.0) && muted == m_muted) {
        return;
    }
    m_volume = volume;
    m_muted = muted;
    applyVolume();
    Q_EMIT volumeChanged();
}

void Player::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    m_muted = muted;
    applyVolume();
    Q_EMIT volumeChanged();
}

qreal Player::rate() const
{
    return m_media->playbackRate();
}

void Player::setRate(qreal rate)
{
    m_media->setPlaybackRate(qBound(0.25, rate, 4.0));
}

int Player::audioTrack() const
{
    return m_media->activeAudioTrack();
}

void Player::setAudioTrack(int index)
{
    if (index >= 0 && index < m_audioTracks.size()) {
        m_media->setActiveAudioTrack(index);
    }
}

void Player::open(const QStringList &paths, int index)
{
    record();
    // Cleared first: the list changing under it is not «the film was removed».
    m_path.clear();
    m_playlist->set(paths, index);
    if (m_playlist->current() >= 0) {
        load(m_playlist->currentPath());
    } else {
        unload();
    }
}

void Player::enqueue(const QStringList &paths)
{
    const int first = m_playlist->append(paths);
    if (first >= 0 && !hasMedia()) {
        playAt(first);
    }
}

void Player::playAt(int index)
{
    if (index < 0 || index >= m_playlist->count()) {
        return;
    }
    record();
    m_playlist->setCurrent(index);
    load(m_playlist->currentPath());
}

void Player::load(const QString &path)
{
    m_path = path;
    m_resumeAt = 0;
    m_subtitles.clear();
    m_subtitle.clear();
    m_subtitleFiles = Subtitles::beside(path);
    if (!QFileInfo::exists(path)) {
        m_media->stop();
        m_media->setSource(QUrl());
        refreshTracks();
        Q_EMIT currentChanged();
        Q_EMIT failed(QStringLiteral("No se encuentra «%1».").arg(title()));
        return;
    }
    m_resumeAt = m_history->resume(path);
    m_media->setSource(QUrl::fromLocalFile(path));
    // Subtitles left next to the film are there to be read.
    if (!m_subtitleFiles.isEmpty() && m_subtitles.load(m_subtitleFiles.first())) {
        m_subtitle = QStringLiteral("f:") + m_subtitleFiles.first();
    }
    refreshTracks();
    refreshSubtitleText();
    Q_EMIT currentChanged();
    Q_EMIT subtitleChanged();
    Q_EMIT durationChanged();
    Q_EMIT positionChanged();
    m_history->record(path, m_resumeAt, m_history->duration(path), QDateTime::currentMSecsSinceEpoch());
    m_media->play();
}

void Player::unload()
{
    m_path.clear();
    m_resumeAt = 0;
    m_media->stop();
    m_media->setSource(QUrl());
    m_subtitles.clear();
    m_subtitle.clear();
    m_subtitleFiles.clear();
    refreshTracks();
    refreshSubtitleText();
    Q_EMIT currentChanged();
    Q_EMIT subtitleChanged();
    Q_EMIT durationChanged();
    Q_EMIT positionChanged();
}

void Player::record()
{
    // Before it has loaded there is nothing true to note, and what was known must stay.
    if (hasMedia() && duration() > 0 && m_resumeAt == 0) {
        m_history->record(m_path, position(), duration(), QDateTime::currentMSecsSinceEpoch());
    }
}

void Player::shutdown()
{
    record();
    m_store->set(QStringLiteral("volume"), m_volume);
    m_store->set(QStringLiteral("muted"), m_muted);
    m_store->set(QStringLiteral("repeat"), m_playlist->repeat());
    m_store->set(QStringLiteral("shuffle"), m_playlist->shuffle());
}

void Player::play()
{
    if (!hasMedia()) {
        return;
    }
    // Ended and asked for again: from the top.
    if (m_media->mediaStatus() == QMediaPlayer::EndOfMedia) {
        m_media->setPosition(0);
    }
    m_media->play();
}

void Player::pause()
{
    m_media->pause();
}

void Player::toggle()
{
    if (playing()) {
        pause();
    } else {
        play();
    }
}

void Player::close()
{
    record();
    m_path.clear();
    m_playlist->clear();
    unload();
}

void Player::next()
{
    playAt(m_playlist->next(true));
}

void Player::previous()
{
    const int before = m_playlist->previous();
    if (position() > 5000 || before < 0) {
        seek(0);
    } else {
        playAt(before);
    }
}

void Player::seek(qint64 ms)
{
    if (!hasMedia()) {
        return;
    }
    m_resumeAt = 0;
    const qint64 to = qBound(qint64(0), ms, qMax(qint64(0), duration()));
    m_media->setPosition(to);
    Q_EMIT seeked(to);
}

void Player::step(qint64 ms)
{
    seek(position() + ms);
}

void Player::refreshTracks()
{
    m_audioTracks.clear();
    m_subtitleChoices.clear();
    if (hasMedia()) {
        const QList<QMediaMetaData> audio = m_media->audioTracks();
        for (int i = 0; i < audio.size(); ++i) {
            m_audioTracks << QVariantMap{{QStringLiteral("index"), i}, {QStringLiteral("label"), trackLabel(audio.at(i), i + 1)}};
        }
        m_subtitleChoices << QVariantMap{{QStringLiteral("key"), QString()}, {QStringLiteral("label"), QStringLiteral("Desactivados")}};
        const QList<QMediaMetaData> inside = m_media->subtitleTracks();
        for (int i = 0; i < inside.size(); ++i) {
            m_subtitleChoices << QVariantMap{{QStringLiteral("key"), QStringLiteral("t:%1").arg(i)}, {QStringLiteral("label"), trackLabel(inside.at(i), i + 1)}};
        }
        for (const QString &file : std::as_const(m_subtitleFiles)) {
            m_subtitleChoices << QVariantMap{{QStringLiteral("key"), QStringLiteral("f:") + file}, {QStringLiteral("label"), Subtitles::label(m_path, file)}};
        }
    }
    Q_EMIT tracksChanged();
}

void Player::setSubtitle(const QString &key)
{
    if (!hasMedia() || key == m_subtitle) {
        return;
    }
    m_subtitles.clear();
    m_subtitle.clear();
    int inside = -1;
    if (key.startsWith(QLatin1String("t:"))) {
        bool ok = false;
        const int index = key.mid(2).toInt(&ok);
        if (ok && index >= 0 && index < m_media->subtitleTracks().size()) {
            inside = index;
            m_subtitle = key;
        }
    } else if (key.startsWith(QLatin1String("f:"))) {
        if (m_subtitles.load(key.mid(2))) {
            m_subtitle = key;
        } else {
            Q_EMIT failed(QStringLiteral("No se pueden leer esos subtítulos."));
        }
    }
    m_media->setActiveSubtitleTrack(inside);
    refreshSubtitleText();
    Q_EMIT subtitleChanged();
}

void Player::cycleSubtitles()
{
    if (m_subtitleChoices.size() < 2) {
        return;
    }
    int at = 0;
    for (int i = 0; i < m_subtitleChoices.size(); ++i) {
        if (m_subtitleChoices.at(i).toMap().value(QStringLiteral("key")).toString() == m_subtitle) {
            at = i;
        }
    }
    setSubtitle(m_subtitleChoices.at((at + 1) % m_subtitleChoices.size()).toMap().value(QStringLiteral("key")).toString());
}

void Player::useSubtitleFile(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (!hasMedia() || path.isEmpty()) {
        return;
    }
    if (!m_subtitleFiles.contains(path)) {
        m_subtitleFiles << path;
        refreshTracks();
    }
    setSubtitle(QStringLiteral("f:") + path);
}

void Player::refreshSubtitleText()
{
    const QString text = m_subtitles.isEmpty() ? QString() : m_subtitles.textAt(position());
    if (text != m_subtitleText) {
        m_subtitleText = text;
        Q_EMIT subtitleTextChanged();
    }
}

QString Player::snapshot()
{
    QVideoSink *sink = m_media->videoSink();
    if (!hasVideo() || !sink) {
        return {};
    }
    const QImage image = sink->videoFrame().toImage();
    if (image.isNull()) {
        return {};
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (folder.isEmpty() || !QDir().mkpath(folder)) {
        return {};
    }
    // «Film 0.12.30.png»: no colons, which some disks refuse.
    QString name = title();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    const QString base = folder + QLatin1Char('/') + name + QLatin1Char(' ') + Media::time(position()).replace(QLatin1Char(':'), QLatin1Char('.'));
    QString path = base + QStringLiteral(".png");
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = base + QStringLiteral(" (%1).png").arg(n);
    }
    return image.save(path) ? path : QString();
}
