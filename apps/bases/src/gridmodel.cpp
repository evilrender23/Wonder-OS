#include "gridmodel.h"

#include <QSet>
#include <QRegularExpression>
#include "sqlutil.h"

GridModel::GridModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void GridModel::setDatabase(Database *db)
{
    if (db == m_db) {
        return;
    }
    if (m_db) {
        disconnect(m_db, nullptr, this, nullptr);
    }
    m_db = db;
    if (m_db) {
        connect(m_db, &Database::dataChanged, this, &GridModel::external);
        connect(m_db, &Database::schemaChanged, this, &GridModel::reload);
    }
    reload();
    Q_EMIT sourceChanged();
}

void GridModel::setTable(const QString &table)
{
    if (table == m_table) {
        return;
    }
    m_table = table;
    // One source at a time.
    if (!table.isEmpty()) {
        m_sql.clear();
    }
    m_sortColumn = -1;
    m_filters.clear();
    reload();
    Q_EMIT sourceChanged();
    Q_EMIT viewChanged();
}

void GridModel::setSql(const QString &sql)
{
    if (sql == m_sql) {
        return;
    }
    m_sql = sql;
    if (!sql.isEmpty()) {
        m_table.clear();
    }
    m_sortColumn = -1;
    m_filters.clear();
    reload();
    Q_EMIT sourceChanged();
    Q_EMIT viewChanged();
}

void GridModel::setParameters(const QVariantMap &parameters)
{
    if (parameters == m_parameters) {
        return;
    }
    m_parameters = parameters;
    reload();
    Q_EMIT sourceChanged();
}

