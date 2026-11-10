#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class Database;

/// Rows in and out of the database: CSV, JSON, SQL dumps and Access files.
namespace Transfer
{
/// The records of a CSV text; quotes may hold separators and line breaks.
QList<QStringList> parseCsv(const QString &text, QChar separator, int maxRows = -1);
/// The one of `,` `;` tab and `|` that splits the first lines most evenly.
QChar detectSeparator(const QString &text);
/// INTEGER, REAL, DATE, DATETIME, BOOLEAN or TEXT: what every value of a
/// column fits. Empty values fit anything.
QString detectType(const QStringList &values, bool decimalComma);
/// A file as text: UTF-8 (with or without BOM), UTF-16 with BOM, else Latin-1.
QString readText(const QString &path, QString *error = nullptr);
/// One field of a CSV file, quoted when it must be.
QString csvField(const QString &value, QChar separator);

/// What importing a file would do, to show it first: {ok, error, separator,
/// header, columns: [{name, type}], rows: [[text…]] (the first ones), count}
QVariantMap previewCsv(const QString &path, const QString &separator = {}, int header = -1);
/// Imports a CSV file into `table`. Options: separator ("" detects it),
/// header (-1 detects, 0, 1), append (into a table that exists: columns are
/// matched by name when there is a header, else by place), columns
/// ([{name, type, skip}] to override what was detected for a new table).
/// {ok, error, rows, table, skipped (the columns of the file left out)}
QVariantMap importCsv(Database &db, const QString &path, const QString &table, const QVariantMap &options = {});
QVariantMap importCsvText(Database &db, const QString &text, const QString &table, const QVariantMap &options = {});

/// The rows of a SELECT (or of a table, given its name) to a file.
bool exportCsv(Database &db, const QString &source, const QString &path, QChar separator = QLatin1Char(','), QString *error = nullptr);
/// An array of objects, one per row; blobs go in base64.
bool exportJson(Database &db, const QString &source, const QString &path, QString *error = nullptr);
/// A script that makes the tables again with their rows, indexes, views and
/// triggers. No tables given: the whole database (Bases' own objects too).
bool exportSql(Database &db, const QStringList &tables, const QString &path, QString *error = nullptr);
/// A SELECT as INSERTs into a table of the name given.
bool exportQuerySql(Database &db, const QString &sql, const QString &table, const QString &path, QString *error = nullptr);

/// mdb-tools is installed, so Access files can be read.
bool hasMdbTools();
/// Every table of an .mdb/.accdb file becomes a table here. {ok, error, tables, rows}
QVariantMap importMdb(Database &db, const QString &path);
}
