#include "database.h"

#include "sqlutil.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

static const QString objectsTable = QStringLiteral("_wonderland_objects");
static const QString historyTable = QStringLiteral("_wonderland_history");

Database::Database(QObject *parent)
    : QObject(parent)
    , m_connection(QStringLiteral("bases-") + QUuid::createUuid().toString(QUuid::Id128))
{
}

Database::~Database()
{
    close();
}

QString Database::name() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).completeBaseName();
}

bool Database::fail(const QString &message) const
{
    m_lastError = message;
    Q_EMIT const_cast<Database *>(this)->failed();
    return false;
}

bool Database::open(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return fail(QStringLiteral("No existe el archivo «%1».").arg(info.fileName()));
    }
    return attach(info.absoluteFilePath(), true);
}

bool Database::create(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists() && !QFile::remove(info.absoluteFilePath())) {
        return fail(QStringLiteral("No se puede reemplazar «%1».").arg(info.fileName()));
    }
    QFile::remove(info.absoluteFilePath() + QStringLiteral("-journal"));
    QFile::remove(info.absoluteFilePath() + QStringLiteral("-wal"));
    return attach(info.absoluteFilePath(), false);
}

bool Database::attach(const QString &path, bool mustExist)
{
    close();
    const bool writable = !mustExist || QFileInfo(path).isWritable();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        db.setDatabaseName(path);
        if (!writable) {
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        }
        if (!db.open()) {
            const QString why = db.lastError().text();
            db = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_connection);
            return fail(QStringLiteral("No se puede abrir «%1»: %2").arg(QFileInfo(path).fileName(), why));
        }
    }
    m_open = true;
    m_path = path;
    m_readOnly = !writable;
    // Reading the schema is what tells a database from any other file.
    const Result check = query(QStringLiteral("SELECT count(*) FROM sqlite_schema"));
    if (!check.ok) {
        const QString file = QFileInfo(path).fileName();
        close();
        return fail(QStringLiteral("«%1» no es una base de datos SQLite.").arg(file));
    }
    exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    if (!mustExist) {
        // An empty file is not yet a database on disk: write its header.
        exec(QStringLiteral("PRAGMA user_version = 1"));
    }
    m_lastError.clear();
    ++m_schemaRevision;
    ++m_dataRevision;
    ++m_objectsRevision;
    Q_EMIT opened();
    Q_EMIT schemaChanged();
    Q_EMIT dataChanged();
    Q_EMIT objectsChanged();
    return true;
}

void Database::close()
{
    if (!m_open) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_open = false;
    m_path.clear();
    m_readOnly = false;
    clearUndo();
    ++m_schemaRevision;
    ++m_objectsRevision;
    Q_EMIT opened();
    Q_EMIT schemaChanged();
    Q_EMIT objectsChanged();
}

// --- Statements ---------------------------------------------------------------

static QVariant plain(const QVariant &value)
{
    // A NULL comes typed after its column; every NULL is the same here.
    return value.isNull() ? QVariant() : value;
}

static void read(QSqlQuery &q, Result &result, int limit)
{
    const QSqlRecord record = q.record();
    const int n = record.count();
    for (int i = 0; i < n; ++i) {
        result.columns.append(record.fieldName(i));
    }
    while (q.next()) {
        if (limit >= 0 && result.rows.size() >= limit) {
            result.truncated = true;
            break;
        }
        QVariantList row;
        row.reserve(n);
        for (int i = 0; i < n; ++i) {
            row.append(plain(q.value(i)));
        }
        result.rows.append(row);
    }
    result.affected = q.isSelect() ? 0 : q.numRowsAffected();
    result.lastId = q.lastInsertId();
    q.finish();
}

Result Database::query(const QString &sql, const QVariantList &binds, int limit)
{
    Result result;
    if (!m_open) {
        result.error = QStringLiteral("No hay ninguna base de datos abierta.");
        return result;
    }
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        result.error = q.lastError().databaseText().isEmpty() ? q.lastError().text() : q.lastError().databaseText();
        return result;
    }
    for (const QVariant &value : binds) {
        q.addBindValue(value);
    }
    if (!q.exec()) {
        result.error = q.lastError().databaseText().isEmpty() ? q.lastError().text() : q.lastError().databaseText();
        return result;
    }
    result.ok = true;
    read(q, result, limit);
    return result;
}

Result Database::queryNamed(const QString &sql, const QVariantMap &binds, int limit)
{
    Result result;
    if (!m_open) {
        result.error = QStringLiteral("No hay ninguna base de datos abierta.");
        return result;
    }
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    q.setForwardOnly(true);
    if (!q.prepare(sql)) {
        result.error = q.lastError().databaseText().isEmpty() ? q.lastError().text() : q.lastError().databaseText();
        return result;
    }
    for (const QString &name : Sql::parameters(sql)) {
        // A value for «nombre» fills «:nombre».
        QVariant value = binds.value(name);
        if (!binds.contains(name)) {
            value = binds.value(name.mid(1));
        }
        q.bindValue(name, value);
    }
    if (!q.exec()) {
        result.error = q.lastError().databaseText().isEmpty() ? q.lastError().text() : q.lastError().databaseText();
        return result;
    }
    result.ok = true;
    read(q, result, limit);
    return result;
}

