#include "transfer.h"

#include "database.h"
#include "designer.h"
#include "sqlutil.h"

#include <algorithm>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <functional>

namespace Transfer
{

QList<QStringList> parseCsv(const QString &text, QChar separator, int maxRows)
{
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    // A field that opened with a quote keeps even the spaces after it closes.
    bool wasQuoted = false;
    const int size = int(text.size());
    const auto endField = [&] {
        row.append(field);
        field.clear();
        wasQuoted = false;
    };
    const auto endRow = [&] {
        endField();
        // A blank line is no record.
        if (row.size() > 1 || !row.first().isEmpty()) {
            rows.append(row);
        }
        row.clear();
    };
    for (int i = 0; i < size; ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < size && text.at(i + 1) == QLatin1Char('"')) {
                    field += c;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"') && field.isEmpty() && !wasQuoted) {
            quoted = true;
            wasQuoted = true;
        } else if (c == separator) {
            endField();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < size && text.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            endRow();
            if (maxRows >= 0 && rows.size() >= maxRows) {
                return rows;
            }
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !row.isEmpty() || wasQuoted) {
        endRow();
    }
    return rows;
}

QChar detectSeparator(const QString &text)
{
    const QString head = text.left(20000);
    QChar best = QLatin1Char(',');
    double bestScore = 0;
    for (const QChar candidate : {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')}) {
        const QList<QStringList> rows = parseCsv(head, candidate, 12);
        if (rows.isEmpty() || rows.first().size() < 2) {
            continue;
        }
        // Every line split into the same number of fields is the sign.
        int same = 0;
        for (const QStringList &row : rows) {
            same += row.size() == rows.first().size() ? 1 : 0;
        }
        const double score = double(same) / rows.size() * 100 + qMin(rows.first().size(), 50);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    }
    return best;
}

static QDate spanishDate(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{1,2})[/.-](\\d{1,2})[/.-](\\d{4})$"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? QDate(match.captured(3).toInt(), match.captured(2).toInt(), match.captured(1).toInt()) : QDate();
}

static bool isIsoDate(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"));
    return pattern.match(text).hasMatch() && QDate::fromString(text, Qt::ISODate).isValid();
}

static bool isIsoDateTime(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}[T ]\\d{2}:\\d{2}(:\\d{2}(\\.\\d+)?)?(Z|[+-]\\d{2}:?\\d{2})?$"));
    return pattern.match(text).hasMatch() && QDate::fromString(text.left(10), Qt::ISODate).isValid();
}

static bool isInteger(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[+-]?\\d{1,18}$"));
    // «007» and «+34…» are codes, not numbers: a leading zero would be lost.
    return pattern.match(text).hasMatch() && !(text.size() > 1 && text.startsWith(QLatin1Char('0'))) && !text.startsWith(QLatin1Char('+'));
}

static bool isReal(const QString &text, bool decimalComma)
{
    static const QRegularExpression dot(QStringLiteral("^[+-]?(\\d+\\.?\\d*|\\.\\d+)([eE][+-]?\\d+)?$"));
    static const QRegularExpression comma(QStringLiteral("^[+-]?\\d+,\\d+$"));
    return dot.match(text).hasMatch() || (decimalComma && comma.match(text).hasMatch());
}

static bool isBool(const QString &text)
{
    static const QSet<QString> words = {QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("sí"), QStringLiteral("si"), QStringLiteral("no"),
                                        QStringLiteral("verdadero"), QStringLiteral("falso"), QStringLiteral("yes")};
    return words.contains(text.toLower());
}

QString detectType(const QStringList &values, bool decimalComma)
{
    bool any = false;
    bool integer = true;
    bool real = true;
    bool date = true;
    bool dateTime = true;
    bool boolean = true;
    for (const QString &raw : values) {
        const QString value = raw.trimmed();
        if (value.isEmpty()) {
            continue;
        }
        any = true;
        integer = integer && isInteger(value);
        real = real && isReal(value, decimalComma) && !(value.size() > 1 && value.startsWith(QLatin1Char('0')) && value.at(1).isDigit());
        date = date && (isIsoDate(value) || spanishDate(value).isValid());
        dateTime = dateTime && isIsoDateTime(value);
        boolean = boolean && isBool(value);
        if (!integer && !real && !date && !dateTime && !boolean) {
            break;
        }
    }
    if (!any) {
        return QStringLiteral("TEXT");
    }
    return integer ? QStringLiteral("INTEGER") : real ? QStringLiteral("REAL") : date ? QStringLiteral("DATE") : dateTime ? QStringLiteral("DATETIME")
        : boolean ? QStringLiteral("BOOLEAN") : QStringLiteral("TEXT");
}

