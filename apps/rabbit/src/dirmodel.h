#pragma once

#include <QAbstractListModel>
#include <QCollator>
#include <QDateTime>
#include <QSet>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class KCoreDirLister;
class KFileItem;
class KFileItemList;

struct Entry {
    QUrl url;
    QString name;
    QString path;
    QString icon;
    QString kind;
    QString mime;
    QString where;
    QStringList tags;
    QDateTime modified;
    quint64 size = 0;
    quint64 order = 0;
    bool isDir = false;
    bool isLink = false;
    bool isHidden = false;
    bool isApp = false;
    bool thumb = false;
    bool selected = false;

    static Entry fromItem(const KFileItem &item);
    static Entry fromPath(const QString &path);
};

/// One folder's contents, sorted and with its selection. `url` is a KIO URL,
/// or one of Rabbit's own places: recents:/, applications:/, tags:/<name>.
/// A non-empty `search` lists matching names below `url` instead.
class DirModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortChanged)
    Q_PROPERTY(bool ascending READ ascending WRITE setAscending NOTIFY sortChanged)
    Q_PROPERTY(bool foldersFirst READ foldersFirst WRITE setFoldersFirst NOTIFY sortChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)

public:
    enum SortKey { ByName, ByKind, ByModified, BySize, ByNone };
    Q_ENUM(SortKey)

    enum Role {
        NameRole = Qt::UserRole + 1,
        UrlRole,
        PathRole,
        IsDirRole,
        IsLinkRole,
        IsHiddenRole,
        IsAppRole,
        IconRole,
        KindRole,
        MimeRole,
        SizeRole,
        SizeTextRole,
        ModifiedRole,
        ModifiedTextRole,
        SelectedRole,
        TagsRole,
        ThumbRole,
        WhereRole,
        FileNameRole,
    };

    explicit DirModel(QObject *parent = nullptr);
    ~DirModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QUrl url() const { return m_url; }
    void setUrl(const QUrl &url);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    SortKey sortKey() const { return m_sortKey; }
    void setSortKey(SortKey key);
    bool ascending() const { return m_ascending; }
    void setAscending(bool ascending);
    bool foldersFirst() const { return m_foldersFirst; }
    void setFoldersFirst(bool foldersFirst);
    bool showHidden() const { return m_showHidden; }
    void setShowHidden(bool show);
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    int count() const { return m_entries.size(); }
    int selectedCount() const { return m_selectedCount; }
    int currentRow() const { return m_currentRow; }
    void setCurrentRow(int row);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int rowForUrl(const QUrl &url) const;
    /// Each row's name on disk, in row order: what a folder's icon layout is keyed by.
    Q_INVOKABLE QStringList fileNames() const;
    Q_INVOKABLE int rowForFileName(const QString &name) const;
    Q_INVOKABLE int typeAhead(const QString &prefix) const;

    /// A click on `row` with the keyboard modifiers held: plain selects it
    /// alone, Ctrl toggles it, Shift extends from the anchor.
    Q_INVOKABLE void click(int row, int modifiers);
    Q_INVOKABLE void selectOnly(int row);
    Q_INVOKABLE void extendTo(int row);
    Q_INVOKABLE void selectRows(const QList<int> &rows, const QList<int> &keep = {});
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int row) const;
    Q_INVOKABLE QList<int> selectedRows() const;
    Q_INVOKABLE QList<QUrl> selectedUrls() const;
    Q_INVOKABLE quint64 selectedSize() const;
    /// Select these URLs now, or as soon as the listing delivers them.
    Q_INVOKABLE void selectUrls(const QList<QUrl> &urls);

Q_SIGNALS:
    void urlChanged();
    void searchChanged();
    void sortChanged();
    void showHiddenChanged();
    void loadingChanged();
    void errorChanged();
    void countChanged();
    void selectionChanged();
    void currentRowChanged();
    /// A URL asked for through selectUrls() has just appeared at `row`.
    void revealed(int row);
    /// An item changed its name on disk.
    void renamed(const QString &from, const QString &to);

private:
    void reload();
    void stopSearch();
    void startSearch();
    void loadStatic();
    void setLoading(bool loading);
    void setError(const QString &error);
    void clearEntries();
    void addEntries(QList<Entry> entries);
    void removeUrl(const QUrl &url);
    void resort();
    bool lessThan(const Entry &a, const Entry &b) const;
    void setSelected(int row, bool selected);
    void applyPendingSelection(int row);
    bool isVirtual() const;

    QUrl m_url;
    QString m_search;
    SortKey m_sortKey = ByName;
    bool m_ascending = true;
    bool m_foldersFirst = false;
    bool m_showHidden = false;
    bool m_loading = false;
    QString m_error;
    QList<Entry> m_entries;
    int m_selectedCount = 0;
    int m_currentRow = -1;
    int m_anchorRow = -1;
    quint64 m_nextOrder = 0;
    QSet<QUrl> m_pendingSelection;
    bool m_pendingFirst = true;
    QCollator m_collator;
    KCoreDirLister *m_lister = nullptr;
    std::shared_ptr<std::atomic_bool> m_searchCancel;
    quint64 m_generation = 0;
};