bool Database::exec(const QString &sql, const QVariantList &binds)
{
    const Result result = query(sql, binds, 0);
    if (!result.ok) {
        return fail(result.error);
    }
    return true;
}

bool Database::begin()
{
    return exec(QStringLiteral("BEGIN IMMEDIATE"));
}

bool Database::commit()
{
    return exec(QStringLiteral("COMMIT"));
}

void Database::rollback()
{
    query(QStringLiteral("ROLLBACK"));
}

void Database::touch(bool schema)
{
    if (schema) {
        clearUndo();
        ++m_schemaRevision;
        Q_EMIT schemaChanged();
    }
    ++m_dataRevision;
    Q_EMIT dataChanged();
}

QVariantMap Database::run(const QString &script, const QVariantMap &binds, int limit)
{
    QElapsedTimer timer;
    timer.start();
    QVariantMap out;
    const QStringList statements = Sql::split(script);
    Result shown;
    bool any = false;
    bool schema = false;
    bool wrote = false;
    qint64 affected = 0;
    int ran = 0;
    QString error;
    QString failedAt;
    for (const QString &statement : statements) {
        const Result result = queryNamed(statement, binds, limit);
        if (!result.ok) {
            error = result.error;
            failedAt = statement;
            break;
        }
        ++ran;
        const QString verb = Sql::verb(statement);
        if (!result.columns.isEmpty()) {
            shown = result;
            any = true;
        }
        if (verb == QLatin1String("CREATE") || verb == QLatin1String("DROP") || verb == QLatin1String("ALTER")) {
            schema = true;
        } else if (!Sql::selects(statement) && verb != QLatin1String("EXPLAIN")) {
            wrote = true;
            affected += result.affected;
        }
    }
    if (schema || wrote) {
        // What undo remembers no longer matches the rows.
        clearUndo();
        touch(schema);
    }
    QVariantList rows;
    for (const QVariantList &row : std::as_const(shown.rows)) {
        rows.append(QVariant(row));
    }
    out.insert(QStringLiteral("ok"), error.isEmpty() && !statements.isEmpty());
    out.insert(QStringLiteral("error"), statements.isEmpty() ? QStringLiteral("No hay nada que ejecutar.") : error);
    out.insert(QStringLiteral("statement"), failedAt);
    out.insert(QStringLiteral("columns"), shown.columns);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("hasRows"), any);
    out.insert(QStringLiteral("affected"), affected);
    out.insert(QStringLiteral("wrote"), schema || wrote);
    out.insert(QStringLiteral("truncated"), shown.truncated);
    out.insert(QStringLiteral("statements"), ran);
    out.insert(QStringLiteral("elapsed"), timer.elapsed());
    return out;
}

// --- Schema -------------------------------------------------------------------

QVariantList Database::tables() const
{
    QVariantList list;
    const Result result = const_cast<Database *>(this)->query(QStringLiteral(
        "SELECT name, type FROM sqlite_schema WHERE type IN ('table', 'view') "
        "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' AND name NOT LIKE '\\_wonderland\\_%' ESCAPE '\\' "
        "ORDER BY name COLLATE NOCASE"));
    for (const QVariantList &row : result.rows) {
        list.append(QVariantMap{{QStringLiteral("name"), row.at(0)}, {QStringLiteral("type"), row.at(1)}});
    }
    return list;
}

QStringList Database::tableNames(bool withViews) const
{
    QStringList names;
    for (const QVariant &table : tables()) {
        const QVariantMap map = table.toMap();
        if (withViews || map.value(QStringLiteral("type")).toString() == QLatin1String("table")) {
            names.append(map.value(QStringLiteral("name")).toString());
        }
    }
    return names;
}

bool Database::exists(const QString &table) const
{
    const Result result = const_cast<Database *>(this)->query(
        QStringLiteral("SELECT 1 FROM sqlite_schema WHERE type IN ('table', 'view') AND name = ? COLLATE NOCASE"), {table});
    return !result.rows.isEmpty();
}

bool Database::isView(const QString &table) const
{
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT type FROM sqlite_schema WHERE name = ? COLLATE NOCASE AND type IN ('table', 'view')"), {table});
    return !result.rows.isEmpty() && result.rows.first().first().toString() == QLatin1String("view");
}

QString Database::sqlOf(const QString &name) const
{
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT sql FROM sqlite_schema WHERE name = ? COLLATE NOCASE"), {name});
    return result.rows.isEmpty() ? QString() : result.rows.first().first().toString();
}

bool Database::withoutRowid(const QString &table) const
{
    const QString sql = sqlOf(table);
    const QList<Sql::Token> tokens = Sql::tokenize(sql);
    QStringList words;
    for (const Sql::Token &token : tokens) {
        if (token.type == Sql::Token::Keyword || token.type == Sql::Token::Identifier) {
            words.append(sql.mid(token.start, token.length).toUpper());
        }
    }
    const int n = words.size();
    for (int i = 0; i + 1 < n; ++i) {
        // Only after the last parenthesis; a column cannot be named WITHOUT unquoted.
        if (words.at(i) == QLatin1String("WITHOUT") && words.at(i + 1) == QLatin1String("ROWID")) {
            return true;
        }
    }
    return false;
}