void GridModel::setSortColumn(int column)
{
    if (column == m_sortColumn) {
        return;
    }
    m_sortColumn = column;
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setSortDescending(bool descending)
{
    if (descending == m_sortDescending) {
        return;
    }
    m_sortDescending = descending;
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setFilters(const QVariantMap &filters)
{
    if (filters == m_filters) {
        return;
    }
    m_filters = filters;
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setSearch(const QString &search)
{
    if (search == m_search) {
        return;
    }
    m_search = search;
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setLinkColumn(const QString &column)
{
    if (column == m_linkColumn) {
        return;
    }
    m_linkColumn = column;
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setLinkValue(const QVariant &value)
{
    if (value == m_linkValue && value.isNull() == m_linkValue.isNull()) {
        return;
    }
    m_linkValue = value;
    refresh();
    Q_EMIT viewChanged();
}

bool GridModel::filtered() const
{
    if (!m_search.trimmed().isEmpty()) {
        return true;
    }
    QVariantList unused;
    if (!conditionTerms(&unused).isEmpty()) {
        return true;
    }
    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it) {
        if (!it.value().toString().trimmed().isEmpty()) {
            return true;
        }
    }
    return false;
}

void GridModel::sortBy(int column)
{
    // Up, down, and back to the order of the table.
    if (column != m_sortColumn) {
        m_sortColumn = column;
        m_sortDescending = false;
    } else if (!m_sortDescending) {
        m_sortDescending = true;
    } else {
        m_sortColumn = -1;
        m_sortDescending = false;
    }
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setFilter(const QString &column, const QString &typed)
{
    if (m_filters.value(column).toString() == typed) {
        return;
    }
    if (typed.trimmed().isEmpty()) {
        m_filters.remove(column);
    } else {
        m_filters.insert(column, typed);
    }
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::clearFilters()
{
    if (m_filters.isEmpty() && m_search.isEmpty() && m_conditions.isEmpty()) {
        return;
    }
    m_conditions.clear();
    m_filters.clear();
    m_search.clear();
    refresh();
    Q_EMIT viewChanged();
}

void GridModel::setError(const QString &error)
{
    if (error == m_error) {
        return;
    }
    m_error = error;
    Q_EMIT errorChanged();
}

QString GridModel::source() const
{
    if (!m_table.isEmpty()) {
        return Sql::ident(m_table);
    }
    return QLatin1Char('(') + Sql::trimmed(m_sql) + QLatin1Char(')');
}

QString GridModel::where(QVariantList *binds) const
{
    QStringList terms;
    if (!m_linkColumn.isEmpty() && m_names.contains(m_linkColumn)) {
        // `= NULL` is never true: the rows tied to nothing are asked for as such.
        if (m_linkValue.isNull()) {
            terms.append(Sql::ident(m_linkColumn) + QStringLiteral(" IS NULL"));
        } else {
            terms.append(Sql::ident(m_linkColumn) + QStringLiteral(" = ?"));
            binds->append(m_linkValue);
        }
    }
    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it) {
        if (!m_names.contains(it.key())) {
            continue;
        }
        const QString term = Sql::criterion(Sql::ident(it.key()), it.value().toString(), binds);
        if (!term.isEmpty()) {
            terms.append(term);
        }
    }
    terms.append(conditionTerms(binds));
    const QString needle = m_search.trimmed();
    if (!needle.isEmpty() && !m_names.isEmpty()) {
        QString pattern;
        for (const QChar c : needle) {
            if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\')) {
                pattern += QLatin1Char('\\');
            }
            pattern += c;
        }
        pattern = QLatin1Char('%') + pattern + QLatin1Char('%');
        QStringList any;
        for (const QString &name : m_names) {
            any.append(Sql::ident(name) + QStringLiteral(" LIKE ? ESCAPE '\\'"));
            binds->append(pattern);
        }
        terms.append(QLatin1Char('(') + any.join(QStringLiteral(" OR ")) + QLatin1Char(')'));
    }
    return terms.isEmpty() ? QString() : QStringLiteral(" WHERE ") + terms.join(QStringLiteral(" AND "));
}

QString GridModel::order() const
{
    QStringList terms;
    if (m_sortColumn >= 0 && m_sortColumn < m_names.size()) {
        terms.append(Sql::ident(m_names.at(m_sortColumn)) + (m_sortDescending ? QStringLiteral(" DESC") : QStringLiteral(" ASC")));
    }
    // The key last: rows that tie keep one order from page to page.
    for (const QString &key : m_keyColumns) {
        terms.append(Sql::ident(key));
    }
    return terms.isEmpty() ? QString() : QStringLiteral(" ORDER BY ") + terms.join(QStringLiteral(", "));
}

QString GridModel::viewSql() const
{
    // The same conditions as where(), with their values written in.
    QStringList terms;
    if (!m_linkColumn.isEmpty() && m_names.contains(m_linkColumn)) {
        terms.append(Sql::ident(m_linkColumn) + (m_linkValue.isNull() ? QStringLiteral(" IS NULL") : QStringLiteral(" = ") + Sql::literal(m_linkValue)));
    }
    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it) {
        if (!m_names.contains(it.key())) {
            continue;
        }
        const QString term = Sql::criterionInline(Sql::ident(it.key()), it.value().toString());
        if (!term.isEmpty()) {
            terms.append(term);
        }
    }
    {
        QVariantList bound;
        const QStringList chained = conditionTerms(&bound);
        if (!chained.isEmpty()) {
            terms.append(Sql::inlined(chained.join(QStringLiteral(" AND ")), bound));
        }
    }
    const QString needle = m_search.trimmed();
    if (!needle.isEmpty() && !m_names.isEmpty()) {
        QString pattern;
        for (const QChar c : needle) {
            if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\')) {
                pattern += QLatin1Char('\\');
            }
            pattern += c;
        }
        const QString literal = Sql::literal(QLatin1Char('%') + pattern + QLatin1Char('%'));
        QStringList any;
        for (const QString &name : m_names) {
            any.append(Sql::ident(name) + QStringLiteral(" LIKE ") + literal + QStringLiteral(" ESCAPE '\\'"));
        }
        terms.append(QLatin1Char('(') + any.join(QStringLiteral(" OR ")) + QLatin1Char(')'));
    }
    QString out = QStringLiteral("SELECT * FROM ") + source();
    if (!terms.isEmpty()) {
        out += QStringLiteral(" WHERE ") + terms.join(QStringLiteral(" AND "));
    }
    if (m_sortColumn >= 0 && m_sortColumn < m_names.size()) {
        out += QStringLiteral(" ORDER BY ") + Sql::ident(m_names.at(m_sortColumn)) + (m_sortDescending ? QStringLiteral(" DESC") : QStringLiteral(" ASC"));
    }
    return out;
}

/// EXPLAIN, or a PRAGMA that only reads: the ones that take a table, and
/// the ones that give a value when they are given none.
static bool readsOnly(const QString &sql)
{
    const QString verb = Sql::verb(sql);
    if (verb == QLatin1String("EXPLAIN")) {
        return true;
    }
    if (verb != QLatin1String("PRAGMA")) {
        return false;
    }
    static const QSet<QString> about = {QStringLiteral("table_info"), QStringLiteral("table_xinfo"), QStringLiteral("table_list"), QStringLiteral("index_list"),
                                        QStringLiteral("index_info"), QStringLiteral("index_xinfo"), QStringLiteral("foreign_key_list"), QStringLiteral("foreign_key_check"),
                                        QStringLiteral("integrity_check"), QStringLiteral("quick_check")};
    static const QSet<QString> values = {QStringLiteral("database_list"), QStringLiteral("collation_list"), QStringLiteral("compile_options"), QStringLiteral("function_list"),
                                         QStringLiteral("pragma_list"), QStringLiteral("page_count"), QStringLiteral("page_size"), QStringLiteral("freelist_count"),
                                         QStringLiteral("user_version"), QStringLiteral("application_id"), QStringLiteral("encoding"), QStringLiteral("journal_mode"),
                                         QStringLiteral("schema_version"), QStringLiteral("foreign_keys"), QStringLiteral("auto_vacuum"), QStringLiteral("synchronous")};
    static const QRegularExpression shape(QStringLiteral("^PRAGMA\\s+(?:main\\.)?([A-Za-z_]+)\\s*(\\(\\s*(?:\"(?:[^\"]|\"\")*\"|'(?:[^']|'')*'|[\\w]+)\\s*\\))?$"),
                                          QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = shape.match(Sql::trimmed(sql).trimmed());
    if (!match.hasMatch()) {
        return false;
    }
    const QString name = match.captured(1).toLower();
    return about.contains(name) || (values.contains(name) && match.captured(2).isEmpty());
}

void GridModel::reload()
{
    beginResetModel();
    m_pages.clear();
    m_recent.clear();
    m_fields.clear();
    m_names.clear();
    m_kinds.clear();
    m_keyColumns.clear();
    m_staticRows.clear();
    m_static = false;
    m_count = 0;
    m_total = -1;
    QString error;
    if (m_db && m_db->isOpen()) {
        if (!m_table.isEmpty()) {
            m_fields = m_db->columns(m_table);
            m_keyColumns = m_db->keyColumns(m_table);
            for (const QVariant &field : std::as_const(m_fields)) {
                const QVariantMap map = field.toMap();
                m_names.append(map.value(QStringLiteral("name")).toString());
                m_kinds.append(map.value(QStringLiteral("kind")).toString());
            }
            if (m_fields.isEmpty()) {
                error = QStringLiteral("No existe la tabla «%1».").arg(m_table);
            }
        } else if (!m_sql.trimmed().isEmpty()) {
            Result head;
            if (Sql::split(m_sql).size() > 1) {
                error = QStringLiteral("Solo se puede mostrar una instrucción a la vez.");
            } else if (Sql::selects(m_sql)) {
                head = m_db->queryNamed(QStringLiteral("SELECT * FROM ") + source() + QStringLiteral(" LIMIT 50"), m_parameters);
            } else if (readsOnly(m_sql)) {
                m_static = true;
                head = m_db->queryNamed(Sql::trimmed(m_sql), m_parameters, 100000);
                m_staticRows = head.rows;
            } else {
                // A grid shows rows: what changes them is run from the editor.
                error = QStringLiteral("Aquí solo se pueden mostrar consultas (SELECT, WITH, VALUES, EXPLAIN o un PRAGMA de lectura).");
            }
            if (error.isEmpty() && !head.ok) {
                error = head.error;
            }
            for (int i = 0; i < head.columns.size(); ++i) {
                // What the first rows hold says how to align the column.
                QString kind = QStringLiteral("text");
                for (const QVariantList &row : std::as_const(head.rows)) {
                    const QVariant &cell = row.at(i);
                    if (cell.isNull()) {
                        continue;
                    }
                    kind = cell.typeId() == QMetaType::LongLong || cell.typeId() == QMetaType::Int ? QStringLiteral("integer")
                        : cell.typeId() == QMetaType::Double ? QStringLiteral("real") : QStringLiteral("text");
                    break;
                }
                m_names.append(head.columns.at(i));
                m_kinds.append(kind);
                m_fields.append(QVariantMap{{QStringLiteral("name"), head.columns.at(i)}, {QStringLiteral("type"), QString()}, {QStringLiteral("kind"), kind},
                                            {QStringLiteral("pk"), 0}, {QStringLiteral("notNull"), false}, {QStringLiteral("fkTable"), QString()}, {QStringLiteral("fkColumn"), QString()}});
            }
        }
    }
    if (m_sortColumn >= m_names.size()) {
        m_sortColumn = -1;
    }
    setError(error);
    if (error.isEmpty()) {
        recount();
    }
    endResetModel();
    Q_EMIT fieldsChanged();
    Q_EMIT countChanged();
}

void GridModel::recount()
{
    m_count = 0;
    m_total = -1;
    if (!m_db || !m_db->isOpen() || m_names.isEmpty()) {
        return;
    }
    if (m_static) {
        m_count = int(m_staticRows.size());
        return;
    }
    QVariantList binds;
    const QString condition = where(&binds);
    Result result;
    if (m_table.isEmpty()) {
        // Named parameters of the query and the positional ones of the filter
        // cannot share a statement: a query is counted with its filters inlined.
        QString sql = QStringLiteral("SELECT count(*) FROM (") + viewSql() + QLatin1Char(')');
        result = m_db->queryNamed(sql, m_parameters);
    } else {
        result = m_db->query(QStringLiteral("SELECT count(*) FROM ") + source() + condition, binds);
    }
    if (!result.ok) {
        setError(result.error);
        return;
    }
    const qint64 count = result.rows.value(0).value(0).toLongLong();
    m_count = int(qMin<qint64>(count, 2000000000));
    if (!m_table.isEmpty()) {
        m_total = condition.isEmpty() ? count : m_db->rowCount(m_table);
    }
}

void GridModel::refresh()
{
    beginResetModel();
    m_pages.clear();
    m_recent.clear();
    if (m_error.isEmpty() || !m_names.isEmpty()) {
        setError(QString());
        recount();
    }
    endResetModel();
    Q_EMIT countChanged();
}

void GridModel::external()
{
    if (m_own) {
        return;
    }
    if (m_static) {
        reload();
        return;
    }
    // Rows changed elsewhere (another tab, undo, a script).
    const int before = m_count;
    m_pages.clear();
    m_recent.clear();
    recount();
    if (m_count != before) {
        beginResetModel();
        endResetModel();
        Q_EMIT countChanged();
    } else if (m_count > 0 && !m_names.isEmpty()) {
        Q_EMIT dataChanged(index(0, 0), index(m_count - 1, int(m_names.size()) - 1));
    }
}

int GridModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_count;
}

int GridModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_names.size());
}

