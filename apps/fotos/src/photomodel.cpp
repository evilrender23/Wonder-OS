#include "photomodel.h"

#include <QLocale>

static QString capitalized(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

static QString counted(int photos, int videos)
{
    if (videos == 0) {
        return photos == 1 ? QStringLiteral("1 foto") : QStringLiteral("%L1 fotos").arg(photos);
    }
    if (photos == 0) {
        return videos == 1 ? QStringLiteral("1 vídeo") : QStringLiteral("%L1 vídeos").arg(videos);
    }
    return QStringLiteral("%L1 elementos").arg(photos + videos);
}

PhotoModel::PhotoModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void PhotoModel::setLibrary(Library *library)
{
    if (library == m_library) {
        return;
    }
    if (m_library) {
        m_library->disconnect(this);
    }
    m_library = library;
    if (m_library) {
        connect(m_library, &Library::changed, this, &PhotoModel::reload);
    }
    Q_EMIT sourceChanged();
    reload();
}

void PhotoModel::setAlbums(Albums *albums)
{
    if (albums == m_albums) {
        return;
    }
    if (m_albums) {
        m_albums->disconnect(this);
    }
    m_albums = albums;
    if (m_albums) {
        connect(m_albums, &Albums::changed, this, [this] {
            // Only these two pages are made of what the albums say.
            if (m_mode == QLatin1String("favorites") || m_mode == QLatin1String("album")) {
                reload();
            }
        });
    }
    Q_EMIT sourceChanged();
    reload();
}

void PhotoModel::setMode(const QString &mode)
{
    if (mode != m_mode) {
        m_mode = mode;
        Q_EMIT sourceChanged();
        reload();
    }
}

void PhotoModel::setKey(const QString &key)
{
    if (key != m_key) {
        m_key = key;
        Q_EMIT sourceChanged();
        reload();
    }
}

void PhotoModel::setQuery(const QString &query)
{
    if (query != m_query) {
        m_query = query;
        Q_EMIT sourceChanged();
        reload();
    }
}

void PhotoModel::setGrouping(const QString &grouping)
{
    if (grouping != m_grouping) {
        m_grouping = grouping;
        Q_EMIT sourceChanged();
        beginResetModel();
        layout();
        endResetModel();
    }
}

void PhotoModel::setColumns(int columns)
{
    columns = qBound(1, columns, 40);
    if (columns != m_columns) {
        m_columns = columns;
        Q_EMIT columnsChanged();
        beginResetModel();
        layout();
        endResetModel();
    }
}

QString PhotoModel::title(const QDateTime &when, const QString &grouping)
{
    const QLocale locale(QLocale::Spanish, QLocale::Spain);
    if (grouping == QLatin1String("year")) {
        return QString::number(when.date().year());
    }
    if (grouping == QLatin1String("day")) {
        return locale.toString(when.date(), QStringLiteral("d 'de' MMMM 'de' yyyy"));
    }
    return capitalized(locale.toString(when.date(), QStringLiteral("MMMM 'de' yyyy")));
}

bool PhotoModel::matches(const MediaItem &item, const QStringList &words, const QString &place)
{
    if (words.isEmpty()) {
        return true;
    }
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(item.taken);
    const QString hay = Media::fold(item.name() + QLatin1Char(' ') + place + QLatin1Char(' ') + item.camera + QLatin1Char(' ') + title(when, QStringLiteral("day"))
                                    + (item.video ? QStringLiteral(" vídeo") : QStringLiteral(" foto")) + (item.screenshot ? QStringLiteral(" captura") : QString()));
    for (const QString &word : words) {
        if (!hay.contains(word)) {
            return false;
        }
    }
    return true;
}

void PhotoModel::reload()
{
    beginResetModel();
    m_items.clear();
    m_videos = 0;
    if (m_library) {
        const QStringList words = Media::fold(m_query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // Many pictures share a folder: its name is worked out once.
        QHash<QString, QString> places;
        const auto wanted = [&](const MediaItem &item) {
            if (words.isEmpty()) {
                return true;
            }
            const QString folder = item.folder();
            auto place = places.constFind(folder);
            if (place == places.constEnd()) {
                place = places.insert(folder, m_library->placeName(folder));
            }
            return matches(item, words, *place);
        };
        const bool fromList = m_mode == QLatin1String("album") || m_mode == QLatin1String("favorites");
        if (fromList) {
            if (m_albums) {
                const QStringList listed = m_mode == QLatin1String("album") ? m_albums->paths(m_key) : m_albums->favorites();
                for (const QString &path : listed) {
                    // What is not on disk now (a drive not plugged in) waits out of sight.
                    const MediaItem item = m_library->find(path);
                    if (!item.path.isEmpty() && wanted(item)) {
                        m_items << item;
                    }
                }
            }
            if (m_mode == QLatin1String("favorites")) {
                std::stable_sort(m_items.begin(), m_items.end(), [](const MediaItem &a, const MediaItem &b) {
                    return a.taken > b.taken;
                });
            }
        } else {
            for (const MediaItem &item : m_library->items()) {
                const bool in = m_mode == QLatin1String("videos") ? item.video
                    : m_mode == QLatin1String("screenshots") ? item.screenshot
                    : m_mode == QLatin1String("folder") ? item.folder() == m_key
                    : true;
                if (in && wanted(item)) {
                    m_items << item;
                }
            }
        }
    }
    m_paths.clear();
    m_paths.reserve(m_items.size());
    for (const MediaItem &item : std::as_const(m_items)) {
        m_paths << item.path;
        m_videos += item.video ? 1 : 0;
    }
    layout();
    endResetModel();
    Q_EMIT contentChanged();
}

void PhotoModel::layout()
{
    m_rows.clear();
    const bool grouped = m_grouping != QLatin1String("none") && m_mode != QLatin1String("album");
    const auto lines = [this](int first, int count) {
        for (int at = 0; at < count; at += m_columns) {
            Row row;
            row.first = first + at;
            row.count = qMin(m_columns, count - at);
            m_rows << row;
        }
    };
    if (!grouped) {
        lines(0, int(m_items.size()));
        return;
    }
    int start = 0;
    while (start < m_items.size()) {
        const QString name = title(QDateTime::fromMSecsSinceEpoch(m_items.at(start).taken), m_grouping);
        int end = start;
        int videos = 0;
        while (end < m_items.size() && title(QDateTime::fromMSecsSinceEpoch(m_items.at(end).taken), m_grouping) == name) {
            videos += m_items.at(end).video ? 1 : 0;
            ++end;
        }
        Row header;
        header.header = true;
        header.title = name;
        header.detail = counted(end - start - videos, videos);
        header.first = start;
        header.count = end - start;
        m_rows << header;
        lines(start, end - start);
        start = end;
    }
}

int PhotoModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariantMap PhotoModel::cell(int index) const
{
    const MediaItem &item = m_items.at(index);
    return {
        {QStringLiteral("path"), item.path},
        {QStringLiteral("index"), index},
        {QStringLiteral("video"), item.video},
        {QStringLiteral("duration"), item.duration},
        {QStringLiteral("stamp"), item.mtime},
    };
}

QVariant PhotoModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case HeaderRole:
        return row.header;
    case TitleRole:
        return row.title;
    case DetailRole:
        return row.detail;
    case FirstRole:
        return row.first;
    case CellsRole: {
        QVariantList cells;
        if (!row.header) {
            for (int i = 0; i < row.count; ++i) {
                cells.append(cell(row.first + i));
            }
        }
        return cells;
    }
    }
    return {};
}

QHash<int, QByteArray> PhotoModel::roleNames() const
{
    return {{HeaderRole, "header"}, {TitleRole, "title"}, {DetailRole, "detail"}, {CellsRole, "cells"}, {FirstRole, "first"}};
}

int PhotoModel::rowOf(int index) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &row = m_rows.at(i);
        if (!row.header && index >= row.first && index < row.first + row.count) {
            return i;
        }
    }
    return -1;
}

QStringList PhotoModel::range(int from, int to) const
{
    const int first = qMax(0, qMin(from, to));
    const int last = qMin(int(m_paths.size()) - 1, qMax(from, to));
    return first <= last ? m_paths.mid(first, last - first + 1) : QStringList();
}

QStringList PhotoModel::group(int row) const
{
    if (row < 0 || row >= m_rows.size() || !m_rows.at(row).header) {
        return {};
    }
    return m_paths.mid(m_rows.at(row).first, m_rows.at(row).count);
}