/// A value of a CSV file as what a column of that type stores.
static QVariant converted(const QString &raw, const QString &type, bool decimalComma)
{
    const QString value = raw.trimmed();
    if (type == QLatin1String("TEXT")) {
        return raw.isEmpty() ? QVariant() : QVariant(raw);
    }
    if (value.isEmpty()) {
        return {};
    }
    if (type == QLatin1String("INTEGER")) {
        bool ok = false;
        const qlonglong number = value.toLongLong(&ok);
        return ok ? QVariant(number) : QVariant(raw);
    }
    if (type == QLatin1String("REAL")) {
        QString dotted = value;
        if (decimalComma && !dotted.contains(QLatin1Char('.'))) {
            dotted.replace(QLatin1Char(','), QLatin1Char('.'));
        }
        bool ok = false;
        const double number = dotted.toDouble(&ok);
        return ok ? QVariant(number) : QVariant(raw);
    }
    if (type == QLatin1String("DATE")) {
        const QDate date = spanishDate(value);
        return date.isValid() ? QVariant(date.toString(Qt::ISODate)) : QVariant(value);
    }
    if (type == QLatin1String("BOOLEAN")) {
        const QString lower = value.toLower();
        if (lower == QLatin1String("true") || lower == QStringLiteral("sí") || lower == QLatin1String("si") || lower == QLatin1String("verdadero") || lower == QLatin1String("yes") || lower == QLatin1String("1")) {
            return 1;
        }
        if (lower == QLatin1String("false") || lower == QLatin1String("no") || lower == QLatin1String("falso") || lower == QLatin1String("0")) {
            return 0;
        }
    }
    return value;
}

QString readText(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("No se puede leer «%1».").arg(QFileInfo(path).fileName());
        }
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(bytes.mid(3));
    }
    if (bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringDecoder::Utf16);
        return decoder(bytes);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(bytes);
    return utf8.hasError() ? QString::fromLatin1(bytes) : text;
}

static bool looksLikeHeader(const QList<QStringList> &rows)
{
    if (rows.isEmpty()) {
        return false;
    }
    QSet<QString> seen;
    for (const QString &cell : rows.first()) {
        const QString name = cell.trimmed();
        // Names are words: no blanks, no numbers, no repeats.
        if (name.isEmpty() || isReal(name, true) || isIsoDate(name) || seen.contains(name.toLower())) {
            return false;
        }
        seen.insert(name.toLower());
    }
    return true;
}