QVariantList Database::foreignKeys(const QString &table) const
{
    QVariantList list;
    Database *self = const_cast<Database *>(this);
    const Result result = self->query(QStringLiteral("SELECT id, seq, \"table\", \"from\", \"to\", on_update, on_delete FROM pragma_foreign_key_list(?) ORDER BY id, seq"), {table});
    int current = -1;
    QVariantMap key;
    QStringList from;
    QStringList to;
    const auto flush = [&] {
        if (current < 0) {
            return;
        }
        if (to.contains(QString())) {
            // No column named: the key refers to the primary key of the other table.
            to.clear();
            const Result pk = self->query(QStringLiteral("SELECT name FROM pragma_table_info(?) WHERE pk > 0 ORDER BY pk"), {key.value(QStringLiteral("table"))});
            for (const QVariantList &row : pk.rows) {
                to.append(row.first().toString());
            }
        }
        key.insert(QStringLiteral("columns"), from);
        key.insert(QStringLiteral("refColumns"), to);
        list.append(key);
    };
    for (const QVariantList &row : result.rows) {
        if (row.at(0).toInt() != current) {
            flush();
            current = row.at(0).toInt();
            key = {{QStringLiteral("id"), current}, {QStringLiteral("table"), row.at(2)}, {QStringLiteral("onUpdate"), row.at(5)}, {QStringLiteral("onDelete"), row.at(6)}};
            from.clear();
            to.clear();
        }
        from.append(row.at(3).toString());
        to.append(row.at(4).toString());
    }
    flush();
    return list;
}

QVariantList Database::indexes(const QString &table) const
{
    QVariantList list;
    Database *self = const_cast<Database *>(this);
    const Result result = self->query(QStringLiteral("SELECT name, \"unique\", origin FROM pragma_index_list(?) ORDER BY seq"), {table});
    for (const QVariantList &row : result.rows) {
        QStringList columns;
        const Result info = self->query(QStringLiteral("SELECT name FROM pragma_index_info(?) ORDER BY seqno"), {row.at(0)});
        for (const QVariantList &column : info.rows) {
            columns.append(column.first().toString());
        }
        list.append(QVariantMap{
            {QStringLiteral("name"), row.at(0)},
            {QStringLiteral("unique"), row.at(1).toInt() != 0},
            {QStringLiteral("origin"), row.at(2)},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("sql"), sqlOf(row.at(0).toString())},
        });
    }
    return list;
}

QVariantList Database::columns(const QString &table) const
{
    QVariantList list;
    Database *self = const_cast<Database *>(this);
    const Result result = self->query(QStringLiteral("SELECT name, type, \"notnull\", dflt_value, pk, hidden FROM pragma_table_xinfo(?) ORDER BY cid"), {table});
    if (result.rows.isEmpty()) {
        return list;
    }
    const QVariantList keys = foreignKeys(table);
    QStringList uniques;
    for (const QVariant &index : indexes(table)) {
        const QVariantMap map = index.toMap();
        const QStringList columns = map.value(QStringLiteral("columns")).toStringList();
        if (map.value(QStringLiteral("unique")).toBool() && map.value(QStringLiteral("origin")).toString() == QLatin1String("u") && columns.size() == 1) {
            uniques.append(columns.first());
        }
    }
    const QString sql = sqlOf(table);
    bool autoIncrement = false;
    for (const Sql::Token &token : Sql::tokenize(sql)) {
        if (token.type == Sql::Token::Keyword && sql.mid(token.start, token.length).compare(QLatin1String("AUTOINCREMENT"), Qt::CaseInsensitive) == 0) {
            autoIncrement = true;
        }
    }
    for (const QVariantList &row : result.rows) {
        const int hidden = row.at(5).toInt();
        if (hidden == 1) {
            continue;
        }
        const QString name = row.at(0).toString();
        const QString type = row.at(1).toString();
        QVariantMap column{
            {QStringLiteral("name"), name},
            {QStringLiteral("type"), type},
            {QStringLiteral("kind"), Sql::kind(type)},
            {QStringLiteral("notNull"), row.at(2).toInt() != 0},
            {QStringLiteral("defaultValue"), row.at(3).toString()},
            {QStringLiteral("pk"), row.at(4).toInt()},
            {QStringLiteral("unique"), uniques.contains(name)},
            {QStringLiteral("autoIncrement"), autoIncrement && row.at(4).toInt() == 1},
            {QStringLiteral("generated"), hidden >= 2},
            {QStringLiteral("fkTable"), QString()},
            {QStringLiteral("fkColumn"), QString()},
            {QStringLiteral("fkOnDelete"), QString()},
            {QStringLiteral("fkOnUpdate"), QString()},
        };
        for (const QVariant &key : keys) {
            const QVariantMap map = key.toMap();
            const QStringList from = map.value(QStringLiteral("columns")).toStringList();
            if (from.size() == 1 && from.first().compare(name, Qt::CaseInsensitive) == 0) {
                column.insert(QStringLiteral("fkTable"), map.value(QStringLiteral("table")));
                column.insert(QStringLiteral("fkColumn"), map.value(QStringLiteral("refColumns")).toStringList().value(0));
                column.insert(QStringLiteral("fkOnDelete"), map.value(QStringLiteral("onDelete")));
                column.insert(QStringLiteral("fkOnUpdate"), map.value(QStringLiteral("onUpdate")));
                break;
            }
        }
        list.append(column);
    }
    return list;
}