QHash<int, QByteArray> GridModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"}, {ValueRole, "value"}, {NullRole, "isNull"}, {KindRole, "kind"}};
}

const QVariantList *GridModel::rowAt(int row) const
{
    if (row < 0 || row >= m_count || !m_db) {
        return nullptr;
    }
    if (m_static) {
        return &m_staticRows.at(row);
    }
    const int page = row / pageSize;
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        QStringList select;
        for (const QString &key : m_keyColumns) {
            select.append(Sql::ident(key));
        }
        Result result;
        if (m_table.isEmpty()) {
            const QString sql = QStringLiteral("SELECT * FROM (") + viewSql() + QStringLiteral(") LIMIT %1 OFFSET %2").arg(pageSize).arg(page * pageSize);
            result = m_db->queryNamed(sql, m_parameters);
        } else {
            for (const QString &name : m_names) {
                select.append(Sql::ident(name));
            }
            QVariantList binds;
            const QString condition = where(&binds);
            binds.append(pageSize);
            binds.append(page * pageSize);
            result = m_db->query(QStringLiteral("SELECT %1 FROM %2%3%4 LIMIT ? OFFSET ?").arg(select.join(QStringLiteral(", ")), source(), condition, order()), binds);
        }
        if (!result.ok) {
            return nullptr;
        }
        it = m_pages.insert(page, result.rows);
        m_recent.append(page);
        while (m_recent.size() > 60) {
            m_pages.remove(m_recent.takeFirst());
        }
    }
    const int at = row % pageSize;
    return at < it->size() ? &it->at(at) : nullptr;
}