namespace
{
struct Parsed {
    QChar separator;
    bool header = false;
    bool decimalComma = false;
    QStringList names;
    QStringList types;
    QList<QStringList> rows;
};

Parsed parse(const QString &text, const QString &separator, int header, int maxRows = -1)
{
    Parsed parsed;
    parsed.separator = separator.isEmpty() ? detectSeparator(text) : (separator == QLatin1String("\\t") || separator == QLatin1String("tab") ? QChar(QLatin1Char('\t')) : separator.at(0));
    parsed.rows = parseCsv(text, parsed.separator, maxRows);
    // A line of nothing but separators is not a record.
    parsed.rows.removeIf([](const QStringList &row) {
        return std::all_of(row.cbegin(), row.cend(), [](const QString &field) { return field.trimmed().isEmpty(); });
    });
    parsed.header = header < 0 ? looksLikeHeader(parsed.rows) : header > 0;
    parsed.decimalComma = parsed.separator != QLatin1Char(',');
    int width = 0;
    for (const QStringList &row : std::as_const(parsed.rows)) {
        width = qMax(width, int(row.size()));
    }
    QStringList first;
    if (parsed.header && !parsed.rows.isEmpty()) {
        first = parsed.rows.takeFirst();
    }
    QSet<QString> seen;
    for (int i = 0; i < width; ++i) {
        QString name = first.value(i).trimmed();
        if (name.isEmpty()) {
            name = QStringLiteral("campo%1").arg(i + 1);
        }
        QString unique = name;
        for (int n = 2; seen.contains(unique.toLower()); ++n) {
            unique = QStringLiteral("%1_%2").arg(name).arg(n);
        }
        seen.insert(unique.toLower());
        parsed.names.append(unique);
        QStringList values;
        for (const QStringList &row : std::as_const(parsed.rows)) {
            values.append(row.value(i));
        }
        parsed.types.append(detectType(values, parsed.decimalComma));
    }
    return parsed;
}

QVariantMap failure(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
}

QVariantMap previewCsv(const QString &path, const QString &separator, int header)
{
    QString error;
    const QString text = readText(path, &error);
    if (!error.isEmpty()) {
        return failure(error);
    }
    const Parsed parsed = parse(text, separator, header);
    if (parsed.names.isEmpty()) {
        return failure(QStringLiteral("El archivo no tiene datos."));
    }
    QVariantList columns;
    for (int i = 0; i < parsed.names.size(); ++i) {
        columns.append(QVariantMap{{QStringLiteral("name"), parsed.names.at(i)}, {QStringLiteral("type"), parsed.types.at(i)}});
    }
    QVariantList rows;
    for (int i = 0; i < parsed.rows.size() && i < 8; ++i) {
        QStringList row = parsed.rows.at(i);
        while (row.size() < parsed.names.size()) {
            row.append(QString());
        }
        rows.append(QVariant(row));
    }
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("separator"), parsed.separator == QLatin1Char('\t') ? QStringLiteral("\\t") : QString(parsed.separator)},
        {QStringLiteral("header"), parsed.header},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("rows"), rows},
        {QStringLiteral("count"), int(parsed.rows.size())},
    };
}