QStringList Database::columnNames(const QString &table) const
{
    QStringList names;
    for (const QVariant &column : columns(table)) {
        names.append(column.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

QStringList Database::storedColumns(const QString &table) const
{
    QStringList names;
    for (const QVariant &column : columns(table)) {
        const QVariantMap map = column.toMap();
        if (!map.value(QStringLiteral("generated")).toBool()) {
            names.append(map.value(QStringLiteral("name")).toString());
        }
    }
    return names;
}

QString Database::labelColumn(const QString &table) const
{
    QString first;
    for (const QVariant &column : columns(table)) {
        const QVariantMap map = column.toMap();
        const QString name = map.value(QStringLiteral("name")).toString();
        if (first.isEmpty()) {
            first = name;
        }
        if (map.value(QStringLiteral("kind")).toString() == QLatin1String("text") && map.value(QStringLiteral("pk")).toInt() == 0
            && map.value(QStringLiteral("fkTable")).toString().isEmpty()) {
            return name;
        }
    }
    return first;
}

QVariantList Database::related(const QString &table, const QVariantMap &record, int limit) const
{
    QVariantList out;
    if (!m_open || record.isEmpty()) {
        return out;
    }
    auto *self = const_cast<Database *>(this);
    const auto read = [&](const QString &other, const QString &column, const QVariant &value) {
        QVariantList rows;
        const QString label = labelColumn(other);
        const Result result = self->query(QStringLiteral("SELECT * FROM %1 WHERE %2 = ? LIMIT ?").arg(Sql::ident(other), Sql::ident(column)), {value, qMax(1, limit)});
        for (const QVariantList &row : result.rows) {
            QVariantMap map;
            for (int i = 0; i < result.columns.size(); ++i) {
                const QVariant &cell = row.at(i);
                map.insert(result.columns.at(i), cell.typeId() == QMetaType::QByteArray ? QVariant(QStringLiteral("(binario)")) : cell);
            }
            rows.append(QVariantMap{{QStringLiteral("label"), map.value(label).toString()}, {QStringLiteral("record"), map}});
        }
        return rows;
    };
    const QVariantList all = relations();
    for (const QVariant &entry : all) {
        const QVariantMap relation = entry.toMap();
        const QString child = relation.value(QStringLiteral("table")).toString();
        const QString column = relation.value(QStringLiteral("column")).toString();
        const QString parent = relation.value(QStringLiteral("refTable")).toString();
        const QString refColumn = relation.value(QStringLiteral("refColumn")).toString();
        if (child.compare(table, Qt::CaseInsensitive) == 0) {
            const QVariant value = record.value(column);
            if (value.isValid() && !value.isNull()) {
                const QVariantList rows = read(parent, refColumn, value);
                out.append(QVariantMap{{QStringLiteral("direction"), QStringLiteral("parent")}, {QStringLiteral("table"), parent}, {QStringLiteral("column"), refColumn},
                                       {QStringLiteral("refColumn"), column}, {QStringLiteral("value"), value}, {QStringLiteral("count"), rows.size()},
                                       {QStringLiteral("rows"), rows}});
            }
        }
    }
    for (const QVariant &entry : all) {
        const QVariantMap relation = entry.toMap();
        const QString child = relation.value(QStringLiteral("table")).toString();
        const QString column = relation.value(QStringLiteral("column")).toString();
        const QString refColumn = relation.value(QStringLiteral("refColumn")).toString();
        if (relation.value(QStringLiteral("refTable")).toString().compare(table, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QVariant value = record.value(refColumn);
        if (!value.isValid() || value.isNull()) {
            continue;
        }
        const Result count = self->query(QStringLiteral("SELECT count(*) FROM %1 WHERE %2 = ?").arg(Sql::ident(child), Sql::ident(column)), {value});
        out.append(QVariantMap{{QStringLiteral("direction"), QStringLiteral("children")}, {QStringLiteral("table"), child}, {QStringLiteral("column"), column},
                               {QStringLiteral("refColumn"), refColumn}, {QStringLiteral("value"), value},
                               {QStringLiteral("count"), count.rows.value(0).value(0).toLongLong()}, {QStringLiteral("rows"), read(child, column, value)}});
    }
    return out;
}

QVariantList Database::relations() const
{
    QVariantList list;
    for (const QString &table : tableNames()) {
        for (const QVariant &key : foreignKeys(table)) {
            const QVariantMap map = key.toMap();
            const QStringList from = map.value(QStringLiteral("columns")).toStringList();
            const QStringList to = map.value(QStringLiteral("refColumns")).toStringList();
            if (from.size() != 1 || to.size() != 1) {
                continue;
            }
            list.append(QVariantMap{
                {QStringLiteral("table"), table},
                {QStringLiteral("column"), from.first()},
                {QStringLiteral("refTable"), map.value(QStringLiteral("table"))},
                {QStringLiteral("refColumn"), to.first()},
            });
        }
    }
    return list;
}

qint64 Database::rowCount(const QString &table) const
{
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT count(*) FROM ") + Sql::ident(table));
    return result.rows.isEmpty() ? -1 : result.rows.first().first().toLongLong();
}

QVariantMap Database::schema() const
{
    QVariantMap map;
    for (const QString &table : tableNames(true)) {
        map.insert(table, columnNames(table));
    }
    return map;
}

QString Database::rowidAlias(const QString &table) const
{
    if (withoutRowid(table)) {
        return {};
    }
    QString alias;
    int keys = 0;
    for (const QVariant &column : columns(table)) {
        const QVariantMap map = column.toMap();
        if (map.value(QStringLiteral("pk")).toInt() > 0) {
            ++keys;
            if (map.value(QStringLiteral("type")).toString().compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) == 0) {
                alias = map.value(QStringLiteral("name")).toString();
            }
        }
    }
    return keys == 1 ? alias : QString();
}

QStringList Database::keyColumns(const QString &table) const
{
    if (isView(table)) {
        return {};
    }
    const QVariantList all = columns(table);
    if (all.isEmpty()) {
        return {};
    }
    if (withoutRowid(table)) {
        QMap<int, QString> key;
        for (const QVariant &column : all) {
            const QVariantMap map = column.toMap();
            if (map.value(QStringLiteral("pk")).toInt() > 0) {
                key.insert(map.value(QStringLiteral("pk")).toInt(), map.value(QStringLiteral("name")).toString());
            }
        }
        return key.values();
    }
    QStringList names;
    for (const QVariant &column : all) {
        names.append(column.toMap().value(QStringLiteral("name")).toString().toLower());
    }
    for (const QString &candidate : {QStringLiteral("rowid"), QStringLiteral("_rowid_"), QStringLiteral("oid")}) {
        if (!names.contains(candidate)) {
            return {candidate};
        }
    }
    return {};
}

QVariantList Database::lookup(const QString &table, const QString &column, int limit) const
{
    QVariantList list;
    QString label;
    for (const QVariant &other : columns(table)) {
        const QVariantMap map = other.toMap();
        const QString name = map.value(QStringLiteral("name")).toString();
        if (name != column && map.value(QStringLiteral("kind")).toString() == QLatin1String("text")) {
            label = name;
            break;
        }
    }
    const QString sql = QStringLiteral("SELECT %1, %2 FROM %3 ORDER BY 1 LIMIT ?")
        .arg(Sql::ident(column), label.isEmpty() ? QStringLiteral("NULL") : Sql::ident(label), Sql::ident(table));
    const Result result = const_cast<Database *>(this)->query(sql, {limit});
    for (const QVariantList &row : result.rows) {
        list.append(QVariantMap{{QStringLiteral("value"), row.at(0)}, {QStringLiteral("label"), row.at(1).toString()}});
    }
    return list;
}

// --- Rows ---------------------------------------------------------------------

QVariant Database::coerce(const QString &kind, const QVariant &value)
{
    if (!value.isValid() || value.isNull()) {
        return {};
    }
    if (value.typeId() != QMetaType::QString) {
        if (kind == QLatin1String("bool")) {
            return value.toBool() ? 1 : 0;
        }
        if (value.typeId() == QMetaType::Double && kind == QLatin1String("integer")) {
            const double number = value.toDouble();
            if (number == double(qlonglong(number))) {
                return qlonglong(number);
            }
        }
        return value;
    }
    const QString text = value.toString();
    if (kind == QLatin1String("text") || kind == QLatin1String("blob")) {
        return text;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        // Nothing typed in a number or a date is no value at all.
        return {};
    }
    bool ok = false;
    if (kind == QLatin1String("integer") || kind == QLatin1String("real")) {
        const qlonglong whole = trimmed.toLongLong(&ok);
        if (ok) {
            return whole;
        }
        QString dotted = trimmed;
        // «12,5» as it is written in Spanish.
        if (dotted.count(QLatin1Char(',')) == 1 && !dotted.contains(QLatin1Char('.'))) {
            dotted.replace(QLatin1Char(','), QLatin1Char('.'));
        }
        const double real = dotted.toDouble(&ok);
        if (ok) {
            return real;
        }
        return text;
    }
    if (kind == QLatin1String("bool")) {
        const QString lower = trimmed.toLower();
        if (lower == QLatin1String("1") || lower == QLatin1String("true") || lower == QStringLiteral("sí") || lower == QLatin1String("si") || lower == QLatin1String("verdadero")) {
            return 1;
        }
        if (lower == QLatin1String("0") || lower == QLatin1String("false") || lower == QLatin1String("no") || lower == QLatin1String("falso")) {
            return 0;
        }
        return text;
    }
    if (kind == QLatin1String("date") || kind == QLatin1String("datetime")) {
        // Dates are kept the way SQLite's own functions read them.
        static const QStringList dayFormats = {QStringLiteral("d/M/yyyy"), QStringLiteral("d-M-yyyy"), QStringLiteral("d.M.yyyy")};
        for (const QString &format : dayFormats) {
            const QDate date = QDate::fromString(trimmed, format);
            if (date.isValid()) {
                return kind == QLatin1String("date") ? date.toString(Qt::ISODate) : date.toString(Qt::ISODate) + QStringLiteral(" 00:00:00");
            }
            for (const QString &clock : {QStringLiteral(" H:mm"), QStringLiteral(" H:mm:ss")}) {
                const QDateTime stamp = QDateTime::fromString(trimmed, format + clock);
                if (stamp.isValid()) {
                    return kind == QLatin1String("date") ? stamp.date().toString(Qt::ISODate) : stamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                }
            }
        }
        return trimmed;
    }
    return text;
}

QString Database::whereKey(const QStringList &keyColumns) const
{
    QStringList parts;
    for (const QString &column : keyColumns) {
        parts.append(Sql::ident(column) + QStringLiteral(" IS ?"));
    }
    return parts.join(QStringLiteral(" AND "));
}

QVariantMap Database::row(const QString &table, const QVariantList &key) const
{
    QVariantMap map;
    const QStringList keys = keyColumns(table);
    if (keys.isEmpty() || keys.size() != key.size()) {
        return map;
    }
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT * FROM %1 WHERE %2").arg(Sql::ident(table), whereKey(keys)), key);
    if (result.rows.isEmpty()) {
        return map;
    }
    for (int i = 0; i < result.columns.size(); ++i) {
        map.insert(result.columns.at(i), result.rows.first().at(i));
    }
    return map;
}

bool Database::updateCell(const QString &table, const QVariantList &key, const QString &column, const QVariant &value, QVariantList *newKey)
{
    const QStringList keys = keyColumns(table);
    if (keys.isEmpty() || keys.size() != key.size()) {
        return fail(QStringLiteral("Las filas de «%1» no se pueden editar.").arg(table));
    }
    const Result before = query(QStringLiteral("SELECT %1 FROM %2 WHERE %3").arg(Sql::ident(column), Sql::ident(table), whereKey(keys)), key);
    if (!before.ok) {
        return fail(before.error);
    }
    if (before.rows.isEmpty()) {
        return fail(QStringLiteral("La fila ya no existe."));
    }
    QVariantList binds{value};
    binds.append(key);
    const Result result = query(QStringLiteral("UPDATE %1 SET %2 = ? WHERE %3").arg(Sql::ident(table), Sql::ident(column), whereKey(keys)), binds);
    if (!result.ok) {
        return fail(result.error);
    }
    // Changing the key itself moves the row to another key.
    QVariantList after = key;
    const QString alias = rowidAlias(table);
    if (!alias.isEmpty() && alias.compare(column, Qt::CaseInsensitive) == 0 && keys.size() == 1) {
        after = {value};
    } else {
        for (int i = 0; i < keys.size(); ++i) {
            if (keys.at(i).compare(column, Qt::CaseInsensitive) == 0) {
                after[i] = value;
            }
        }
    }
    if (newKey) {
        *newKey = after;
    }
    Change change;
    change.type = Change::Update;
    change.table = table;
    change.text = QStringLiteral("Cambiar «%1»").arg(column);
    change.key = key;
    change.keyAfter = after;
    change.column = column;
    change.before = before.rows.first().first();
    change.after = value;
    push(change);
    ++m_dataRevision;
    Q_EMIT dataChanged();
    return true;
}

QVariantList Database::insertRow(const QString &table, const QVariantMap &values)
{
    const QStringList keys = keyColumns(table);
    if (keys.isEmpty()) {
        fail(QStringLiteral("En «%1» no se pueden añadir filas.").arg(table));
        return {};
    }
    QStringList names;
    QStringList marks;
    QVariantList binds;
    const QStringList known = storedColumns(table);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            continue;
        }
        names.append(Sql::ident(it.key()));
        marks.append(QStringLiteral("?"));
        binds.append(it.value());
    }
    const QString sql = names.isEmpty() ? QStringLiteral("INSERT INTO %1 DEFAULT VALUES").arg(Sql::ident(table))
                                        : QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(Sql::ident(table), names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", ")));
    const Result result = query(sql, binds);
    if (!result.ok) {
        fail(result.error);
        return {};
    }
    QVariantList key;
    if (withoutRowid(table)) {
        for (const QString &column : keys) {
            key.append(values.value(column));
        }
    } else {
        key.append(result.lastId);
    }
    Change change;
    change.type = Change::Insert;
    change.table = table;
    change.text = QStringLiteral("Añadir registro");
    change.keyColumns = keys;
    change.keys = {key};
    // The whole row as it was stored, defaults included, to put it back on redo.
    change.columns = known;
    QStringList quoted;
    for (const QString &column : known) {
        quoted.append(Sql::ident(column));
    }
    const Result stored = query(QStringLiteral("SELECT %1 FROM %2 WHERE %3").arg(quoted.join(QStringLiteral(", ")), Sql::ident(table), whereKey(keys)), key);
    if (stored.ok && !stored.rows.isEmpty()) {
        change.rows = {stored.rows.first()};
    }
    push(change);
    ++m_dataRevision;
    Q_EMIT dataChanged();
    return key;
}

