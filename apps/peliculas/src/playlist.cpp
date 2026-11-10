#include "playlist.h"

#include "media.h"

#include <QDir>
#include <QFileInfo>

Playlist::Playlist(QObject *parent)
    : QAbstractListModel(parent)
    , m_random(QRandomGenerator::securelySeeded())
{
}

int Playlist::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant Playlist::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const QString &path = m_paths.at(index.row());
    switch (role) {
    case PathRole:
        return path;
    case TitleRole:
    case Qt::DisplayRole:
        return Media::title(path);
    case FolderRole:
        return QFileInfo(path).dir().dirName();
    case CurrentRole:
        return index.row() == m_current;
    case AudioRole:
        return Media::isAudio(path);
    }
    return {};
}

QHash<int, QByteArray> Playlist::roleNames() const
{
    return {{PathRole, "path"}, {TitleRole, "title"}, {FolderRole, "folder"}, {CurrentRole, "current"}, {AudioRole, "audio"}};
}

void Playlist::seed(quint32 seed)
{
    m_random.seed(seed);
}

void Playlist::setRepeat(int repeat)
{
    repeat = qBound(0, repeat, 2);
    if (repeat != m_repeat) {
        m_repeat = repeat;
        Q_EMIT modeChanged();
    }
}

void Playlist::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle) {
        return;
    }
    m_shuffle = shuffle;
    reshuffle();
    Q_EMIT modeChanged();
}

void Playlist::reshuffle()
{
    m_order.clear();
    if (!m_shuffle) {
        return;
    }
    m_order = m_paths;
    for (qsizetype i = m_order.size() - 1; i > 0; --i) {
        m_order.swapItemsAt(i, m_random.bounded(quint32(i + 1)));
    }
    // What is playing opens the order: turning shuffle on skips nothing.
    const qsizetype at = m_order.indexOf(currentPath());
    if (at > 0) {
        m_order.move(at, 0);
    }
}

void Playlist::markCurrent(int before)
{
    const QList<int> roles{CurrentRole};
    if (before >= 0 && before < count()) {
        Q_EMIT dataChanged(index(before), index(before), roles);
    }
    if (m_current >= 0 && m_current != before) {
        Q_EMIT dataChanged(index(m_current), index(m_current), roles);
    }
}

void Playlist::set(const QStringList &paths, int current)
{
    QString wanted = paths.value(current);
    beginResetModel();
    m_paths = paths;
    m_paths.removeDuplicates();
    m_current = m_paths.isEmpty() ? -1 : qMax(0, int(m_paths.indexOf(wanted)));
    reshuffle();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
}

int Playlist::append(const QStringList &paths)
{
    QStringList fresh;
    for (const QString &path : paths) {
        if (!m_paths.contains(path) && !fresh.contains(path)) {
            fresh << path;
        }
    }
    if (!fresh.isEmpty()) {
        beginInsertRows({}, count(), count() + int(fresh.size()) - 1);
        m_paths << fresh;
        endInsertRows();
        if (m_shuffle) {
            // Each lands somewhere after what is playing.
            for (const QString &path : std::as_const(fresh)) {
                const int from = int(m_order.indexOf(currentPath())) + 1;
                m_order.insert(from + int(m_random.bounded(quint32(m_order.size() - from + 1))), path);
            }
        }
        Q_EMIT countChanged();
    }
    return paths.isEmpty() ? -1 : int(m_paths.indexOf(paths.first()));
}

void Playlist::setCurrent(int index)
{
    if (index < -1 || index >= count() || index == m_current) {
        return;
    }
    const int before = m_current;
    m_current = index;
    markCurrent(before);
    Q_EMIT currentChanged();
}

int Playlist::next(bool manual) const
{
    if (m_current < 0) {
        return count() > 0 ? 0 : -1;
    }
    if (m_repeat == 2 && !manual) {
        return m_current;
    }
    if (m_shuffle) {
        const qsizetype at = m_order.indexOf(currentPath());
        if (at + 1 < m_order.size()) {
            return int(m_paths.indexOf(m_order.at(at + 1)));
        }
        return m_repeat == 1 && !m_order.isEmpty() ? int(m_paths.indexOf(m_order.first())) : -1;
    }
    if (m_current + 1 < count()) {
        return m_current + 1;
    }
    return m_repeat == 1 ? 0 : -1;
}

int Playlist::previous() const
{
    if (m_current < 0) {
        return -1;
    }
    if (m_shuffle) {
        const qsizetype at = m_order.indexOf(currentPath());
        if (at > 0) {
            return int(m_paths.indexOf(m_order.at(at - 1)));
        }
        return m_repeat == 1 && !m_order.isEmpty() ? int(m_paths.indexOf(m_order.last())) : -1;
    }
    if (m_current > 0) {
        return m_current - 1;
    }
    return m_repeat == 1 ? count() - 1 : -1;
}

void Playlist::remove(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    const bool playing = index == m_current;
    beginRemoveRows({}, index, index);
    m_order.removeOne(m_paths.at(index));
    m_paths.removeAt(index);
    if (index < m_current) {
        --m_current;
    } else if (playing && m_current >= count()) {
        // The last one went: nothing follows it.
        m_current = -1;
    }
    endRemoveRows();
    Q_EMIT countChanged();
    if (playing) {
        markCurrent(-1);
        Q_EMIT currentChanged();
        Q_EMIT currentRemoved();
    } else if (index < m_current + 1) {
        Q_EMIT currentChanged();
    }
}

void Playlist::move(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) {
        return;
    }
    const QString playing = currentPath();
    // Qt counts the destination before the row is taken out.
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_paths.move(from, to);
    m_current = int(m_paths.indexOf(playing));
    endMoveRows();
    Q_EMIT currentChanged();
}

void Playlist::clear()
{
    if (m_paths.isEmpty()) {
        return;
    }
    const bool playing = m_current >= 0;
    beginResetModel();
    m_paths.clear();
    m_order.clear();
    m_current = -1;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
    if (playing) {
        Q_EMIT currentRemoved();
    }
}