QVariantList GridModel::keyOf(const QVariantList &row) const
{
    return row.mid(0, m_keyColumns.size());
}

QString GridModel::display(const QVariant &value)
{
    if (value.isNull() || !value.isValid()) {
        return {};
    }
    switch (value.typeId()) {
    case QMetaType::Double: {
        const double number = value.toDouble();
        QString text = QString::number(number, 'g', 15);
        return text;
    }
    case QMetaType::QByteArray:
        return QStringLiteral("BLOB (%L1 bytes)").arg(value.toByteArray().size());
    default:
        return value.toString();
    }
}

QVariant GridModel::value(int row, int column) const
{
    const QVariantList *cells = rowAt(row);
    const int at = column + (m_static || m_table.isEmpty() ? 0 : int(m_keyColumns.size()));
    if (!cells || column < 0 || at >= cells->size()) {
        return {};
    }
    const QVariant &cell = cells->at(at);
    // A blob has no business crossing into QML whole.
    return cell.typeId() == QMetaType::QByteArray ? QVariant(display(cell)) : cell;
}

QString GridModel::text(int row, int column) const
{
    return display(value(row, column));
}

QVariant GridModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
        return text(index.row(), index.column());
    case ValueRole:
        return value(index.row(), index.column());
    case NullRole: {
        const QVariant cell = value(index.row(), index.column());
        return cell.isNull() || !cell.isValid();
    }
    case KindRole:
        return m_kinds.value(index.column());
    }
    return {};
}