QVariantMap importCsvText(Database &db, const QString &text, const QString &table, const QVariantMap &options)
{
    const QString name = table.trimmed();
    if (name.isEmpty()) {
        return failure(QStringLiteral("La tabla necesita un nombre."));
    }
    if (!db.isOpen() || db.readOnly()) {
        return failure(QStringLiteral("La base de datos no se puede modificar."));
    }
    Parsed parsed = parse(text, options.value(QStringLiteral("separator")).toString(), options.value(QStringLiteral("header"), -1).toInt());
    if (parsed.names.isEmpty()) {
        return failure(QStringLiteral("El archivo no tiene datos."));
    }
    const bool append = options.value(QStringLiteral("append")).toBool();
    const bool exists = db.exists(name);
    if (append != exists) {
        return failure(append ? QStringLiteral("No existe la tabla «%1».").arg(name) : QStringLiteral("Ya hay una tabla o una vista llamada «%1».").arg(name));
    }

    // Which column of the table each field of the file goes to; empty skips it.
    QStringList targets;
    QStringList types = parsed.types;
    TableDef def;
    if (append) {
        const QVariantList columns = db.columns(name);
        QStringList existing;
        QHash<QString, QString> declared;
        for (const QVariant &column : columns) {
            const QVariantMap map = column.toMap();
            if (map.value(QStringLiteral("generated")).toBool()) {
                continue;
            }
            existing.append(map.value(QStringLiteral("name")).toString());
            declared.insert(existing.last(), map.value(QStringLiteral("type")).toString());
        }
        bool matched = false;
        for (int i = 0; i < parsed.names.size(); ++i) {
            QString target;
            if (parsed.header) {
                for (const QString &candidate : std::as_const(existing)) {
                    if (candidate.compare(parsed.names.at(i), Qt::CaseInsensitive) == 0) {
                        target = candidate;
                    }
                }
            } else {
                target = existing.value(i);
            }
            matched = matched || !target.isEmpty();
            targets.append(target);
            // Dates written the Spanish way are still turned round.
            if (!target.isEmpty() && types.at(i) != QLatin1String("DATE")) {
                const QString kind = Sql::kind(declared.value(target));
                types[i] = kind == QLatin1String("integer") ? QStringLiteral("INTEGER") : kind == QLatin1String("real") ? QStringLiteral("REAL")
                    : kind == QLatin1String("bool") ? QStringLiteral("BOOLEAN") : kind == QLatin1String("text") ? QStringLiteral("TEXT") : types.at(i);
            }
        }
        if (!matched) {
            return failure(QStringLiteral("Ninguna columna del archivo coincide con las de «%1».").arg(name));
        }
    } else {
        const QVariantList overrides = options.value(QStringLiteral("columns")).toList();
        def.name = name;
        for (int i = 0; i < parsed.names.size(); ++i) {
            const QVariantMap over = overrides.value(i).toMap();
            if (over.value(QStringLiteral("skip")).toBool()) {
                targets.append(QString());
                continue;
            }
            ColumnDef column;
            column.name = over.value(QStringLiteral("name"), parsed.names.at(i)).toString().trimmed();
            column.type = over.value(QStringLiteral("type"), parsed.types.at(i)).toString().trimmed().toUpper();
            types[i] = column.type;
            def.columns.append(column);
            targets.append(column.name);
        }
        const QString wrong = Designer::validate(def);
        if (!wrong.isEmpty()) {
            return failure(wrong);
        }
    }

    QStringList into;
    QStringList marks;
    for (const QString &target : std::as_const(targets)) {
        if (!target.isEmpty()) {
            into.append(Sql::ident(target));
            marks.append(QStringLiteral("?"));
        }
    }
    if (!db.begin()) {
        return failure(db.lastError());
    }
    if (!append) {
        const Result made = db.query(Designer::createSql(def));
        if (!made.ok) {
            db.rollback();
            return failure(made.error);
        }
    }
    const QString insert = QStringLiteral("INSERT INTO %1 (%2) VALUES (%3)").arg(Sql::ident(name), into.join(QStringLiteral(", ")), marks.join(QStringLiteral(", ")));
    int count = 0;
    for (const QStringList &row : std::as_const(parsed.rows)) {
        QVariantList binds;
        for (int i = 0; i < targets.size(); ++i) {
            if (!targets.at(i).isEmpty()) {
                binds.append(converted(row.value(i), types.at(i), parsed.decimalComma));
            }
        }
        const Result result = db.query(insert, binds);
        if (!result.ok) {
            db.rollback();
            return failure(QStringLiteral("Fila %1: %2").arg(count + 1 + (parsed.header ? 1 : 0)).arg(result.error));
        }
        ++count;
    }
    if (!db.commit()) {
        const QString message = db.lastError();
        db.rollback();
        return failure(message);
    }
    db.touch(true);
    // The columns of the file that went nowhere, to say so.
    QStringList skipped;
    for (int i = 0; i < targets.size(); ++i) {
        if (targets.at(i).isEmpty()) {
            skipped.append(parsed.names.at(i));
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("rows"), count}, {QStringLiteral("table"), name}, {QStringLiteral("skipped"), skipped}};
}

QVariantMap importCsv(Database &db, const QString &path, const QString &table, const QVariantMap &options)
{
    QString error;
    const QString text = readText(path, &error);
    if (!error.isEmpty()) {
        return failure(error);
    }
    return importCsvText(db, text, table, options);
}

// --- Out -------------------------------------------------------------------------

QString csvField(const QString &value, QChar separator)
{
    if (value.contains(separator) || value.contains(QLatin1Char('"')) || value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))
        || value.startsWith(QLatin1Char(' ')) || value.endsWith(QLatin1Char(' '))) {
        QString quoted = value;
        quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    return value;
}

static QString selectOf(const Database &db, const QString &source)
{
    return db.exists(source) ? QStringLiteral("SELECT * FROM ") + Sql::ident(source) : Sql::trimmed(source);
}