bool Database::deleteRows(const QString &table, const QList<QVariantList> &rowKeys)
{
    const QStringList keys = keyColumns(table);
    if (keys.isEmpty()) {
        return fail(QStringLiteral("Las filas de «%1» no se pueden eliminar.").arg(table));
    }
    if (rowKeys.isEmpty()) {
        return true;
    }
    Change change;
    change.type = Change::Delete;
    change.table = table;
    change.text = rowKeys.size() == 1 ? QStringLiteral("Eliminar registro") : QStringLiteral("Eliminar %1 registros").arg(rowKeys.size());
    change.keyColumns = keys;
    change.columns = storedColumns(table);
    QStringList quoted;
    for (const QString &column : std::as_const(change.columns)) {
        quoted.append(Sql::ident(column));
    }
    if (!begin()) {
        return false;
    }
    for (const QVariantList &key : rowKeys) {
        if (key.size() != keys.size()) {
            continue;
        }
        const Result stored = query(QStringLiteral("SELECT %1 FROM %2 WHERE %3").arg(quoted.join(QStringLiteral(", ")), Sql::ident(table), whereKey(keys)), key);
        if (!stored.ok || stored.rows.isEmpty()) {
            continue;
        }
        const Result removed = query(QStringLiteral("DELETE FROM %1 WHERE %2").arg(Sql::ident(table), whereKey(keys)), key);
        if (!removed.ok) {
            rollback();
            return fail(removed.error);
        }
        change.keys.append(key);
        change.rows.append(stored.rows.first());
    }
    if (!commit()) {
        rollback();
        return false;
    }
    if (!change.keys.isEmpty()) {
        push(change);
    }
    ++m_dataRevision;
    Q_EMIT dataChanged();
    return true;
}

