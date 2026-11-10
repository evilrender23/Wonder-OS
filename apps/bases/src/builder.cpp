#include "builder.h"

#include "database.h"
#include "sqlutil.h"

#include <QSet>

namespace Builder
{

QVariantList joins(const Database &db, const QStringList &tables)
{
    QVariantList list;
    if (tables.size() < 2) {
        return list;
    }
    const QVariantList relations = db.relations();
    QStringList joined = {tables.first()};
    QStringList waiting = tables.mid(1);
    // Each pass joins the tables a key ties to the ones already in.
    bool progress = true;
    while (progress && !waiting.isEmpty()) {
        progress = false;
        for (int i = 0; i < waiting.size(); ++i) {
            const QString table = waiting.at(i);
            for (const QVariant &item : relations) {
                const QVariantMap relation = item.toMap();
                const QString from = relation.value(QStringLiteral("table")).toString();
                const QString to = relation.value(QStringLiteral("refTable")).toString();
                const bool forward = from.compare(table, Qt::CaseInsensitive) == 0 && joined.contains(to, Qt::CaseInsensitive);
                const bool backward = to.compare(table, Qt::CaseInsensitive) == 0 && joined.contains(from, Qt::CaseInsensitive);
                if (!forward && !backward) {
                    continue;
                }
                list.append(QVariantMap{
                    {QStringLiteral("table"), table},
                    {QStringLiteral("column"), relation.value(forward ? QStringLiteral("column") : QStringLiteral("refColumn"))},
                    {QStringLiteral("refTable"), forward ? to : from},
                    {QStringLiteral("refColumn"), relation.value(forward ? QStringLiteral("refColumn") : QStringLiteral("column"))},
                });
                joined.append(table);
                waiting.removeAt(i);
                progress = true;
                break;
            }
            if (progress) {
                break;
            }
        }
    }
    return list;
}

QString sql(const Database &db, const QVariantMap &design)
{
    QStringList tables;
    for (const QString &table : design.value(QStringLiteral("tables")).toStringList()) {
        if (db.exists(table) && !tables.contains(table, Qt::CaseInsensitive)) {
            tables.append(table);
        }
    }
    if (tables.isEmpty()) {
        return {};
    }
    static const QSet<QString> aggregates = {QStringLiteral("sum"), QStringLiteral("count"), QStringLiteral("avg"), QStringLiteral("min"), QStringLiteral("max")};
    const bool several = tables.size() > 1;
    const auto qualified = [&](const QVariantMap &column) {
        const QString name = Sql::ident(column.value(QStringLiteral("column")).toString());
        return several ? Sql::ident(column.value(QStringLiteral("table")).toString()) + QLatin1Char('.') + name : name;
    };

    QList<QVariantMap> columns;
    bool grouped = false;
    for (const QVariant &item : design.value(QStringLiteral("columns")).toList()) {
        const QVariantMap column = item.toMap();
        const QString table = column.value(QStringLiteral("table")).toString();
        if (!tables.contains(table, Qt::CaseInsensitive) || !db.columnNames(table).contains(column.value(QStringLiteral("column")).toString(), Qt::CaseInsensitive)) {
            continue;
        }
        columns.append(column);
        grouped = grouped || !column.value(QStringLiteral("total")).toString().isEmpty();
    }

    QStringList select;
    QStringList where;
    QStringList having;
    QStringList groupBy;
    QStringList orderBy;
    for (const QVariantMap &column : std::as_const(columns)) {
        const QString total = column.value(QStringLiteral("total")).toString().toLower();
        const bool aggregate = aggregates.contains(total);
        const QString plain = qualified(column);
        const QString expression = aggregate ? total.toUpper() + QLatin1Char('(') + plain + QLatin1Char(')') : plain;
        const QString alias = column.value(QStringLiteral("alias")).toString().trimmed();
        if (column.value(QStringLiteral("show"), true).toBool()) {
            QString item = expression;
            if (!alias.isEmpty()) {
                item += QStringLiteral(" AS ") + Sql::ident(alias);
            } else if (aggregate) {
                item += QStringLiteral(" AS ") + Sql::ident(total + QLatin1Char('_') + column.value(QStringLiteral("column")).toString());
            }
            select.append(item);
        }
        if (grouped && !aggregate) {
            groupBy.append(plain);
        }
        const QString criterion = Sql::criterionInline(expression, column.value(QStringLiteral("criterion")).toString());
        if (!criterion.isEmpty()) {
            (aggregate ? having : where).append(criterion);
        }
        const QString sort = column.value(QStringLiteral("sort")).toString().toLower();
        if (sort == QLatin1String("asc") || sort == QLatin1String("desc")) {
            orderBy.append(expression + (sort == QLatin1String("desc") ? QStringLiteral(" DESC") : QString()));
        }
    }
    if (select.isEmpty()) {
        select.append(QStringLiteral("*"));
    }

    QString text = QStringLiteral("SELECT ");
    if (design.value(QStringLiteral("distinct")).toBool()) {
        text += QStringLiteral("DISTINCT ");
    }
    text += select.join(QStringLiteral(", ")) + QStringLiteral("\nFROM ") + Sql::ident(tables.first());
    QStringList joined = {tables.first()};
    for (const QVariant &item : joins(db, tables)) {
        const QVariantMap join = item.toMap();
        const QString table = join.value(QStringLiteral("table")).toString();
        text += QStringLiteral("\nJOIN %1 ON %1.%2 = %3.%4")
                    .arg(Sql::ident(table), Sql::ident(join.value(QStringLiteral("column")).toString()),
                         Sql::ident(join.value(QStringLiteral("refTable")).toString()), Sql::ident(join.value(QStringLiteral("refColumn")).toString()));
        joined.append(table);
    }
    for (const QString &table : std::as_const(tables)) {
        if (!joined.contains(table, Qt::CaseInsensitive)) {
            text += QStringLiteral("\nCROSS JOIN ") + Sql::ident(table);
        }
    }
    if (!where.isEmpty()) {
        text += QStringLiteral("\nWHERE ") + where.join(QStringLiteral(" AND "));
    }
    if (grouped && !groupBy.isEmpty()) {
        text += QStringLiteral("\nGROUP BY ") + groupBy.join(QStringLiteral(", "));
    }
    if (!having.isEmpty()) {
        text += QStringLiteral("\nHAVING ") + having.join(QStringLiteral(" AND "));
    }
    if (!orderBy.isEmpty()) {
        text += QStringLiteral("\nORDER BY ") + orderBy.join(QStringLiteral(", "));
    }
    const int limit = design.value(QStringLiteral("limit")).toInt();
    if (limit > 0) {
        text += QStringLiteral("\nLIMIT %1").arg(limit);
    }
    return text;
}

QVariantMap defaultForm(const Database &db, const QString &table)
{
    QVariantList fields;
    for (const QString &name : db.columnNames(table)) {
        fields.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("label"), Sql::pretty(name)}, {QStringLiteral("show"), true}});
    }
    QVariantList subforms;
    for (const QVariant &item : db.relations()) {
        const QVariantMap relation = item.toMap();
        if (relation.value(QStringLiteral("refTable")).toString().compare(table, Qt::CaseInsensitive) == 0) {
            subforms.append(QVariantMap{{QStringLiteral("table"), relation.value(QStringLiteral("table"))},
                                        {QStringLiteral("column"), relation.value(QStringLiteral("column"))},
                                        {QStringLiteral("refColumn"), relation.value(QStringLiteral("refColumn"))},
                                        {QStringLiteral("show"), true}});
        }
    }
    return {
        {QStringLiteral("table"), table},
        {QStringLiteral("columns"), fields.size() > 6 ? 2 : 1},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("subforms"), subforms},
    };
}