/// Reads a SELECT a slice at a time, so that a big table never sits whole in memory.
static bool forEachRow(Database &db, const QString &select, QString *error, const std::function<void(const QStringList &)> &head,
                       const std::function<void(const QVariantList &)> &each)
{
    if (!Sql::selects(select) || Sql::split(select).size() != 1) {
        if (error) {
            *error = QStringLiteral("Solo se puede exportar una consulta SELECT.");
        }
        return false;
    }
    constexpr int slice = 20000;
    for (qint64 offset = 0;; offset += slice) {
        const Result result = db.query(QStringLiteral("SELECT * FROM (%1) LIMIT %2 OFFSET %3").arg(select).arg(slice).arg(offset));
        if (!result.ok) {
            if (error) {
                *error = result.error;
            }
            return false;
        }
        if (offset == 0) {
            head(result.columns);
        }
        for (const QVariantList &row : result.rows) {
            each(row);
        }
        if (result.rows.size() < slice) {
            return true;
        }
    }
}

static bool finish(QSaveFile &file, const QString &path, QString *error)
{
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    return true;
}

static QString plain(const QVariant &value)
{
    if (value.isNull()) {
        return {};
    }
    if (value.typeId() == QMetaType::QByteArray) {
        return QString::fromLatin1(value.toByteArray().toBase64());
    }
    if (value.typeId() == QMetaType::Double) {
        const QString text = Sql::literal(value);
        return text.endsWith(QLatin1String(".0")) ? text.chopped(2) : text;
    }
    return value.toString();
}

bool exportCsv(Database &db, const QString &source, const QString &path, QChar separator, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    const auto line = [&](const QStringList &fields) {
        QStringList out;
        for (const QString &field : fields) {
            out.append(csvField(field, separator));
        }
        file.write(out.join(separator).toUtf8());
        file.write("\r\n");
    };
    const bool ok = forEachRow(db, selectOf(db, source), error, line, [&](const QVariantList &row) {
        QStringList fields;
        for (const QVariant &value : row) {
            fields.append(plain(value));
        }
        line(fields);
    });
    if (!ok) {
        file.cancelWriting();
        return false;
    }
    return finish(file, path, error);
}

bool exportJson(Database &db, const QString &source, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    QStringList names;
    bool first = true;
    file.write("[");
    const bool ok = forEachRow(db, selectOf(db, source), error, [&](const QStringList &columns) { names = columns; }, [&](const QVariantList &row) {
        QJsonObject object;
        for (int i = 0; i < names.size(); ++i) {
            const QVariant &value = row.at(i);
            object.insert(names.at(i), value.isNull() ? QJsonValue() : value.typeId() == QMetaType::QByteArray
                ? QJsonValue(QString::fromLatin1(value.toByteArray().toBase64())) : QJsonValue::fromVariant(value));
        }
        file.write(first ? "\n  " : ",\n  ");
        first = false;
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    });
    if (!ok) {
        file.cancelWriting();
        return false;
    }
    file.write("\n]\n");
    return finish(file, path, error);
}

static bool writeInserts(Database &db, QSaveFile &file, const QString &select, const QString &table, QString *error)
{
    QString prefix;
    return forEachRow(db, select, error, [&](const QStringList &columns) {
        QStringList quoted;
        for (const QString &column : columns) {
            quoted.append(Sql::ident(column));
        }
        prefix = QStringLiteral("INSERT INTO %1 (%2) VALUES (").arg(Sql::ident(table), quoted.join(QStringLiteral(", ")));
    }, [&](const QVariantList &row) {
        QStringList values;
        for (const QVariant &value : row) {
            values.append(Sql::literal(value));
        }
        file.write((prefix + values.join(QStringLiteral(", ")) + QStringLiteral(");\n")).toUtf8());
    });
}

