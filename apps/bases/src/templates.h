#pragma once

#include <QString>
#include <QVariant>

class Database;

/// Databases to start from.
namespace Templates
{
/// [{id, name, description, symbol}]
QVariantList list();
/// Makes the tables of a template in an open database, with a form and a
/// query to begin with.
bool apply(Database &db, const QString &id, QString *error = nullptr);
/// A shop to try things on: customers, products, orders and their lines,
/// tied by foreign keys, with a few hundred rows. The same every time.
bool sample(Database &db, QString *error = nullptr);
}