QVariantMap defaultReport(Database &db, const QString &source)
{
    const bool table = db.exists(source);
    QStringList names;
    QStringList kinds;
    if (table) {
        for (const QVariant &item : db.columns(source)) {
            names.append(item.toMap().value(QStringLiteral("name")).toString());
            kinds.append(item.toMap().value(QStringLiteral("kind")).toString());
        }
    } else if (Sql::selects(source) && Sql::split(source).size() == 1) {
        const Result head = db.query(QStringLiteral("SELECT * FROM (%1) LIMIT 1").arg(Sql::trimmed(source)));
        names = head.columns;
        for (int i = 0; i < names.size(); ++i) {
            const QVariant value = head.rows.value(0).value(i);
            kinds.append(value.typeId() == QMetaType::Double ? QStringLiteral("real") : value.typeId() == QMetaType::LongLong ? QStringLiteral("integer") : QStringLiteral("text"));
        }
    }
    QVariantList columns;
    for (int i = 0; i < names.size(); ++i) {
        columns.append(QVariantMap{{QStringLiteral("name"), names.at(i)}, {QStringLiteral("label"), Sql::pretty(names.at(i))}, {QStringLiteral("show"), true},
                                   // Money and amounts add up; identifiers do not.
                                   {QStringLiteral("total"), kinds.at(i) == QLatin1String("real") ? QStringLiteral("sum") : QString()}});
    }
    return {
        {QStringLiteral("title"), table ? Sql::pretty(source) : QStringLiteral("Informe")},
        {QStringLiteral("source"), table ? source : QString()},
        {QStringLiteral("sql"), table ? QString() : Sql::trimmed(source)},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("groupBy"), QString()},
        {QStringLiteral("landscape"), names.size() > 6},
    };
}

}
