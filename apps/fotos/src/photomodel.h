#pragma once

#include "albums.h"
#include "library.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>

/// What a page of the window shows, laid out for a list that draws only what
/// is on screen: a row is either the heading of a group («Enero de 2024») or
/// one line of up to `columns` pictures.
class PhotoModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Library *library READ library WRITE setLibrary NOTIFY sourceChanged)
    Q_PROPERTY(Albums *albums READ albums WRITE setAlbums NOTIFY sourceChanged)
    /// library, favorites, videos, screenshots, folder (key: its path), album (key: its id).
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY sourceChanged)
    Q_PROPERTY(QString key READ key WRITE setKey NOTIFY sourceChanged)
    /// Narrows what is shown to what matches these words.
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY sourceChanged)
    /// year, month, day or none. Albums keep their own order and are never grouped.
    Q_PROPERTY(QString grouping READ grouping WRITE setGrouping NOTIFY sourceChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    /// The pictures, in the order shown.
    Q_PROPERTY(QStringList paths READ paths NOTIFY contentChanged)
    Q_PROPERTY(int count READ count NOTIFY contentChanged)
    Q_PROPERTY(int photoCount READ photoCount NOTIFY contentChanged)
    Q_PROPERTY(int videoCount READ videoCount NOTIFY contentChanged)

public:
    enum Roles { HeaderRole = Qt::UserRole + 1, TitleRole, DetailRole, CellsRole, FirstRole };

    explicit PhotoModel(QObject *parent = nullptr);

    Library *library() const { return m_library; }
    void setLibrary(Library *library);
    Albums *albums() const { return m_albums; }
    void setAlbums(Albums *albums);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString key() const { return m_key; }
    void setKey(const QString &key);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString grouping() const { return m_grouping; }
    void setGrouping(const QString &grouping);
    int columns() const { return m_columns; }
    void setColumns(int columns);
    QStringList paths() const { return m_paths; }
    int count() const { return int(m_items.size()); }
    int photoCount() const { return count() - m_videos; }
    int videoCount() const { return m_videos; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// The row of the list the picture at `index` (of `paths`) is in, -1 when none.
    Q_INVOKABLE int rowOf(int index) const;
    Q_INVOKABLE int indexOf(const QString &path) const { return int(m_paths.indexOf(path)); }
    /// The paths from one picture to another, both included, whichever comes first.
    Q_INVOKABLE QStringList range(int from, int to) const;
    /// The paths of the group whose heading is at `row`.
    Q_INVOKABLE QStringList group(int row) const;

    /// What the pictures of a period are called: «2024», «Enero de 2024», «31 de enero de 2024».
    static QString title(const QDateTime &when, const QString &grouping);
    /// Whether an item matches words already folded (Media::fold).
    static bool matches(const MediaItem &item, const QStringList &words, const QString &place);

Q_SIGNALS:
    void sourceChanged();
    void columnsChanged();
    void contentChanged();

private:
    struct Row {
        bool header = false;
        QString title;
        QString detail;
        /// Header: its group's first picture and how many it has. Line: its first picture and how many.
        int first = 0;
        int count = 0;
    };
    void reload();
    void layout();
    QVariantMap cell(int index) const;

    Library *m_library = nullptr;
    Albums *m_albums = nullptr;
    QString m_mode = QStringLiteral("library");
    QString m_key;
    QString m_query;
    QString m_grouping = QStringLiteral("month");
    int m_columns = 5;
    QList<MediaItem> m_items;
    QStringList m_paths;
    int m_videos = 0;
    QList<Row> m_rows;
};
