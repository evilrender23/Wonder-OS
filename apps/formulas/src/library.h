#pragma once

#include <QList>
#include <QString>
#include <QVariant>

/// The formulas kept to use again: the collection that comes with the app,
/// and the ones the user saved with a name.
namespace Library
{
struct Entry {
    QString id;
    QString name;
    QString latex;
};
struct Shelf {
    QString id;
    QString title;
    QList<Entry> entries;
};

/// Álgebra, Cálculo, Física, Estadística.
const QList<Shelf> &collection();
/// The formula of an entry of the collection, or an empty string.
QString builtin(const QString &id);

/// The user's own, in the order they were saved.
QList<Entry> saved(const QString &dataDir);
/// Adds one; an empty name gets «Fórmula N». Returns its id.
QString save(const QString &dataDir, const QString &name, const QString &latex);
bool rename(const QString &dataDir, const QString &id, const QString &name);
bool remove(const QString &dataDir, const QString &id);

/// [{id, title, entries: [{id, name, latex, own}]}] with the user's shelf first, filtered by `query`.
QVariantList shelves(const QString &dataDir, const QString &query = QString());
}
