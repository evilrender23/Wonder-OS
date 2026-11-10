#include "queue.h"

#include <algorithm>
#include <numeric>

Queue::Queue(QObject *parent)
    : QObject(parent)
    , m_random(QRandomGenerator::global()->generate())
{
}

QStringList Queue::items() const
{
    QStringList list;
    list.reserve(m_order.size());
    for (const int index : m_order) {
        list << m_paths.at(index);
    }
    return list;
}

QString Queue::current() const
{
    return m_position >= 0 && m_position < m_order.size() ? m_paths.at(m_order.at(m_position)) : QString();
}

bool Queue::hasNext() const
{
    return !isEmpty() && (m_position + 1 < m_order.size() || m_repeat != Repeat::Off);
}

bool Queue::hasPrevious() const
{
    return !isEmpty() && (m_position > 0 || m_repeat == Repeat::All);
}

void Queue::shuffleFrom(int first)
{
    if (first < m_order.size()) {
        std::shuffle(m_order.begin() + first, m_order.end(), m_random);
    }
}

void Queue::setTracks(const QStringList &paths, int start)
{
    m_paths = paths;
    m_order.resize(paths.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    if (paths.isEmpty()) {
        m_position = -1;
    } else {
        m_position = std::clamp(start, 0, int(paths.size()) - 1);
        if (m_shuffle) {
            std::swap(m_order[0], m_order[m_position]);
            m_position = 0;
            shuffleFrom(1);
        }
    }
    Q_EMIT itemsChanged();
    Q_EMIT positionChanged();
}

void Queue::playNext(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    const bool wasEmpty = isEmpty();
    int at = m_position + 1;
    for (const QString &path : paths) {
        m_paths << path;
        m_order.insert(at++, int(m_paths.size()) - 1);
    }
    Q_EMIT itemsChanged();
    if (wasEmpty) {
        m_position = 0;
        Q_EMIT positionChanged();
    }
}

void Queue::append(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    const bool wasEmpty = isEmpty();
    for (const QString &path : paths) {
        m_paths << path;
        m_order << int(m_paths.size()) - 1;
    }
    Q_EMIT itemsChanged();
    if (wasEmpty) {
        m_position = 0;
        Q_EMIT positionChanged();
    }
}

void Queue::removeAt(int position)
{
    if (position < 0 || position >= m_order.size() || position == m_position) {
        return;
    }
    const int index = m_order.takeAt(position);
    m_paths.removeAt(index);
    for (int &other : m_order) {
        if (other > index) {
            --other;
        }
    }
    Q_EMIT itemsChanged();
    if (position < m_position) {
        --m_position;
        Q_EMIT positionChanged();
    }
}

void Queue::clearUpcoming()
{
    if (m_position < 0 || m_position + 1 >= m_order.size()) {
        return;
    }
    // Rebuild from what stays, so the indexes into m_paths remain dense.
    const QStringList kept = items().mid(0, m_position + 1);
    m_paths = kept;
    m_order.resize(kept.size());
    std::iota(m_order.begin(), m_order.end(), 0);
    Q_EMIT itemsChanged();
}

void Queue::clear()
{
    if (isEmpty()) {
        return;
    }
    m_paths.clear();
    m_order.clear();
    m_position = -1;
    Q_EMIT itemsChanged();
    Q_EMIT positionChanged();
}

bool Queue::jumpTo(int position)
{
    if (position < 0 || position >= m_order.size()) {
        return false;
    }
    m_position = position;
    Q_EMIT positionChanged();
    return true;
}

bool Queue::next(bool ended)
{
    if (isEmpty()) {
        return false;
    }
    if (ended && m_repeat == Repeat::One) {
        Q_EMIT positionChanged();
        return true;
    }
    if (m_position + 1 < m_order.size()) {
        ++m_position;
    } else if (m_repeat == Repeat::Off) {
        return false;
    } else {
        // Round again: a shuffled queue gets a new order, not the same one twice.
        if (m_shuffle && m_order.size() > 2) {
            const int last = m_order.last();
            shuffleFrom(0);
            if (m_order.first() == last) {
                std::swap(m_order.first(), m_order.last());
            }
            Q_EMIT itemsChanged();
        }
        m_position = 0;
    }
    Q_EMIT positionChanged();
    return true;
}

bool Queue::previous()
{
    if (isEmpty()) {
        return false;
    }
    if (m_position > 0) {
        --m_position;
    } else if (m_repeat == Repeat::All) {
        m_position = int(m_order.size()) - 1;
    } else {
        return false;
    }
    Q_EMIT positionChanged();
    return true;
}

void Queue::setShuffle(bool shuffle)
{
    if (m_shuffle == shuffle) {
        return;
    }
    m_shuffle = shuffle;
    if (!isEmpty()) {
        if (shuffle) {
            // What already played stays played; only what is left to hear is mixed.
            shuffleFrom(m_position + 1);
        } else {
            const int index = m_order.at(m_position);
            std::iota(m_order.begin(), m_order.end(), 0);
            m_position = index;
            Q_EMIT positionChanged();
        }
        Q_EMIT itemsChanged();
    }
    Q_EMIT modeChanged();
}

void Queue::setRepeat(Repeat repeat)
{
    if (m_repeat == repeat) {
        return;
    }
    m_repeat = repeat;
    Q_EMIT modeChanged();
}
