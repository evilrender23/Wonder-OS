#pragma once

#include "database.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

/// The rows of a table, a view or a query, for a grid that only ever asks for
/// what is on screen: rows are read a page at a time and a few pages are kept.
/// Sorting, the filters of each column and the search are done by SQLite.
class GridModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Database *database READ database WRITE setDatabase NOTIFY sourceChanged)
    /// A table or a view…
    Q_PROPERTY(QString table READ table WRITE setTable NOTIFY sourceChanged)
    /// …or one statement, with the values of its named parameters.
    Q_PROPERTY(QString sql READ sql WRITE setSql NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters WRITE setParameters NOTIFY sourceChanged)
    /// -1 is the order of the table itself.
    Q_PROPERTY(int sortColumn READ sortColumn WRITE setSortColumn NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY viewChanged)
    /// column name → what was typed under its header (see Sql::criterion).
    Q_PROPERTY(QVariantMap filters READ filters WRITE setFilters NOTIFY viewChanged)
    /// A chain of filters, all of which a row must meet: [{column, op, value}]
    /// (see Sql::condition).
    Q_PROPERTY(QVariantList conditions READ conditions WRITE setConditions NOTIFY viewChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY viewChanged)
    /// Only the rows whose `linkColumn` is `linkValue`: a subform.
    Q_PROPERTY(QString linkColumn READ linkColumn WRITE setLinkColumn NOTIFY viewChanged)
    Q_PROPERTY(QVariant linkValue READ linkValue WRITE setLinkValue NOTIFY viewChanged)
    Q_PROPERTY(bool filtered READ filtered NOTIFY viewChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// The rows there are without filters; -1 for a query.
    Q_PROPERTY(qint64 total READ total NOTIFY countChanged)
    /// [{name, type, kind, pk, notNull, fkTable, fkColumn, …}]
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(bool editable READ editable NOTIFY fieldsChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    enum Roles { ValueRole = Qt::UserRole + 1, NullRole, KindRole };

    explicit GridModel(QObject *parent = nullptr);

    Database *database() const { return m_db; }
    void setDatabase(Database *db);
    QString table() const { return m_table; }
    void setTable(const QString &table);
    QString sql() const { return m_sql; }
    void setSql(const QString &sql);
    QVariantMap parameters() const { return m_parameters; }
    void setParameters(const QVariantMap &parameters);
    int sortColumn() const { return m_sortColumn; }
    void setSortColumn(int column);
    bool sortDescending() const { return m_sortDescending; }
    void setSortDescending(bool descending);
    QVariantMap filters() const { return m_filters; }
    void setFilters(const QVariantMap &filters);
    QVariantList conditions() const { return m_conditions; }
    void setConditions(const QVariantList &conditions);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    QString linkColumn() const { return m_linkColumn; }
    void setLinkColumn(const QString &column);
    QVariant linkValue() const { return m_linkValue; }
    void setLinkValue(const QVariant &value);
    bool filtered() const;
    int count() const { return m_count; }
    qint64 total() const { return m_total; }
    QVariantList fields() const { return m_fields; }
    bool editable() const { return !m_keyColumns.isEmpty() && m_db && !m_db->readOnly(); }
    QString error() const { return m_error; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// How a value reads in a cell.
    Q_INVOKABLE static QString display(const QVariant &value);
    Q_INVOKABLE QVariant value(int row, int column) const;
    Q_INVOKABLE QString text(int row, int column) const;
    /// column name → value, of one row.
    Q_INVOKABLE QVariantMap record(int row) const;
    /// Stores what was typed, as the kind of the column wants it.
    Q_INVOKABLE bool setValue(int row, int column, const QVariant &value);
    Q_INVOKABLE bool setNull(int row, int column);
    /// What tells the row apart in its table; empty when it cannot be edited.
    Q_INVOKABLE QVariantList keyAt(int row) const;
    /// The same as setValue() for a row that may not be in sight, by its key.
    Q_INVOKABLE bool store(const QVariantList &key, const QString &column, const QVariant &value);
    /// The rows in sight gathered by what one column holds, for a board:
    /// [{value, label, count, rows: [{key, record}]}], no more than `perGroup`
    /// rows each. A foreign key is labelled with the text of what it refers to.
    Q_INVOKABLE QVariantList groups(const QString &column, int perGroup = 50) const;
    /// The words of the operators a chain of filters takes.
    Q_INVOKABLE static QVariantList operators();
    /// The place of the new row in the grid; -1 when it was added but is out
    /// of sight (filtered out, or sorted somewhere), -2 when it failed.
    Q_INVOKABLE int insert(const QVariantMap &values);
    Q_INVOKABLE bool remove(const QVariantList &rows);
    /// The first row from `from` on where some column holds `text`, or -1.
    Q_INVOKABLE int find(const QString &text, int from, bool backwards = false) const;
    Q_INVOKABLE void sortBy(int column);
    Q_INVOKABLE void setFilter(const QString &column, const QString &typed);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void refresh();
    /// A width that suits the name and the kind of a column.
    Q_INVOKABLE int widthHint(int column) const;
    /// The statement behind the grid, with what it binds written in: to export what is in sight.
    Q_INVOKABLE QString viewSql() const;

Q_SIGNALS:
    void sourceChanged();
    void viewChanged();
    void countChanged();
    void fieldsChanged();
    void errorChanged();

private:
    void reload();
    void recount();
    void setError(const QString &error);
    QString source() const;
    QString where(QVariantList *binds) const;
    QStringList conditionTerms(QVariantList *binds) const;
    QString order() const;
    const QVariantList *rowAt(int row) const;
    QVariantList keyOf(const QVariantList &row) const;
    void external();

    QPointer<Database> m_db;
    QString m_table;
    QString m_sql;
    QVariantMap m_parameters;
    int m_sortColumn = -1;
    bool m_sortDescending = false;
    QVariantMap m_filters;
    QVariantList m_conditions;
    QString m_search;
    QString m_linkColumn;
    QVariant m_linkValue;
    int m_count = 0;
    qint64 m_total = -1;
    QVariantList m_fields;
    QStringList m_names;
    QStringList m_kinds;
    QStringList m_keyColumns;
    QString m_error;
    /// A statement that cannot be paged (PRAGMA, EXPLAIN): read whole, once.
    bool m_static = false;
    QList<QVariantList> m_staticRows;
    bool m_own = false;
    static constexpr int pageSize = 200;
    mutable QHash<int, QList<QVariantList>> m_pages;
    mutable QList<int> m_recent;
};