QVariant GridModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }
    return orientation == Qt::Horizontal ? QVariant(m_names.value(section)) : QVariant(section + 1);
}

QVariantMap GridModel::record(int row) const
{
    QVariantMap map;
    if (!rowAt(row)) {
        return map;
    }
    for (int i = 0; i < m_names.size(); ++i) {
        map.insert(m_names.at(i), value(row, i));
    }
    return map;
}

bool GridModel::setValue(int row, int column, const QVariant &typed)
{
    const QVariantList *cells = rowAt(row);
    if (!cells || !editable() || column < 0 || column >= m_names.size()) {
        return false;
    }
    const QVariant stored = Database::coerce(m_kinds.at(column), typed);
    const int at = column + int(m_keyColumns.size());
    const QVariant &old = cells->at(at);
    if (old == stored && old.isNull() == stored.isNull() && old.typeId() == stored.typeId()) {
        return true;
    }
    QVariantList newKey;
    m_own = true;
    const bool ok = m_db->updateCell(m_table, keyOf(*cells), m_names.at(column), stored, &newKey);
    m_own = false;
    if (!ok) {
        setError(m_db->lastError());
        return false;
    }
    setError(QString());
    // The row stays where it is until the next refresh, as it was typed.
    QVariantList &cached = m_pages[row / pageSize][row % pageSize];
    cached[at] = stored;
    for (int i = 0; i < newKey.size() && i < m_keyColumns.size(); ++i) {
        cached[i] = newKey.at(i);
    }
    Q_EMIT dataChanged(index(row, column), index(row, column));
    return true;
}

bool GridModel::setNull(int row, int column)
{
    return setValue(row, column, QVariant());
}

int GridModel::insert(const QVariantMap &values)
{
    if (!editable()) {
        return -2;
    }
    QVariantMap stored;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const int column = int(m_names.indexOf(it.key()));
        if (column < 0) {
            continue;
        }
        const QVariant value = Database::coerce(m_kinds.at(column), it.value());
        // What was left empty takes the default of its column.
        if (value.isValid() && !value.isNull() && !(value.typeId() == QMetaType::QString && value.toString().isEmpty())) {
            stored.insert(it.key(), value);
        }
    }
    if (!m_linkColumn.isEmpty() && !stored.contains(m_linkColumn) && m_linkValue.isValid()) {
        stored.insert(m_linkColumn, m_linkValue);
    }
    m_own = true;
    const QVariantList key = m_db->insertRow(m_table, stored);
    m_own = false;
    if (key.isEmpty()) {
        setError(m_db->lastError());
        return -2;
    }
    refresh();
    // Where did it land? Only the order of the table says so cheaply.
    if (m_sortColumn < 0 && m_keyColumns.size() == 1 && key.size() == 1) {
        QVariantList binds;
        QString condition = where(&binds);
        condition += (condition.isEmpty() ? QStringLiteral(" WHERE ") : QStringLiteral(" AND ")) + Sql::ident(m_keyColumns.first()) + QStringLiteral(" < ?");
        binds.append(key.first());
        const Result before = m_db->query(QStringLiteral("SELECT count(*) FROM ") + source() + condition, binds);
        const int at = before.ok ? before.rows.value(0).value(0).toInt() : -1;
        const QVariantList *cells = rowAt(at);
        if (cells && keyOf(*cells).value(0) == key.first()) {
            return at;
        }
    }
    return -1;
}

