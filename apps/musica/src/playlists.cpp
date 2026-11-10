#include "playlists.h"

#include "store.h"

#include <QJsonArray>
#include <QUuid>

static QJsonArray toArray(const QStringList &list)
{
    QJsonArray array;
    for (const QString &each : list) {
        array << each;
    }
    return array;
}

static QStringList toList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &each : value.toArray()) {
        list << each.toString();
    }
    return list;
}

Playlists::Playlists(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    const QJsonObject data = Store::read(m_path);
    m_favorites = toList(data.value(QLatin1String("favorites")));
    for (const QJsonValue &value : data.value(QLatin1String("playlists")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QLatin1String("id")).toString();
        if (!id.isEmpty() && id != favorites()) {
            m_lists.append({id, object.value(QLatin1String("name")).toString(), toList(object.value(QLatin1String("tracks")))});
        }
    }
}

QVariantList Playlists::list() const
{
    QVariantList out;
    for (const Playlist &playlist : m_lists) {
        out << QVariantMap{
            {QStringLiteral("id"), playlist.id},
            {QStringLiteral("name"), playlist.name},
            {QStringLiteral("count"), playlist.paths.size()},
            {QStringLiteral("cover"), playlist.paths.value(0)},
        };
    }
    return out;
}

Playlists::Playlist *Playlists::find(const QString &id)
{
    for (Playlist &playlist : m_lists) {
        if (playlist.id == id) {
            return &playlist;
        }
    }
    return nullptr;
}

const Playlists::Playlist *Playlists::find(const QString &id) const
{
    return const_cast<Playlists *>(this)->find(id);
}

void Playlists::touched(const QString &id)
{
    ++m_revision;
    save();
    Q_EMIT tracksChanged(id);
    if (id != favorites()) {
        Q_EMIT listChanged();
    }
}

QString Playlists::create(const QString &name, const QStringList &paths)
{
    QString title = name.trimmed();
    if (title.isEmpty()) {
        title = tr("Nueva playlist");
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_lists.append({id, title, paths});
    touched(id);
    return id;
}

void Playlists::rename(const QString &id, const QString &name)
{
    Playlist *playlist = find(id);
    const QString title = name.trimmed();
    if (!playlist || title.isEmpty() || playlist->name == title) {
        return;
    }
    playlist->name = title;
    save();
    Q_EMIT listChanged();
}

void Playlists::remove(const QString &id)
{
    for (qsizetype i = 0; i < m_lists.size(); ++i) {
        if (m_lists.at(i).id == id) {
            m_lists.removeAt(i);
            touched(id);
            return;
        }
    }
}

void Playlists::add(const QString &id, const QStringList &paths)
{
    if (paths.isEmpty()) {
        return;
    }
    if (id == favorites()) {
        for (const QString &path : paths) {
            if (!m_favorites.contains(path)) {
                m_favorites << path;
            }
        }
    } else if (Playlist *playlist = find(id)) {
        playlist->paths << paths;
    } else {
        return;
    }
    touched(id);
}

void Playlists::removeAt(const QString &id, int index)
{
    QStringList *paths = id == favorites() ? &m_favorites : nullptr;
    if (Playlist *playlist = find(id)) {
        paths = &playlist->paths;
    }
    if (!paths || index < 0 || index >= paths->size()) {
        return;
    }
    paths->removeAt(index);
    touched(id);
}

QString Playlists::name(const QString &id) const
{
    if (id == favorites()) {
        return tr("Favoritas");
    }
    const Playlist *playlist = find(id);
    return playlist ? playlist->name : QString();
}

QStringList Playlists::paths(const QString &id) const
{
    if (id == favorites()) {
        return m_favorites;
    }
    const Playlist *playlist = find(id);
    return playlist ? playlist->paths : QStringList();
}

bool Playlists::isFavorite(const QString &path) const
{
    return m_favorites.contains(path);
}

void Playlists::toggleFavorite(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (!m_favorites.removeOne(path)) {
        m_favorites << path;
    }
    touched(favorites());
}

void Playlists::save() const
{
    QJsonArray lists;
    for (const Playlist &playlist : m_lists) {
        lists << QJsonObject{
            {QStringLiteral("id"), playlist.id},
            {QStringLiteral("name"), playlist.name},
            {QStringLiteral("tracks"), toArray(playlist.paths)},
        };
    }
    Store::write(m_path, {{QStringLiteral("favorites"), toArray(m_favorites)}, {QStringLiteral("playlists"), lists}});
}
