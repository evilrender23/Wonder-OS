#include "designer.h"

#include "database.h"
#include "sqlutil.h"

#include <QMap>
#include <QSet>

namespace Designer
{

static const QStringList actions = {QStringLiteral("NO ACTION"), QStringLiteral("RESTRICT"), QStringLiteral("SET NULL"), QStringLiteral("SET DEFAULT"), QStringLiteral("CASCADE")};

static QString action(const QString &typed)
{
    const QString upper = typed.trimmed().toUpper();
    return actions.contains(upper) && upper != QLatin1String("NO ACTION") ? upper : QString();
}

TableDef fromVariant(const QVariantMap &map)
{
    TableDef def;
    def.name = map.value(QStringLiteral("name")).toString().trimmed();
    def.withoutRowid = map.value(QStringLiteral("withoutRowid")).toBool();
    for (const QVariant &item : map.value(QStringLiteral("columns")).toList()) {
        const QVariantMap c = item.toMap();
        ColumnDef column;
        column.name = c.value(QStringLiteral("name")).toString().trimmed();
        column.type = c.value(QStringLiteral("type")).toString().simplified();
        column.notNull = c.value(QStringLiteral("notNull")).toBool();
        column.pk = c.value(QStringLiteral("pk")).toInt();
        column.unique = c.value(QStringLiteral("unique")).toBool();
        column.autoIncrement = c.value(QStringLiteral("autoIncrement")).toBool();
        column.defaultValue = c.value(QStringLiteral("defaultValue")).toString();
        column.origin = c.value(QStringLiteral("origin")).toString();
        column.fkTable = c.value(QStringLiteral("fkTable")).toString();
        column.fkColumn = c.value(QStringLiteral("fkColumn")).toString();
        column.fkOnDelete = c.value(QStringLiteral("fkOnDelete")).toString();
        column.fkOnUpdate = c.value(QStringLiteral("fkOnUpdate")).toString();
        def.columns.append(column);
    }
    for (const QVariant &item : map.value(QStringLiteral("compositeKeys")).toList()) {
        const QVariantMap k = item.toMap();
        def.compositeKeys.append({k.value(QStringLiteral("columns")).toStringList(), k.value(QStringLiteral("table")).toString(),
                                  k.value(QStringLiteral("refColumns")).toStringList(), k.value(QStringLiteral("onDelete")).toString(),
                                  k.value(QStringLiteral("onUpdate")).toString()});
    }
    return def;
}

QVariantMap toVariant(const TableDef &def)
{
    QVariantList columns;
    for (const ColumnDef &column : def.columns) {
        columns.append(QVariantMap{
            {QStringLiteral("name"), column.name},
            {QStringLiteral("type"), column.type},
            {QStringLiteral("notNull"), column.notNull},
            {QStringLiteral("pk"), column.pk},
            {QStringLiteral("unique"), column.unique},
            {QStringLiteral("autoIncrement"), column.autoIncrement},
            {QStringLiteral("defaultValue"), column.defaultValue},
            {QStringLiteral("origin"), column.origin},
            {QStringLiteral("fkTable"), column.fkTable},
            {QStringLiteral("fkColumn"), column.fkColumn},
            {QStringLiteral("fkOnDelete"), column.fkOnDelete},
            {QStringLiteral("fkOnUpdate"), column.fkOnUpdate},
        });
    }
    QVariantList keys;
    for (const CompositeKey &key : def.compositeKeys) {
        keys.append(QVariantMap{
            {QStringLiteral("columns"), key.columns},
            {QStringLiteral("table"), key.table},
            {QStringLiteral("refColumns"), key.refColumns},
            {QStringLiteral("onDelete"), key.onDelete},
            {QStringLiteral("onUpdate"), key.onUpdate},
        });
    }
    return {
        {QStringLiteral("name"), def.name},
        {QStringLiteral("columns"), columns},
        {QStringLiteral("compositeKeys"), keys},
        {QStringLiteral("withoutRowid"), def.withoutRowid},
    };
}

TableDef read(const Database &db, const QString &table)
{
    TableDef def;
    def.name = table;
    def.withoutRowid = db.withoutRowid(table);
    for (const QVariant &item : db.columns(table)) {
        const QVariantMap c = item.toMap();
        if (c.value(QStringLiteral("generated")).toBool()) {
            continue;
        }
        ColumnDef column;
        column.name = c.value(QStringLiteral("name")).toString();
        column.origin = column.name;
        column.type = c.value(QStringLiteral("type")).toString();
        column.notNull = c.value(QStringLiteral("notNull")).toBool();
        column.pk = c.value(QStringLiteral("pk")).toInt();
        column.unique = c.value(QStringLiteral("unique")).toBool();
        column.autoIncrement = c.value(QStringLiteral("autoIncrement")).toBool();
        column.defaultValue = c.value(QStringLiteral("defaultValue")).toString();
        column.fkTable = c.value(QStringLiteral("fkTable")).toString();
        column.fkColumn = c.value(QStringLiteral("fkColumn")).toString();
        column.fkOnDelete = action(c.value(QStringLiteral("fkOnDelete")).toString());
        column.fkOnUpdate = action(c.value(QStringLiteral("fkOnUpdate")).toString());
        def.columns.append(column);
    }
    for (const QVariant &item : db.foreignKeys(table)) {
        const QVariantMap k = item.toMap();
        const QStringList columns = k.value(QStringLiteral("columns")).toStringList();
        if (columns.size() > 1) {
            def.compositeKeys.append({columns, k.value(QStringLiteral("table")).toString(), k.value(QStringLiteral("refColumns")).toStringList(),
                                      action(k.value(QStringLiteral("onDelete")).toString()), action(k.value(QStringLiteral("onUpdate")).toString())});
        }
    }
    return def;
}

QString validate(const TableDef &def)
{
    if (def.name.isEmpty()) {
        return QStringLiteral("La tabla necesita un nombre.");
    }
    if (def.name.startsWith(QLatin1String("sqlite_"), Qt::CaseInsensitive) || def.name.startsWith(QLatin1String("_wonderland_"), Qt::CaseInsensitive)) {
        return QStringLiteral("«%1» es un nombre reservado.").arg(def.name);
    }
    if (def.columns.isEmpty()) {
        return QStringLiteral("La tabla necesita al menos un campo.");
    }
    QSet<QString> seen;
    int keys = 0;
    for (const ColumnDef &column : def.columns) {
        if (column.name.isEmpty()) {
            return QStringLiteral("Hay un campo sin nombre.");
        }
        const QString lower = column.name.toLower();
        if (seen.contains(lower)) {
            return QStringLiteral("El campo «%1» está repetido.").arg(column.name);
        }
        seen.insert(lower);
        if (!Sql::validType(column.type)) {
            return QStringLiteral("«%1» no es un tipo válido para «%2».").arg(column.type, column.name);
        }
        if (column.pk > 0) {
            ++keys;
        }
        if (!column.fkTable.isEmpty() && column.fkColumn.isEmpty()) {
            return QStringLiteral("La relación de «%1» necesita un campo de «%2».").arg(column.name, column.fkTable);
        }
    }
    for (const ColumnDef &column : def.columns) {
        if (column.autoIncrement && (keys != 1 || column.pk == 0 || column.type.compare(QLatin1String("INTEGER"), Qt::CaseInsensitive) != 0)) {
            return QStringLiteral("Solo una clave primaria única de tipo INTEGER puede ser autoincremental («%1»).").arg(column.name);
        }
    }
    if (def.withoutRowid && keys == 0) {
        return QStringLiteral("Una tabla WITHOUT ROWID necesita clave primaria.");
    }
    return {};
}

QString createSql(const TableDef &def, const QString &nameOverride)
{
    QMap<int, QString> key;
    for (const ColumnDef &column : def.columns) {
        if (column.pk > 0) {
            // Two columns given the same place keep the order they are listed in.
            int place = column.pk * 1000;
            while (key.contains(place)) {
                ++place;
            }
            key.insert(place, column.name);
        }
    }
    QStringList lines;
    for (const ColumnDef &column : def.columns) {
        QString line = Sql::ident(column.name);
        if (!column.type.isEmpty()) {
            line += QLatin1Char(' ') + column.type.toUpper();
        }
        if (column.pk > 0 && key.size() == 1) {
            line += QStringLiteral(" PRIMARY KEY");
            if (column.autoIncrement) {
                line += QStringLiteral(" AUTOINCREMENT");
            }
        }
        if (column.notNull) {
            line += QStringLiteral(" NOT NULL");
        }
        if (column.unique && !(column.pk > 0 && key.size() == 1)) {
            line += QStringLiteral(" UNIQUE");
        }
        const QString fallback = Sql::defaultClause(column.defaultValue);
        if (!fallback.isEmpty()) {
            line += QStringLiteral(" DEFAULT ") + fallback;
        }
        if (!column.fkTable.isEmpty() && !column.fkColumn.isEmpty()) {
            line += QStringLiteral(" REFERENCES %1 (%2)").arg(Sql::ident(column.fkTable), Sql::ident(column.fkColumn));
            if (!action(column.fkOnDelete).isEmpty()) {
                line += QStringLiteral(" ON DELETE ") + action(column.fkOnDelete);
            }
            if (!action(column.fkOnUpdate).isEmpty()) {
                line += QStringLiteral(" ON UPDATE ") + action(column.fkOnUpdate);
            }
        }
        lines.append(line);
    }
    const auto quoteAll = [](const QStringList &names) {
        QStringList quoted;
        for (const QString &name : names) {
            quoted.append(Sql::ident(name));
        }
        return quoted.join(QStringLiteral(", "));
    };
    if (key.size() > 1) {
        lines.append(QStringLiteral("PRIMARY KEY (%1)").arg(quoteAll(key.values())));
    }
    for (const CompositeKey &composite : def.compositeKeys) {
        QString line = QStringLiteral("FOREIGN KEY (%1) REFERENCES %2 (%3)").arg(quoteAll(composite.columns), Sql::ident(composite.table), quoteAll(composite.refColumns));
        if (!action(composite.onDelete).isEmpty()) {
            line += QStringLiteral(" ON DELETE ") + action(composite.onDelete);
        }
        if (!action(composite.onUpdate).isEmpty()) {
            line += QStringLiteral(" ON UPDATE ") + action(composite.onUpdate);
        }
        lines.append(line);
    }
    return QStringLiteral("CREATE TABLE %1 (\n    %2\n)%3")
        .arg(Sql::ident(nameOverride.isEmpty() ? def.name : nameOverride), lines.join(QStringLiteral(",\n    ")), def.withoutRowid ? QStringLiteral(" WITHOUT ROWID") : QString());
}

static bool give(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

bool create(Database &db, const TableDef &def, QString *error)
{
    const QString wrong = validate(def);
    if (!wrong.isEmpty()) {
        return give(error, wrong);
    }
    if (db.exists(def.name)) {
        return give(error, QStringLiteral("Ya hay una tabla o una vista llamada «%1».").arg(def.name));
    }
    const Result result = db.query(createSql(def));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

namespace
{
struct Plan {
    QString error;
    /// ALTER TABLE … RENAME: they keep indexes, triggers and views in step.
    QStringList renames;
    /// The rebuild; empty when renames are all there is to do.
    QStringList rebuild;
    QString finalName;
};

bool sameColumn(const ColumnDef &a, const ColumnDef &b)
{
    return a.name == b.name && a.type.compare(b.type, Qt::CaseInsensitive) == 0 && a.notNull == b.notNull && (a.pk > 0) == (b.pk > 0) && a.unique == b.unique
        && a.autoIncrement == b.autoIncrement && Sql::defaultClause(a.defaultValue) == Sql::defaultClause(b.defaultValue)
        && a.fkTable.compare(b.fkTable, Qt::CaseInsensitive) == 0 && a.fkColumn.compare(b.fkColumn, Qt::CaseInsensitive) == 0
        && action(a.fkOnDelete) == action(b.fkOnDelete) && action(a.fkOnUpdate) == action(b.fkOnUpdate);
}

Plan makePlan(const Database &db, const QString &oldName, const TableDef &def)
{
    Plan plan;
    plan.error = validate(def);
    if (!plan.error.isEmpty()) {
        return plan;
    }
    if (!db.exists(oldName) || db.isView(oldName)) {
        plan.error = QStringLiteral("No existe la tabla «%1».").arg(oldName);
        return plan;
    }
    const bool renamed = def.name != oldName;
    if (renamed && def.name.compare(oldName, Qt::CaseInsensitive) != 0 && db.exists(def.name)) {
        plan.error = QStringLiteral("Ya hay una tabla o una vista llamada «%1».").arg(def.name);
        return plan;
    }
    const TableDef current = read(db, oldName);
    QSet<QString> origins;
    for (const ColumnDef &column : def.columns) {
        if (column.origin.isEmpty()) {
            continue;
        }
        bool found = false;
        for (const ColumnDef &old : current.columns) {
            found = found || old.name == column.origin;
        }
        if (!found) {
            plan.error = QStringLiteral("El campo «%1» ya no existe en la tabla.").arg(column.origin);
            return plan;
        }
        if (origins.contains(column.origin)) {
            plan.error = QStringLiteral("El campo «%1» aparece dos veces.").arg(column.origin);
            return plan;
        }
        origins.insert(column.origin);
    }

    plan.finalName = def.name;
    if (renamed) {
        plan.renames.append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(Sql::ident(oldName), Sql::ident(def.name)));
    }
    // Renames go through names nobody has, so that two columns can swap theirs.
    QList<QPair<QString, QString>> pending;
    for (const ColumnDef &column : def.columns) {
        if (!column.origin.isEmpty() && column.origin != column.name) {
            pending.append({column.origin, column.name});
        }
    }
    QSet<QString> taken;
    for (const ColumnDef &old : current.columns) {
        taken.insert(old.name.toLower());
    }
    QList<QPair<QString, QString>> second;
    int counter = 0;
    for (const auto &[from, to] : std::as_const(pending)) {
        if (taken.contains(to.toLower()) && to.compare(from, Qt::CaseInsensitive) != 0) {
            QString temporary;
            do {
                temporary = QStringLiteral("_bases_tmp_%1").arg(++counter);
            } while (taken.contains(temporary));
            plan.renames.append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(Sql::ident(def.name), Sql::ident(from), Sql::ident(temporary)));
            second.append({temporary, to});
        } else {
            plan.renames.append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(Sql::ident(def.name), Sql::ident(from), Sql::ident(to)));
            taken.insert(to.toLower());
        }
        taken.remove(from.toLower());
    }
    for (const auto &[from, to] : std::as_const(second)) {
        plan.renames.append(QStringLiteral("ALTER TABLE %1 RENAME COLUMN %2 TO %3").arg(Sql::ident(def.name), Sql::ident(from), Sql::ident(to)));
    }

    // After the renames, is anything else different?
    bool same = current.columns.size() == def.columns.size() && current.withoutRowid == def.withoutRowid;
    for (int i = 0; same && i < def.columns.size(); ++i) {
        ColumnDef before = current.columns.at(i);
        const ColumnDef &after = def.columns.at(i);
        if (after.origin != before.name) {
            same = false;
            break;
        }
        before.name = after.name;
        same = sameColumn(before, after);
    }
    if (same) {
        // The order of a composite key is part of it.
        QMap<int, QString> oldKey;
        QMap<int, QString> newKey;
        for (const ColumnDef &column : current.columns) {
            if (column.pk > 0) {
                oldKey.insert(column.pk, column.name);
            }
        }
        for (const ColumnDef &column : def.columns) {
            if (column.pk > 0) {
                newKey.insert(column.pk, column.origin);
            }
        }
        same = oldKey.values() == newKey.values();
    }
    if (same) {
        return plan;
    }

    QString temporary = QStringLiteral("_bases_new_") + def.name;
    while (db.exists(temporary)) {
        temporary += QLatin1Char('_');
    }
    plan.rebuild.append(createSql(def, temporary));
    QStringList into;
    for (const ColumnDef &column : def.columns) {
        if (!column.origin.isEmpty()) {
            // By now the old table already carries the new names.
            into.append(Sql::ident(column.name));
        }
    }
    if (!into.isEmpty()) {
        plan.rebuild.append(QStringLiteral("INSERT INTO %1 (%2) SELECT %2 FROM %3").arg(Sql::ident(temporary), into.join(QStringLiteral(", ")), Sql::ident(def.name)));
    }
    plan.rebuild.append(QStringLiteral("DROP TABLE %1").arg(Sql::ident(def.name)));
    plan.rebuild.append(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(Sql::ident(temporary), Sql::ident(def.name)));
    return plan;
}
}