// --- Undo ---------------------------------------------------------------------

void Database::push(const Change &change)
{
    while (m_changes.size() > m_undoAt) {
        m_changes.removeLast();
    }
    m_changes.append(change);
    // Old steps go when there are too many to be worth their memory.
    while (m_changes.size() > 200) {
        m_changes.removeFirst();
    }
    m_undoAt = m_changes.size();
    Q_EMIT undoChanged();
}

void Database::clearUndo()
{
    if (m_changes.isEmpty()) {
        return;
    }
    m_changes.clear();
    m_undoAt = 0;
    Q_EMIT undoChanged();
}

QString Database::undoText() const
{
    return canUndo() ? m_changes.at(m_undoAt - 1).text : QString();
}

QString Database::redoText() const
{
    return canRedo() ? m_changes.at(m_undoAt).text : QString();
}

bool Database::removeKeys(const Change &change)
{
    for (const QVariantList &key : change.keys) {
        const Result result = query(QStringLiteral("DELETE FROM %1 WHERE %2").arg(Sql::ident(change.table), whereKey(change.keyColumns)), key);
        if (!result.ok) {
            return fail(result.error);
        }
    }
    return true;
}

bool Database::restoreRows(const Change &change)
{
    // With its old key, so that what refers to the row finds it again.
    const bool rowid = !withoutRowid(change.table);
    QStringList names;
    for (const QString &column : change.columns) {
        names.append(Sql::ident(column));
    }
    if (rowid) {
        names.prepend(Sql::ident(change.keyColumns.value(0)));
    }
    QStringList marks;
    for (int i = 0; i < names.size(); ++i) {
        marks.append(QStringLiteral("?"));
    }
    const QString sql = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(Sql::ident(change.table), names.join(QStringLiteral(", ")), marks.join(QStringLiteral(", ")));
    for (int i = 0; i < change.rows.size(); ++i) {
        QVariantList binds = change.rows.at(i);
        if (rowid) {
            binds.prepend(change.keys.at(i).value(0));
        }
        const Result result = query(sql, binds);
        if (!result.ok) {
            return fail(result.error);
        }
    }
    return true;
}