bool GridModel::remove(const QVariantList &rows)
{
    if (!editable()) {
        return false;
    }
    QList<QVariantList> keys;
    for (const QVariant &row : rows) {
        const QVariantList *cells = rowAt(row.toInt());
        if (cells) {
            keys.append(keyOf(*cells));
        }
    }
    m_own = true;
    const bool ok = m_db->deleteRows(m_table, keys);
    m_own = false;
    if (!ok) {
        setError(m_db->lastError());
        return false;
    }
    refresh();
    return true;
}

int GridModel::find(const QString &text, int from, bool backwards) const
{
    if (text.isEmpty() || m_count == 0) {
        return -1;
    }
    // A scan of what the grid holds, a page at a time; it gives up after a
    // while rather than read a huge table to its end.
    const int limit = qMin(m_count, 20000);
    for (int step = 0; step < limit; ++step) {
        const int row = ((backwards ? from - step : from + step) % m_count + m_count) % m_count;
        for (int column = 0; column < m_names.size(); ++column) {
            if (this->text(row, column).contains(text, Qt::CaseInsensitive)) {
                return row;
            }
        }
    }
    return -1;
}

int GridModel::widthHint(int column) const
{
    if (column < 0 || column >= m_names.size()) {
        return 120;
    }
    const QString kind = m_kinds.at(column);
    int width = kind == QLatin1String("text") ? 180 : kind == QLatin1String("datetime") ? 160 : kind == QLatin1String("bool") ? 76 : 110;
    // Room for the whole name, its glyph and the sort arrow.
    width = qMax(width, int(m_names.at(column).size()) * 8 + 58);
    return qMin(width, 320);
}

// --- A chain of filters, and the board ---------------------------------------------

void GridModel::setConditions(const QVariantList &conditions)
{
    if (conditions == m_conditions) {
        return;
    }
    m_conditions = conditions;
    refresh();
    Q_EMIT viewChanged();
}

QStringList GridModel::conditionTerms(QVariantList *binds) const
{
    QStringList terms;
    for (const QVariant &entry : m_conditions) {
        const QVariantMap map = entry.toMap();
        const QString column = map.value(QStringLiteral("column")).toString();
        const int at = int(m_names.indexOf(column));
        if (at < 0) {
            continue;
        }
        const QString op = map.value(QStringLiteral("op")).toString();
        QVariant value = map.value(QStringLiteral("value"));
        // Comparisons are made with what the column stores; a pattern stays text.
        static const QStringList typed{QStringLiteral("eq"), QStringLiteral("ne"), QStringLiteral("gt"), QStringLiteral("ge"), QStringLiteral("lt"), QStringLiteral("le")};
        if (typed.contains(op)) {
            const QVariant stored = Database::coerce(m_kinds.at(at), value);
            if (stored.isValid() && !stored.isNull()) {
                value = stored;
            }
        }
        const QString term = Sql::condition(Sql::ident(column), op, value, binds);
        if (!term.isEmpty()) {
            terms.append(term);
        }
    }
    return terms;
}

QVariantList GridModel::operators()
{
    return Sql::operators();
}

QVariantList GridModel::keyAt(int row) const
{
    const QVariantList *cells = rowAt(row);
    return cells && !m_keyColumns.isEmpty() && !m_table.isEmpty() ? keyOf(*cells) : QVariantList();
}

bool GridModel::store(const QVariantList &key, const QString &column, const QVariant &value)
{
    const int at = int(m_names.indexOf(column));
    if (!editable() || at < 0 || key.size() != m_keyColumns.size()) {
        return false;
    }
    m_own = true;
    const bool ok = m_db->updateCell(m_table, key, column, Database::coerce(m_kinds.at(at), value));
    m_own = false;
    if (!ok) {
        setError(m_db->lastError());
        return false;
    }
    refresh();
    return true;
}

