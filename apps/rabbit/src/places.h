#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

/// The sidebar: Favourites, Locations (mounted and mountable volumes, from
/// Solid) and Tags.
class Places : public QAbstractListModel
{
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        UrlRole,
        SymbolRole,
        SectionRole,
        KindRole,
        ColorRole,
        EjectableRole,
        MountedRole,
        UdiRole,
        RemovableRole,
    };

    explicit Places(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int rowForUrl(const QUrl &url) const;
    Q_INVOKABLE bool isFavorite(const QUrl &url) const;
    Q_INVOKABLE void addFavorite(const QUrl &url);
    Q_INVOKABLE void removeFavorite(const QUrl &url);
    /// Mounts the volume if needed; mounted() follows with where it landed.
    Q_INVOKABLE void mount(const QString &udi);
    Q_INVOKABLE void eject(const QString &udi);
    Q_INVOKABLE QString titleFor(const QUrl &url) const;
    Q_INVOKABLE QString symbolFor(const QUrl &url) const;
    /// Where the system keeps its own partitions (/boot, /var…): mounted there,
    /// a volume is not a disk the user browses.
    static bool isSystemMountPoint(const QString &path);
    /// The address a folder is shown under: the folder the trash is kept in is the Trash itself.
    Q_INVOKABLE static QUrl canonical(const QUrl &url);

Q_SIGNALS:
    void mounted(const QUrl &url);
    void ejected(const QUrl &url);
    void failed(const QString &message);

private:
    struct Place {
        QString title;
        QUrl url;
        QString symbol;
        QString section;
        QString kind;
        QString color;
        QString udi;
        bool ejectable = false;
        bool mounted = true;
        bool removable = false;
    };

    void rebuild();
    QList<Place> favorites() const;
    QList<Place> volumes();
    QStringList storedFavorites() const;
    void storeFavorites(const QStringList &paths);

    QList<Place> m_places;
    QSet<QString> m_watched;
};