bool Database::apply(const Change &change, bool forward)
{
    if (!begin()) {
        return false;
    }
    bool ok = false;
    switch (change.type) {
    case Change::Update: {
        QVariantList binds{forward ? change.after : change.before};
        binds.append(forward ? change.key : change.keyAfter);
        const Result result = query(QStringLiteral("UPDATE %1 SET %2 = ? WHERE %3").arg(Sql::ident(change.table), Sql::ident(change.column), whereKey(keyColumns(change.table))), binds);
        ok = result.ok;
        if (!ok) {
            fail(result.error);
        }
        break;
    }
    case Change::Insert:
        ok = forward ? restoreRows(change) : removeKeys(change);
        break;
    case Change::Delete:
        ok = forward ? removeKeys(change) : restoreRows(change);
        break;
    }
    if (!ok) {
        rollback();
        return false;
    }
    if (!commit()) {
        rollback();
        return false;
    }
    return true;
}

bool Database::undo()
{
    if (!canUndo()) {
        return false;
    }
    if (!apply(m_changes.at(m_undoAt - 1), false)) {
        return false;
    }
    --m_undoAt;
    Q_EMIT undoChanged();
    ++m_dataRevision;
    Q_EMIT dataChanged();
    return true;
}

bool Database::redo()
{
    if (!canRedo()) {
        return false;
    }
    if (!apply(m_changes.at(m_undoAt), true)) {
        return false;
    }
    ++m_undoAt;
    Q_EMIT undoChanged();
    ++m_dataRevision;
    Q_EMIT dataChanged();
    return true;
}

// --- Bases' own objects ---------------------------------------------------------

bool Database::hasMeta(const QString &table) const
{
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT 1 FROM sqlite_schema WHERE type = 'table' AND name = ?"), {table});
    return !result.rows.isEmpty();
}

bool Database::ensureMeta()
{
    // Made on the first save: opening someone else's database leaves it as it was.
    return exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (id INTEGER PRIMARY KEY, kind TEXT NOT NULL, name TEXT NOT NULL, "
                               "definition TEXT NOT NULL DEFAULT '{}', modified TEXT, UNIQUE (kind, name))").arg(Sql::ident(objectsTable)))
        && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (id INTEGER PRIMARY KEY, sql TEXT NOT NULL, ran TEXT, ok INTEGER NOT NULL DEFAULT 1)").arg(Sql::ident(historyTable)));
}

