#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// The favourites and the albums the user makes: lists of files, which stay
/// where they are on disk.
class Albums : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    /// [{id, name, count, cover}], in the order they were made.
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY changed)

public:
    explicit Albums(const QString &path, QObject *parent = nullptr);

    QVariantList list() const;
    int revision() const { return m_revision; }
    int favoriteCount() const { return int(m_favorites.size()); }

    Q_INVOKABLE bool isFavorite(const QString &path) const { return m_favoriteSet.contains(path); }
    Q_INVOKABLE void toggleFavorite(const QString &path);
    /// All of them on, or all of them off.
    Q_INVOKABLE void setFavorite(const QStringList &paths, bool on);
    /// Whether every one of these is a favourite.
    Q_INVOKABLE bool allFavorite(const QStringList &paths) const;
    /// Newest favourite first.
    QStringList favorites() const { return m_favorites; }

    /// Returns the id of the new album.
    Q_INVOKABLE QString create(const QString &name, const QStringList &paths = {});
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void remove(const QString &id);
    /// Returns how many were not already in it.
    Q_INVOKABLE int add(const QString &id, const QStringList &paths);
    Q_INVOKABLE void take(const QString &id, const QStringList &paths);
    /// "" when there is no such album.
    Q_INVOKABLE QString name(const QString &id) const;
    Q_INVOKABLE QStringList paths(const QString &id) const;

    /// These files are gone for good: no list keeps them.
    void forget(const QStringList &paths);

Q_SIGNALS:
    void changed();

private:
    struct Album {
        QString id;
        QString name;
        QStringList paths;
    };
    Album *find(const QString &id);
    const Album *find(const QString &id) const;
    void touched();

    QString m_path;
    QStringList m_favorites;
    QSet<QString> m_favoriteSet;
    QList<Album> m_albums;
    int m_revision = 0;
};
