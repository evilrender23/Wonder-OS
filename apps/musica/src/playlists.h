#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

/// The user's playlists, and «Favoritas», the one that is always there.
class Playlists : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.playlists")
    /// {id, name, count, cover} of every playlist but Favoritas, in the order they were made.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    /// Changes whenever any playlist's tracks do: what a binding on isFavorite() depends on.
    Q_PROPERTY(int revision READ revision NOTIFY tracksChanged)

public:
    static QString favorites() { return QStringLiteral("favorites"); }

    explicit Playlists(const QString &path, QObject *parent = nullptr);

    QVariantList list() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE QString create(const QString &name, const QStringList &paths = {});
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void add(const QString &id, const QStringList &paths);
    Q_INVOKABLE void removeAt(const QString &id, int index);
    Q_INVOKABLE QString name(const QString &id) const;
    Q_INVOKABLE QStringList paths(const QString &id) const;
    Q_INVOKABLE bool isFavorite(const QString &path) const;
    Q_INVOKABLE void toggleFavorite(const QString &path);

Q_SIGNALS:
    void listChanged();
    void tracksChanged(const QString &id);

private:
    struct Playlist {
        QString id;
        QString name;
        QStringList paths;
    };
    Playlist *find(const QString &id);
    const Playlist *find(const QString &id) const;
    void touched(const QString &id);
    void save() const;

    QString m_path;
    QList<Playlist> m_lists;
    QStringList m_favorites;
    int m_revision = 0;
};
