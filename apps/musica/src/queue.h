#pragma once

#include <QObject>
#include <QRandomGenerator>
#include <QStringList>

/// What plays and in which order. It knows nothing about sound: the player
/// asks it what comes next.
class Queue : public QObject
{
    Q_OBJECT

public:
    enum class Repeat { Off, All, One };

    explicit Queue(QObject *parent = nullptr);

    /// The files in playing order (shuffled or not).
    QStringList items() const;
    int position() const { return m_position; }
    int size() const { return int(m_order.size()); }
    bool isEmpty() const { return m_order.isEmpty(); }
    QString current() const;
    bool shuffle() const { return m_shuffle; }
    Repeat repeat() const { return m_repeat; }
    bool hasNext() const;
    bool hasPrevious() const;

    /// Replaces the queue and starts at `start`. Shuffling keeps that track first.
    void setTracks(const QStringList &paths, int start = 0);
    /// Right after the current track, in the order given.
    void playNext(const QStringList &paths);
    void append(const QStringList &paths);
    void removeAt(int position);
    /// Drops everything after the current track.
    void clearUpcoming();
    void clear();
    bool jumpTo(int position);

    /// Moves on. `ended`: the track finished by itself, which is when «repeat one»
    /// plays it again; a press of the button always moves. False at the end of the queue.
    bool next(bool ended = false);
    bool previous();

    void setShuffle(bool shuffle);
    void setRepeat(Repeat repeat);
    /// A fixed seed, for tests.
    void seed(quint32 seed) { m_random.seed(seed); }

Q_SIGNALS:
    void itemsChanged();
    void positionChanged();
    void modeChanged();

private:
    void shuffleFrom(int first);

    /// The files in the order they were queued; `m_order` indexes into it.
    QStringList m_paths;
    QList<int> m_order;
    int m_position = -1;
    bool m_shuffle = false;
    Repeat m_repeat = Repeat::Off;
    QRandomGenerator m_random;
};
