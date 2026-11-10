#pragma once

#include <QString>
#include <QVariant>

class Database;

/// The copies a manual save leaves behind: whole databases, one file each, in a
/// directory of their own per document, so that a save can be gone back to.
namespace Versions
{
/// The most copies kept of one document; the oldest go first.
constexpr int limit = 20;

/// Where the copies of the database at `path` live under `root`.
QString directory(const QString &root, const QString &path);
/// Copies the open database, as it is now, into `dir`. Returns the path of the
/// copy, or «» with `error` set. No more than `limit` copies are left.
QString save(Database &db, const QString &dir, QString *error = nullptr);
/// The latest first: [{path, name, when (ISO), label, size}]
QVariantList list(const QString &dir);
/// What a copy holds, to look at before going back to it:
/// {ok, error, tables: [{name, rows}], rows, objects}
QVariantMap describe(const QString &version);
/// Puts a copy in the place of the open database, which is opened again.
/// What was there is saved as a version first, so going back can be undone.
bool restore(Database &db, const QString &dir, const QString &version, QString *error = nullptr);
}