QStringList plan(const Database &db, const QString &oldName, const TableDef &def)
{
    const Plan made = makePlan(db, oldName, def);
    if (!made.error.isEmpty()) {
        return {};
    }
    return made.renames + made.rebuild;
}

QStringList losses(const Database &db, const QString &table)
{
    QStringList list;
    const QString sql = db.sqlOf(table);
    bool check = false;
    bool collate = false;
    bool generated = false;
    for (const Sql::Token &token : Sql::tokenize(sql)) {
        if (token.type != Sql::Token::Keyword) {
            continue;
        }
        const QString word = sql.mid(token.start, token.length).toUpper();
        check = check || word == QLatin1String("CHECK");
        collate = collate || word == QLatin1String("COLLATE");
        generated = generated || word == QLatin1String("GENERATED");
    }
    if (check) {
        list.append(QStringLiteral("las restricciones CHECK"));
    }
    if (collate) {
        list.append(QStringLiteral("las reglas de ordenación (COLLATE)"));
    }
    if (generated) {
        list.append(QStringLiteral("los campos calculados"));
    }
    for (const QVariant &index : db.indexes(table)) {
        const QVariantMap map = index.toMap();
        if (map.value(QStringLiteral("origin")).toString() == QLatin1String("u") && map.value(QStringLiteral("columns")).toStringList().size() > 1) {
            list.append(QStringLiteral("las restricciones UNIQUE de varios campos"));
            break;
        }
    }
    return list;
}