static QVariantMap objectFrom(const QVariantList &row)
{
    return {
        {QStringLiteral("id"), row.at(0).toInt()},
        {QStringLiteral("kind"), row.at(1)},
        {QStringLiteral("name"), row.at(2)},
        {QStringLiteral("definition"), QJsonDocument::fromJson(row.at(3).toString().toUtf8()).object().toVariantMap()},
    };
}

QVariantList Database::objects(const QString &kind) const
{
    QVariantList list;
    if (!m_open || !hasMeta(objectsTable)) {
        return list;
    }
    const Result result = const_cast<Database *>(this)->query(
        QStringLiteral("SELECT id, kind, name, definition FROM %1 WHERE kind = ? ORDER BY name COLLATE NOCASE").arg(Sql::ident(objectsTable)), {kind});
    for (const QVariantList &row : result.rows) {
        list.append(objectFrom(row));
    }
    return list;
}

QVariantMap Database::object(int id) const
{
    if (!m_open || !hasMeta(objectsTable)) {
        return {};
    }
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT id, kind, name, definition FROM %1 WHERE id = ?").arg(Sql::ident(objectsTable)), {id});
    return result.rows.isEmpty() ? QVariantMap() : objectFrom(result.rows.first());
}

QVariantMap Database::objectNamed(const QString &kind, const QString &name) const
{
    if (!m_open || !hasMeta(objectsTable)) {
        return {};
    }
    const Result result = const_cast<Database *>(this)->query(
        QStringLiteral("SELECT id, kind, name, definition FROM %1 WHERE kind = ? AND name = ? COLLATE NOCASE").arg(Sql::ident(objectsTable)), {kind, name});
    return result.rows.isEmpty() ? QVariantMap() : objectFrom(result.rows.first());
}

int Database::saveObject(const QString &kind, const QString &name, const QVariantMap &definition, int id)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        fail(QStringLiteral("Hace falta un nombre."));
        return 0;
    }
    if (!ensureMeta()) {
        return 0;
    }
    const QVariantMap same = objectNamed(kind, clean);
    if (!same.isEmpty() && same.value(QStringLiteral("id")).toInt() != id) {
        fail(QStringLiteral("Ya hay otro con el nombre «%1».").arg(clean));
        return 0;
    }
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(definition)).toJson(QJsonDocument::Compact));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    Result result;
    if (id > 0) {
        result = query(QStringLiteral("UPDATE %1 SET name = ?, definition = ?, modified = ? WHERE id = ?").arg(Sql::ident(objectsTable)), {clean, json, now, id});
    } else {
        result = query(QStringLiteral("INSERT INTO %1 (kind, name, definition, modified) VALUES (?, ?, ?, ?)").arg(Sql::ident(objectsTable)), {kind, clean, json, now});
        id = result.lastId.toInt();
    }
    if (!result.ok) {
        fail(result.error);
        return 0;
    }
    ++m_objectsRevision;
    Q_EMIT objectsChanged();
    return id;
}

bool Database::renameObject(int id, const QString &name)
{
    const QVariantMap found = object(id);
    if (found.isEmpty()) {
        return false;
    }
    return saveObject(found.value(QStringLiteral("kind")).toString(), name, found.value(QStringLiteral("definition")).toMap(), id) > 0;
}

bool Database::removeObject(int id)
{
    if (!m_open || !hasMeta(objectsTable)) {
        return false;
    }
    if (!exec(QStringLiteral("DELETE FROM %1 WHERE id = ?").arg(Sql::ident(objectsTable)), {id})) {
        return false;
    }
    ++m_objectsRevision;
    Q_EMIT objectsChanged();
    return true;
}

void Database::addHistory(const QString &sql, bool ok)
{
    const QString text = sql.trimmed();
    if (text.isEmpty() || m_readOnly || !ensureMeta()) {
        return;
    }
    const QString table = Sql::ident(historyTable);
    // Running the same thing again moves it to the top instead of repeating it.
    query(QStringLiteral("DELETE FROM %1 WHERE sql = ?").arg(table), {text});
    query(QStringLiteral("INSERT INTO %1 (sql, ran, ok) VALUES (?, ?, ?)").arg(table), {text, QDateTime::currentDateTime().toString(Qt::ISODate), ok ? 1 : 0});
    query(QStringLiteral("DELETE FROM %1 WHERE id NOT IN (SELECT id FROM %1 ORDER BY id DESC LIMIT 200)").arg(table));
    ++m_objectsRevision;
    Q_EMIT objectsChanged();
}

QVariantList Database::history(int limit) const
{
    QVariantList list;
    if (!m_open || !hasMeta(historyTable)) {
        return list;
    }
    const Result result = const_cast<Database *>(this)->query(QStringLiteral("SELECT sql, ran, ok FROM %1 ORDER BY id DESC LIMIT ?").arg(Sql::ident(historyTable)), {limit});
    for (const QVariantList &row : result.rows) {
        list.append(QVariantMap{{QStringLiteral("sql"), row.at(0)}, {QStringLiteral("ran"), row.at(1)}, {QStringLiteral("ok"), row.at(2).toInt() != 0}});
    }
    return list;
}

void Database::clearHistory()
{
    if (!m_open || !hasMeta(historyTable)) {
        return;
    }
    exec(QStringLiteral("DELETE FROM %1").arg(Sql::ident(historyTable)));
    ++m_objectsRevision;
    Q_EMIT objectsChanged();
}
