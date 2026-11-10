#pragma once

#include "media.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class Store;

/// The pictures and videos of the folders the user keeps them in, newest first.
class Library : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    /// Goes up with every change: what QML binds to, to ask again.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY changed)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY changed)
    Q_PROPERTY(int screenshotCount READ screenshotCount NOTIFY changed)
    /// The folders that hold something: [{path, name, count, cover, coverStamp}].
    Q_PROPERTY(QVariantList places READ places NOTIFY changed)

public:
    /// `watch` off: the library only changes when told to (tests).
    Library(Store *store, const QString &cachePath, bool watch = true, QObject *parent = nullptr);
    ~Library() override;

    QStringList folders() const { return m_folders; }
    bool scanning() const { return m_job.isRunning(); }
    int revision() const { return m_revision; }
    int count() const { return int(m_items.size()); }
    int photoCount() const { return count() - m_videos; }
    int videoCount() const { return m_videos; }
    int screenshotCount() const { return m_screenshots; }
    QVariantList places() const;

    const QList<MediaItem> &items() const { return m_items; }
    bool contains(const QString &path) const { return m_index.contains(path); }
    /// Null (an empty path) when the library does not have it.
    MediaItem find(const QString &path) const;

    /// The folders a new user's library starts with: Pictures and Videos.
    static QStringList defaultFolders();

    Q_INVOKABLE void addFolder(const QUrl &folder);
    Q_INVOKABLE void removeFolder(const QString &folder);
    Q_INVOKABLE void rescan();
    /// Blocks until the scan under way is done (tests, and leaving).
    void waitForScan();
    Q_INVOKABLE QVariantMap item(const QString &path) const;
    /// «Imágenes», «Imágenes/Viajes»: a folder as the window names it.
    Q_INVOKABLE QString placeName(const QString &folder) const;

    /// Takes these out now, without waiting for the disk to be read again
    /// (they went to the bin).
    void forget(const QStringList &paths);
    /// Reads one file again (it was turned).
    void refresh(const QString &path);

    struct Scan {
        QList<MediaItem> items;
        QStringList dirs;
    };
    /// Reads the folders. What `known` already has, unchanged on disk, is kept as it is.
    static Scan scan(const QStringList &folders, const QHash<QString, MediaItem> &known);

Q_SIGNALS:
    void changed();
    void foldersChanged();
    void scanningChanged();
    /// One file has new contents: its pictures on screen are stale.
    void itemChanged(const QString &path);

private:
    void adopt(const Scan &scan);
    void setItems(QList<MediaItem> items);
    void save() const;

    Store *m_store;
    QString m_cachePath;
    QStringList m_folders;
    QList<MediaItem> m_items;
    QHash<QString, int> m_index;
    int m_videos = 0;
    int m_screenshots = 0;
    int m_revision = 0;
    QFutureWatcher<Scan> m_job;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer m_settle;
    bool m_again = false;
};
