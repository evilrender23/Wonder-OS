#pragma once

#include "track.h"

#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class Store;

/// The music on disk: the folders the user chose, read once and remembered.
/// A rescan only opens the files that are new or changed.
class Library : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.library")
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QVariantList albums READ albums NOTIFY changed)
    Q_PROPERTY(QVariantList recentAlbums READ recentAlbums NOTIFY changed)
    Q_PROPERTY(QVariantList artists READ artists NOTIFY changed)

public:
    struct Scan {
        QList<Track> tracks;
        QStringList folders;
    };

    /// `watch` off: the library only changes when told to (tests).
    Library(Store *store, const QString &cachePath, bool watch = true, QObject *parent = nullptr);
    ~Library() override;

    QStringList folders() const { return m_folders; }
    bool scanning() const { return m_scanning; }
    int count() const { return int(m_tracks.size()); }
    const QList<Track> &tracks() const { return m_tracks; }
    QVariantList albums() const { return m_albums; }
    QVariantList recentAlbums() const { return m_recentAlbums; }
    QVariantList artists() const { return m_artists; }

    Q_INVOKABLE void addFolder(const QUrl &folder);
    Q_INVOKABLE void removeFolder(const QString &folder);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE QVariantMap album(const QString &key) const;
    Q_INVOKABLE QVariantList albumsOf(const QString &artist) const;
    Q_INVOKABLE QStringList albumPaths(const QString &key) const;
    Q_INVOKABLE QStringList artistPaths(const QString &artist) const;

    bool contains(const QString &path) const { return m_byPath.contains(path); }
    /// A track of the library, or — for a file opened from outside it — its tags read now.
    Track track(const QString &path) const;
    QList<Track> albumTracks(const QString &key) const;
    QList<Track> artistTracks(const QString &artist) const;

    /// Every audio file under `folders`. Files in `known` with the same date and
    /// size are taken from there instead of being opened again.
    static Scan scan(const QStringList &folders, const QHash<QString, Track> &known);
    /// Artist, then album (oldest first), disc and track number.
    static void sort(QList<Track> &tracks);

Q_SIGNALS:
    void foldersChanged();
    void scanningChanged();
    void changed();

private:
    void setTracks(const QList<Track> &tracks);
    void loadCache();
    void saveCache() const;
    void finishScan();

    Store *m_store;
    QString m_cachePath;
    QStringList m_folders;
    QList<Track> m_tracks;
    QHash<QString, int> m_byPath;
    mutable QHash<QString, Track> m_outside;
    QVariantList m_albums;
    QVariantList m_recentAlbums;
    QVariantList m_artists;
    QFutureWatcher<Scan> m_job;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_settle;
    bool m_scanning = false;
    bool m_again = false;
};