bool alter(Database &db, const QString &oldName, const TableDef &def, QString *error)
{
    const Plan made = makePlan(db, oldName, def);
    if (!made.error.isEmpty()) {
        return give(error, made.error);
    }
    if (made.renames.isEmpty() && made.rebuild.isEmpty()) {
        return true;
    }
    const bool rebuilding = !made.rebuild.isEmpty();
    // Keys that already dangled before are not this change's fault.
    QSet<QString> danglingBefore;
    const auto dangling = [&db] {
        QSet<QString> set;
        const Result check = db.query(QStringLiteral("PRAGMA foreign_key_check"));
        for (const QVariantList &row : check.rows) {
            set.insert(row.value(0).toString() + QLatin1Char('/') + row.value(1).toString() + QLatin1Char('/') + row.value(2).toString() + QLatin1Char('/') + row.value(3).toString());
        }
        return set;
    };
    if (rebuilding) {
        danglingBefore = dangling();
        // Dropping the old table must not cascade into the rows that refer to
        // it; the pragma only takes effect outside a transaction.
        db.query(QStringLiteral("PRAGMA foreign_keys = OFF"));
    }
    const auto restore = [&] {
        if (rebuilding) {
            db.query(QStringLiteral("PRAGMA foreign_keys = ON"));
        }
    };
    if (!db.begin()) {
        restore();
        return give(error, db.lastError());
    }
    const auto abort = [&](const QString &message) {
        db.rollback();
        restore();
        return give(error, message);
    };
    for (const QString &statement : made.renames) {
        const Result result = db.query(statement);
        if (!result.ok) {
            return abort(result.error);
        }
    }
    if (rebuilding) {
        // What hangs from the table goes with it when it is dropped.
        QStringList keep;
        QSet<QString> columns;
        for (const ColumnDef &column : def.columns) {
            columns.insert(column.name.toLower());
        }
        for (const QVariant &index : db.indexes(def.name)) {
            const QVariantMap map = index.toMap();
            const QString sql = map.value(QStringLiteral("sql")).toString();
            if (sql.isEmpty()) {
                continue;
            }
            bool whole = true;
            for (const QString &column : map.value(QStringLiteral("columns")).toStringList()) {
                // An index on an expression lists an empty name: keep it and let it fail loudly if it must.
                whole = whole && (column.isEmpty() || columns.contains(column.toLower()));
            }
            if (whole) {
                keep.append(sql);
            }
        }
        const Result triggers = db.query(QStringLiteral("SELECT sql FROM sqlite_schema WHERE type = 'trigger' AND tbl_name = ? COLLATE NOCASE AND sql IS NOT NULL"), {def.name});
        for (const QVariantList &row : triggers.rows) {
            keep.append(row.first().toString());
        }
        for (const QString &statement : made.rebuild + keep) {
            const Result result = db.query(statement);
            if (!result.ok) {
                return abort(result.error);
            }
        }
        const QSet<QString> now = dangling();
        if (!(now - danglingBefore).isEmpty()) {
            return abort(QStringLiteral("El cambio dejaría registros que apuntan a filas que no existen (claves foráneas)."));
        }
    }
    if (!db.commit()) {
        const QString message = db.lastError();
        return abort(message);
    }
    restore();
    db.touch(true);
    return true;
}

