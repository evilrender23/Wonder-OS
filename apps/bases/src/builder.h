#pragma once

#include <QString>
#include <QVariant>

class Database;

/// The visual query builder, and the definitions Bases makes by itself.
namespace Builder
{
/// The SELECT a design stands for. A design is {tables: [name…], columns:
/// [{table, column, alias, show, criterion, sort (asc|desc), total
/// (group|sum|count|avg|min|max)}], distinct, limit}. Tables are joined
/// through the foreign keys there are between them; the ones no key reaches
/// are left as a cross join. Empty when there is nothing to select from.
QString sql(const Database &db, const QVariantMap &design);
/// The joins sql() would write: [{table, column, refTable, refColumn}]
QVariantList joins(const Database &db, const QStringList &tables);

/// A form with every field of a table and a subform per table that refers to it:
/// {table, columns, fields: [{name, label, show}], subforms: [{table, column, show}]}
QVariantMap defaultForm(const Database &db, const QString &table);
/// A report with every column of a table or of a statement:
/// {title, source, sql, columns: [{name, label, show, total}], groupBy, landscape}
QVariantMap defaultReport(Database &db, const QString &source);
}
