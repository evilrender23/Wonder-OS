#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

class Database;

struct ColumnDef {
    QString name;
    QString type;
    bool notNull = false;
    /// 0, or its place in the primary key.
    int pk = 0;
    bool unique = false;
    bool autoIncrement = false;
    /// As typed: Sql::defaultClause() makes it safe.
    QString defaultValue;
    /// The name this column had in the table being altered; empty for a new one.
    QString origin;
    QString fkTable;
    QString fkColumn;
    QString fkOnDelete;
    QString fkOnUpdate;
};

/// A foreign key over several columns: kept as it was, the designer does not edit it.
struct CompositeKey {
    QStringList columns;
    QString table;
    QStringList refColumns;
    QString onDelete;
    QString onUpdate;
};

struct TableDef {
    QString name;
    QList<ColumnDef> columns;
    QList<CompositeKey> compositeKeys;
    bool withoutRowid = false;
};

/// Tables as a list of columns, and the DDL that makes the database match.
namespace Designer
{
TableDef fromVariant(const QVariantMap &map);
QVariantMap toVariant(const TableDef &def);
/// The table as it is now; every column's origin is its own name.
TableDef read(const Database &db, const QString &table);
/// What is wrong with a definition, in words; empty when nothing is.
QString validate(const TableDef &def);
QString createSql(const TableDef &def, const QString &nameOverride = {});

bool create(Database &db, const TableDef &def, QString *error);
/// The statements alter() would run, to show them first.
QStringList plan(const Database &db, const QString &oldName, const TableDef &def);
/// Makes `oldName` look like `def`: renames with ALTER TABLE, and anything
/// else by rebuilding the table inside one transaction — new table, rows
/// copied, old one dropped, indexes and triggers made again — that is
/// rolled back whole if a step fails or a foreign key is left dangling.
bool alter(Database &db, const QString &oldName, const TableDef &def, QString *error);
/// What a rebuild cannot carry over (CHECK constraints, …), in words.
QStringList losses(const Database &db, const QString &table);

bool dropTable(Database &db, const QString &table, QString *error);
bool renameTable(Database &db, const QString &table, const QString &name, QString *error);
bool createIndex(Database &db, const QString &table, const QString &name, const QStringList &columns, bool unique, QString *error);
bool dropIndex(Database &db, const QString &name, QString *error);
bool createView(Database &db, const QString &name, const QString &select, QString *error);
}
