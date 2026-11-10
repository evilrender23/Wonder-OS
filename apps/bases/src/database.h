#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

/// What a statement gave back.
struct Result {
    bool ok = false;
    QString error;
    QStringList columns;
    QList<QVariantList> rows;
    qint64 affected = 0;
    QVariant lastId;
    /// There were more rows than the limit asked for.
    bool truncated = false;
};

/// One SQLite file: its schema, its rows and the objects Bases keeps in it
/// (saved queries, forms, reports) in tables named `_wonderland_*`.
///
/// Identifiers are always quoted and values always bound: nothing a person
/// types reaches SQLite as part of a statement, except in run(), which is
/// where they type SQL on purpose.
class Database : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.db")
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY opened)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY opened)
    Q_PROPERTY(QString path READ path NOTIFY opened)
    Q_PROPERTY(QString name READ name NOTIFY opened)
    /// Goes up when tables, columns, views or indexes change.
    Q_PROPERTY(int schemaRevision READ schemaRevision NOTIFY schemaChanged)
    /// Goes up when rows change.
    Q_PROPERTY(int dataRevision READ dataRevision NOTIFY dataChanged)
    /// Goes up when saved queries, forms or reports change.
    Q_PROPERTY(int objectsRevision READ objectsRevision NOTIFY objectsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY failed)

public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    /// Opens a file that must already be an SQLite database.
    Q_INVOKABLE bool open(const QString &path);
    /// Makes an empty database, replacing any file at `path`.
    Q_INVOKABLE bool create(const QString &path);
    Q_INVOKABLE void close();

    bool isOpen() const { return m_open; }
    bool readOnly() const { return m_readOnly; }
    QString path() const { return m_path; }
    QString name() const;
    int schemaRevision() const { return m_schemaRevision; }
    int dataRevision() const { return m_dataRevision; }
    int objectsRevision() const { return m_objectsRevision; }
    QString lastError() const { return m_lastError; }

    // --- Statements -----------------------------------------------------------

    /// One statement with positional values. `limit` < 0 reads every row.
    Result query(const QString &sql, const QVariantList &binds = {}, int limit = -1);
    /// One statement with named values (`:name`).
    Result queryNamed(const QString &sql, const QVariantMap &binds, int limit = -1);
    bool exec(const QString &sql, const QVariantList &binds = {});
    bool begin();
    bool commit();
    void rollback();

    /// What a person typed: one or several statements, run in order until one
    /// fails. {ok, error, statement (the one that failed), columns, rows,
    /// affected, truncated, statements (how many ran), elapsed (ms)} — the
    /// columns and rows are those of the last statement that returned any.
    Q_INVOKABLE QVariantMap run(const QString &script, const QVariantMap &binds = {}, int limit = 1000);
    /// The schema or the rows changed behind the model's back (a script ran).
    void touch(bool schema);

    // --- Schema ---------------------------------------------------------------

    /// [{name, type: table|view}], without SQLite's and Bases' own.
    Q_INVOKABLE QVariantList tables() const;
    Q_INVOKABLE QStringList tableNames(bool withViews = false) const;
    Q_INVOKABLE bool exists(const QString &table) const;
    Q_INVOKABLE bool isView(const QString &table) const;
    /// [{name, type, kind, notNull, defaultValue, pk (0, or its place in the
    /// key), unique, autoIncrement, generated, fkTable, fkColumn, fkOnDelete,
    /// fkOnUpdate}]
    Q_INVOKABLE QVariantList columns(const QString &table) const;
    Q_INVOKABLE QStringList columnNames(const QString &table) const;
    /// [{id, columns, table, refColumns, onUpdate, onDelete}]
    Q_INVOKABLE QVariantList foreignKeys(const QString &table) const;
    /// [{name, unique, columns, origin (c: made by hand, u: UNIQUE, pk), sql}]
    Q_INVOKABLE QVariantList indexes(const QString &table) const;
    /// Every single-column foreign key: [{table, column, refTable, refColumn}]
    Q_INVOKABLE QVariantList relations() const;
    /// The CREATE statement of a table, view, index or trigger.
    Q_INVOKABLE QString sqlOf(const QString &name) const;
    Q_INVOKABLE qint64 rowCount(const QString &table) const;
    /// table → [column names], for the editor's completion.
    Q_INVOKABLE QVariantMap schema() const;
    bool withoutRowid(const QString &table) const;
    /// The columns that tell one row from another: the primary key of a
    /// WITHOUT ROWID table, else a name for the rowid no column hides. Empty
    /// for a view, whose rows cannot be edited.
    QStringList keyColumns(const QString &table) const;
    /// The column that is the rowid under another name (INTEGER PRIMARY KEY).
    QString rowidAlias(const QString &table) const;
    /// The values a foreign key may take: [{value, label}], the label being
    /// the first text column of the table referred to.
    Q_INVOKABLE QVariantList lookup(const QString &table, const QString &column, int limit = 500) const;

    /// What a record is tied to by foreign keys, both ways: the record each of
    /// its keys refers to («parent») and the records of other tables that
    /// refer to it («children»): [{direction, table, column, refColumn, value,
    /// count, rows: [{label, record}]}], no more than `limit` rows each.
    Q_INVOKABLE QVariantList related(const QString &table, const QVariantMap &record, int limit = 5) const;
    /// What names a record for a person: its first text column, else its key.
    Q_INVOKABLE QString labelColumn(const QString &table) const;

    // --- Rows, with undo ---------------------------------------------------------

    /// What was typed, as the value a column of that kind stores.
    static QVariant coerce(const QString &kind, const QVariant &value);
    bool updateCell(const QString &table, const QVariantList &key, const QString &column, const QVariant &value, QVariantList *newKey = nullptr);
    /// The key of the new row; empty when it could not be inserted.
    QVariantList insertRow(const QString &table, const QVariantMap &values);
    bool deleteRows(const QString &table, const QList<QVariantList> &keys);
    /// A row by its key, as column → value.
    QVariantMap row(const QString &table, const QVariantList &key) const;

    bool canUndo() const { return m_undoAt > 0; }
    bool canRedo() const { return m_undoAt < m_changes.size(); }
    QString undoText() const;
    QString redoText() const;
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();

    // --- Bases' own objects ---------------------------------------------------------

    /// [{id, name, definition}] of one kind (query, form, report, layout).
    Q_INVOKABLE QVariantList objects(const QString &kind) const;
    Q_INVOKABLE QVariantMap object(int id) const;
    Q_INVOKABLE QVariantMap objectNamed(const QString &kind, const QString &name) const;
    /// Saves a new object (`id` 0) or replaces one; 0 when it could not.
    Q_INVOKABLE int saveObject(const QString &kind, const QString &name, const QVariantMap &definition, int id = 0);
    Q_INVOKABLE bool renameObject(int id, const QString &name);
    Q_INVOKABLE bool removeObject(int id);
    Q_INVOKABLE void addHistory(const QString &sql, bool ok);
    /// The latest first: [{sql, ran, ok}]
    Q_INVOKABLE QVariantList history(int limit = 50) const;
    Q_INVOKABLE void clearHistory();