bool dropTable(Database &db, const QString &table, QString *error)
{
    const Result result = db.query((db.isView(table) ? QStringLiteral("DROP VIEW ") : QStringLiteral("DROP TABLE ")) + Sql::ident(table));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

bool renameTable(Database &db, const QString &table, const QString &name, QString *error)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        return give(error, QStringLiteral("La tabla necesita un nombre."));
    }
    if (clean.startsWith(QLatin1String("sqlite_"), Qt::CaseInsensitive) || clean.startsWith(QLatin1String("_wonderland_"), Qt::CaseInsensitive)) {
        return give(error, QStringLiteral("«%1» es un nombre reservado.").arg(clean));
    }
    const Result result = db.query(QStringLiteral("ALTER TABLE %1 RENAME TO %2").arg(Sql::ident(table), Sql::ident(clean)));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

bool createIndex(Database &db, const QString &table, const QString &name, const QStringList &columns, bool unique, QString *error)
{
    if (columns.isEmpty()) {
        return give(error, QStringLiteral("El índice necesita al menos un campo."));
    }
    QString indexName = name.trimmed();
    if (indexName.isEmpty()) {
        indexName = QStringLiteral("idx_%1_%2").arg(table, columns.join(QLatin1Char('_')));
    }
    QStringList quoted;
    for (const QString &column : columns) {
        quoted.append(Sql::ident(column));
    }
    const Result result = db.query(QStringLiteral("CREATE %1INDEX %2 ON %3 (%4)").arg(unique ? QStringLiteral("UNIQUE ") : QString(), Sql::ident(indexName), Sql::ident(table), quoted.join(QStringLiteral(", "))));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

bool dropIndex(Database &db, const QString &name, QString *error)
{
    const Result result = db.query(QStringLiteral("DROP INDEX ") + Sql::ident(name));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

bool createView(Database &db, const QString &name, const QString &select, QString *error)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        return give(error, QStringLiteral("La vista necesita un nombre."));
    }
    if (!Sql::selects(select) || Sql::split(select).size() != 1) {
        return give(error, QStringLiteral("Una vista se hace con una sola consulta SELECT."));
    }
    const Result result = db.query(QStringLiteral("CREATE VIEW %1 AS %2").arg(Sql::ident(clean), Sql::trimmed(select)));
    if (!result.ok) {
        return give(error, result.error);
    }
    db.touch(true);
    return true;
}

}