QVariantList GridModel::groups(const QString &column, int perGroup) const
{
    QVariantList out;
    const int at = int(m_names.indexOf(column));
    if (!m_db || !m_db->isOpen() || at < 0 || m_static) {
        return out;
    }
    QVariantList binds;
    const QString condition = where(&binds);
    const QString from = m_table.isEmpty() ? QLatin1Char('(') + viewSql() + QLatin1Char(')') : source() + condition;
    if (m_table.isEmpty()) {
        binds.clear();
    }
    const QString name = Sql::ident(column);
    const Result heads = m_table.isEmpty()
        ? m_db->queryNamed(QStringLiteral("SELECT %1, count(*) FROM %2 GROUP BY 1 ORDER BY %1 IS NULL, 1 LIMIT 60").arg(name, from), m_parameters)
        : m_db->query(QStringLiteral("SELECT %1, count(*) FROM %2 GROUP BY 1 ORDER BY %1 IS NULL, 1 LIMIT 60").arg(name, from), binds);
    if (!heads.ok) {
        return out;
    }
    // What a foreign key refers to reads better than its number.
    QHash<QString, QString> labels;
    const QVariantMap field = m_fields.value(at).toMap();
    const QString fkTable = field.value(QStringLiteral("fkTable")).toString();
    if (!fkTable.isEmpty()) {
        for (const QVariant &entry : m_db->lookup(fkTable, field.value(QStringLiteral("fkColumn")).toString(), 2000)) {
            const QVariantMap map = entry.toMap();
            if (!map.value(QStringLiteral("label")).toString().isEmpty()) {
                labels.insert(map.value(QStringLiteral("value")).toString(), map.value(QStringLiteral("label")).toString());
            }
        }
    }
    QStringList select;
    for (const QString &key : m_keyColumns) {
        select.append(Sql::ident(key));
    }
    for (const QString &each : m_names) {
        select.append(Sql::ident(each));
    }
    const int keys = m_table.isEmpty() ? 0 : int(m_keyColumns.size());
    const QString columns = m_table.isEmpty() ? QStringLiteral("*") : select.join(QStringLiteral(", "));
    for (const QVariantList &head : heads.rows) {
        const QVariant value = head.at(0);
        QVariantList groupBinds = binds;
        QString inGroup = m_table.isEmpty() || condition.isEmpty() ? QStringLiteral(" WHERE ") : QStringLiteral(" AND ");
        if (value.isNull()) {
            inGroup += name + QStringLiteral(" IS NULL");
        } else {
            inGroup += name + QStringLiteral(" = ?");
            groupBinds.append(value);
        }
        const QString sql = QStringLiteral("SELECT %1 FROM %2%3%4 LIMIT %5").arg(columns, from, inGroup, order()).arg(qMax(1, perGroup));
        Result rows;
        if (m_table.isEmpty()) {
            // Named and positional values cannot share a statement.
            rows = m_db->queryNamed(Sql::inlined(sql, groupBinds), m_parameters);
        } else {
            rows = m_db->query(sql, groupBinds);
        }
        QVariantList cards;
        for (const QVariantList &row : std::as_const(rows.rows)) {
            QVariantMap record;
            for (int i = 0; i < m_names.size() && i + keys < row.size(); ++i) {
                const QVariant &cell = row.at(i + keys);
                record.insert(m_names.at(i), cell.typeId() == QMetaType::QByteArray ? QVariant(display(cell)) : cell);
            }
            cards.append(QVariantMap{{QStringLiteral("key"), QVariant(row.mid(0, keys))}, {QStringLiteral("record"), record}});
        }
        const QString text = value.isNull() ? QString() : display(value);
        out.append(QVariantMap{{QStringLiteral("value"), value},
                               {QStringLiteral("empty"), value.isNull()},
                               {QStringLiteral("label"), value.isNull() ? QStringLiteral("Sin valor") : labels.value(value.toString(), text)},
                               {QStringLiteral("count"), head.at(1).toLongLong()},
                               {QStringLiteral("rows"), cards}});
    }
    return out;
}