Q_SIGNALS:
    void opened();
    void schemaChanged();
    void dataChanged();
    void objectsChanged();
    void undoChanged();
    void failed();

private:
    struct Change {
        enum Type { Update, Insert, Delete } type = Update;
        QString table;
        QString text;
        // Update
        QVariantList key;
        QVariantList keyAfter;
        QString column;
        QVariant before;
        QVariant after;
        // Insert and Delete: whole rows, with their keys.
        QStringList keyColumns;
        QStringList columns;
        QList<QVariantList> keys;
        QList<QVariantList> rows;
    };

    bool attach(const QString &path, bool mustExist);
    bool fail(const QString &message) const;
    QString whereKey(const QStringList &keyColumns) const;
    bool ensureMeta();
    bool hasMeta(const QString &table) const;
    void push(const Change &change);
    bool apply(const Change &change, bool forward);
    bool removeKeys(const Change &change);
    bool restoreRows(const Change &change);
    void clearUndo();
    QStringList storedColumns(const QString &table) const;

    QString m_connection;
    QString m_path;
    bool m_open = false;
    bool m_readOnly = false;
    int m_schemaRevision = 0;
    int m_dataRevision = 0;
    int m_objectsRevision = 0;
    mutable QString m_lastError;
    QList<Change> m_changes;
    int m_undoAt = 0;
};
