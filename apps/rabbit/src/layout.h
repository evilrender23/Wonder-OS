#pragma once

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

struct FolderData;

/// What a folder remembers about how it is shown: where each icon sits, the
/// view, the icon size and the arrangement, as macOS keeps in .DS_Store.
///
/// The folder carries only an id (the `user.wonderland.folder-id` extended
/// attribute); the layout itself lives under the app's data directory, so
/// nothing is littered around the disk and a renamed or moved folder keeps
/// its layout. Folders that can't take the attribute (read-only, FAT,
/// network, virtual places) are remembered by path instead.
///
/// Positions are in content coordinates, in pixels, keyed by file name. A
/// folder with no stored positions flows in rows; the first icon the user
/// moves freezes all of them where they are.
class FolderLayout : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY storedChanged)
    Q_PROPERTY(int iconSize READ iconSize WRITE setIconSize NOTIFY storedChanged)
    /// "" (free), "grid" (free, snapping), or a sort key: "name", "kind", "modified", "size".
    Q_PROPERTY(QString arrange READ arrange WRITE setArrange NOTIFY storedChanged)
    Q_PROPERTY(bool arranged READ arranged NOTIFY storedChanged)

public:
    explicit FolderLayout(QObject *parent = nullptr);
    ~FolderLayout() override;

    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);
    QString view() const;
    void setView(const QString &view);
    int iconSize() const;
    void setIconSize(int size);
    QString arrange() const;
    void setArrange(const QString &arrange);
    /// True once the folder has positions of its own.
    bool arranged() const;

    /// Rows of `cellWidth` × `cellHeight` cells filling `width`, in the order given. Stores nothing.
    Q_INVOKABLE QVariantMap flow(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight) const;
    /// Where each name goes: its stored position, or, for a name not seen
    /// before, the first free cell, which is then remembered.
    Q_INVOKABLE QVariantMap place(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight);
    /// Stores flow() as the folder's own positions.
    Q_INVOKABLE void freeze(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight);
    /// Moves icons to the positions given (name → point). With `snap`, each lands on the nearest free cell.
    Q_INVOKABLE void moveTo(const QVariantMap &positions, bool snap, qreal cellWidth, qreal cellHeight);
    /// Keeps a place for files about to arrive (a drop): the first at `at`, the rest in free cells after it.
    Q_INVOKABLE void reserve(const QStringList &names, const QPointF &at, qreal width, qreal cellWidth, qreal cellHeight);
    /// Clean Up: every icon to its nearest free cell.
    Q_INVOKABLE void cleanUp(const QStringList &names, qreal cellWidth, qreal cellHeight);
    Q_INVOKABLE void rename(const QString &from, const QString &to);
    /// Forgets the positions: the folder flows again.
    Q_INVOKABLE void reset();
    Q_INVOKABLE void flush();

    static QString dataDirectory();
    static QString folderId(const QString &path);

Q_SIGNALS:
    void urlChanged();
    void storedChanged();
    /// Another window changed this folder's positions.
    void changed();

private:
    void release();
    void touch();
    void rescale(qreal cellWidth, qreal cellHeight);

    QUrl m_url;
    std::shared_ptr<FolderData> m_data;
    QTimer m_saveTimer;
};