bool exportSql(Database &db, const QStringList &tables, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    const bool whole = tables.isEmpty();
    file.write("PRAGMA foreign_keys = OFF;\nBEGIN TRANSACTION;\n");
    const Result schema = db.query(QStringLiteral("SELECT type, name, tbl_name, sql FROM sqlite_schema WHERE sql IS NOT NULL AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY rowid"));
    if (!schema.ok) {
        if (error) {
            *error = schema.error;
        }
        file.cancelWriting();
        return false;
    }
    const auto wanted = [&](const QString &table) {
        return whole || tables.contains(table, Qt::CaseInsensitive);
    };
    // Tables and their rows first; what is built on them after.
    for (const QVariantList &row : schema.rows) {
        if (row.at(0).toString() != QLatin1String("table") || !wanted(row.at(1).toString())) {
            continue;
        }
        const QString name = row.at(1).toString();
        file.write((row.at(3).toString() + QStringLiteral(";\n")).toUtf8());
        // The stored columns only: a generated one cannot be inserted into.
        QStringList columns;
        for (const QVariant &column : db.columns(name)) {
            const QVariantMap map = column.toMap();
            if (!map.value(QStringLiteral("generated")).toBool()) {
                columns.append(Sql::ident(map.value(QStringLiteral("name")).toString()));
            }
        }
        if (columns.isEmpty()) {
            const Result info = db.query(QStringLiteral("SELECT name FROM pragma_table_info(?)"), {name});
            for (const QVariantList &column : info.rows) {
                columns.append(Sql::ident(column.first().toString()));
            }
        }
        if (!writeInserts(db, file, QStringLiteral("SELECT %1 FROM %2").arg(columns.join(QStringLiteral(", ")), Sql::ident(name)), name, error)) {
            file.cancelWriting();
            return false;
        }
    }
    for (const QVariantList &row : schema.rows) {
        const QString type = row.at(0).toString();
        if (type == QLatin1String("table")) {
            continue;
        }
        if (type == QLatin1String("view") ? wanted(row.at(1).toString()) : wanted(row.at(2).toString())) {
            file.write((row.at(3).toString() + QStringLiteral(";\n")).toUtf8());
        }
    }
    file.write("COMMIT;\nPRAGMA foreign_keys = ON;\n");
    return finish(file, path, error);
}

bool exportQuerySql(Database &db, const QString &sql, const QString &table, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    file.write("BEGIN TRANSACTION;\n");
    if (!writeInserts(db, file, Sql::trimmed(sql), table.isEmpty() ? QStringLiteral("consulta") : table, error)) {
        file.cancelWriting();
        return false;
    }
    file.write("COMMIT;\n");
    return finish(file, path, error);
}

// --- Access ---------------------------------------------------------------------

bool hasMdbTools()
{
    return !QStandardPaths::findExecutable(QStringLiteral("mdb-tables")).isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("mdb-export")).isEmpty();
}

static bool runTool(const QString &program, const QStringList &arguments, QByteArray *output)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return false;
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(120000)) {
        process.kill();
        return false;
    }
    *output = process.readAllStandardOutput();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

QVariantMap importMdb(Database &db, const QString &path)
{
    if (!hasMdbTools()) {
        return failure(QStringLiteral("Para leer archivos de Access hace falta mdb-tools."));
    }
    QByteArray output;
    if (!runTool(QStringLiteral("mdb-tables"), {QStringLiteral("-1"), path}, &output)) {
        return failure(QStringLiteral("«%1» no es una base de Access que se pueda leer.").arg(QFileInfo(path).fileName()));
    }
    QStringList done;
    int rows = 0;
    for (const QString &table : QString::fromUtf8(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QByteArray csv;
        // ISO dates, so that they sort and are told from text.
        if (!runTool(QStringLiteral("mdb-export"), {QStringLiteral("-D"), QStringLiteral("%Y-%m-%d"), QStringLiteral("-T"), QStringLiteral("%Y-%m-%d %H:%M:%S"), path, table}, &csv)) {
            return failure(QStringLiteral("No se pudo leer la tabla «%1».").arg(table));
        }
        QString name = table;
        for (int n = 2; db.exists(name); ++n) {
            name = QStringLiteral("%1_%2").arg(table).arg(n);
        }
        const QVariantMap result = importCsvText(db, QString::fromUtf8(csv), name, {{QStringLiteral("separator"), QStringLiteral(",")}, {QStringLiteral("header"), 1}});
        if (!result.value(QStringLiteral("ok")).toBool()) {
            return failure(QStringLiteral("%1: %2").arg(table, result.value(QStringLiteral("error")).toString()));
        }
        done.append(name);
        rows += result.value(QStringLiteral("rows")).toInt();
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("tables"), done}, {QStringLiteral("rows"), rows}};
}

}
