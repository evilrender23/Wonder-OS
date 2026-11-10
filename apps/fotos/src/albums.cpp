#include "albums.h"

#include "store.h"

#include <QJsonArray>
#include <QUuid>

Albums::Albums(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    const QJsonObject root = Store::read(m_path);
    for (const QJsonValue &value : root.value(QLatin1String("favorites")).toArray()) {
        const QString file = value.toString();
        if (!file.isEmpty() && !m_favoriteSet.contains(file)) {
            m_favorites << file;
            m_favoriteSet.insert(file);
        }
    }
    for (const QJsonValue &value : root.value(QLatin1String("albums")).toArray()) {
        const QJsonObject object = value.toObject();
        Album album;
        album.id = object.value(QLatin1String("id")).toString();
        album.name = object.value(QLatin1String("name")).toString();
        for (const QJsonValue &file : object.value(QLatin1String("paths")).toArray()) {
            album.paths << file.toString();
        }
        if (!album.id.isEmpty()) {
            m_albums << album;
        }
    }
}

void Albums::touched()
{
    QJsonArray albums;
    for (const Album &album : std::as_const(m_albums)) {
        albums.append(QJsonObject{
            {QLatin1String("id"), album.id},
            {QLatin1String("name"), album.name},
            {QLatin1String("paths"), QJsonArray::fromStringList(album.paths)},
        });
    }
    Store::write(m_path, {{QLatin1String("favorites"), QJsonArray::fromStringList(m_favorites)}, {QLatin1String("albums"), albums}});
    ++m_revision;
    Q_EMIT changed();
}

Albums::Album *Albums::find(const QString &id)
{
    for (Album &album : m_albums) {
        if (album.id == id) {
            return &album;
        }
    }
    return nullptr;
}

const Albums::Album *Albums::find(const QString &id) const
{
    return const_cast<Albums *>(this)->find(id);
}

QVariantList Albums::list() const
{
    QVariantList out;
    for (const Album &album : m_albums) {
        out.append(QVariantMap{
            {QStringLiteral("id"), album.id},
            {QStringLiteral("name"), album.name},
            {QStringLiteral("count"), int(album.paths.size())},
            {QStringLiteral("cover"), album.paths.isEmpty() ? QString() : album.paths.first()},
        });
    }
    return out;
}

void Albums::toggleFavorite(const QString &path)
{
    setFavorite({path}, !isFavorite(path));
}

void Albums::setFavorite(const QStringList &paths, bool on)
{
    bool any = false;
    for (const QString &path : paths) {
        if (path.isEmpty() || m_favoriteSet.contains(path) == on) {
            continue;
        }
        any = true;
        if (on) {
            m_favorites.prepend(path);
            m_favoriteSet.insert(path);
        } else {
            m_favorites.removeAll(path);
            m_favoriteSet.remove(path);
        }
    }
    if (any) {
        touched();
    }
}

bool Albums::allFavorite(const QStringList &paths) const
{
    if (paths.isEmpty()) {
        return false;
    }
    for (const QString &path : paths) {
        if (!m_favoriteSet.contains(path)) {
            return false;
        }
    }
    return true;
}

QString Albums::create(const QString &name, const QStringList &paths)
{
    Album album;
    album.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    album.name = name.trimmed();
    for (const QString &path : paths) {
        if (!album.paths.contains(path)) {
            album.paths << path;
        }
    }
    m_albums << album;
    touched();
    return album.id;
}

void Albums::rename(const QString &id, const QString &name)
{
    Album *album = find(id);
    if (!album || name.trimmed().isEmpty() || album->name == name.trimmed()) {
        return;
    }
    album->name = name.trimmed();
    touched();
}

void Albums::remove(const QString &id)
{
    for (int i = 0; i < m_albums.size(); ++i) {
        if (m_albums.at(i).id == id) {
            m_albums.removeAt(i);
            touched();
            return;
        }
    }
}

int Albums::add(const QString &id, const QStringList &paths)
{
    Album *album = find(id);
    if (!album) {
        return 0;
    }
    QSet<QString> have(album->paths.begin(), album->paths.end());
    int added = 0;
    for (const QString &path : paths) {
        if (!path.isEmpty() && !have.contains(path)) {
            have.insert(path);
            album->paths << path;
            ++added;
        }
    }
    if (added > 0) {
        touched();
    }
    return added;
}

void Albums::take(const QString &id, const QStringList &paths)
{
    Album *album = find(id);
    if (!album) {
        return;
    }
    const QSet<QString> gone(paths.begin(), paths.end());
    const qsizetype before = album->paths.size();
    album->paths.removeIf([&gone](const QString &path) {
        return gone.contains(path);
    });
    if (album->paths.size() != before) {
        touched();
    }
}

QString Albums::name(const QString &id) const
{
    const Album *album = find(id);
    return album ? album->name : QString();
}

QStringList Albums::paths(const QString &id) const
{
    const Album *album = find(id);
    return album ? album->paths : QStringList();
}

void Albums::forget(const QStringList &paths)
{
    const QSet<QString> gone(paths.begin(), paths.end());
    const auto isGone = [&gone](const QString &path) {
        return gone.contains(path);
    };
    bool any = m_favorites.removeIf(isGone) > 0;
    m_favoriteSet.subtract(gone);
    for (Album &album : m_albums) {
        any = album.paths.removeIf(isGone) > 0 || any;
    }
    if (any) {
        touched();
    }
}
