#include "versions.h"

#include "database.h"
#include "sqlutil.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace
{
const QString stampFormat = QStringLiteral("yyyyMMdd-HHmmss-zzz");

bool give(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
    return false;
}

void prune(const QString &dir)
{
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (qsizetype i = Versions::limit; i < files.size(); ++i) {
        QFile::remove(dir + QLatin1Char('/') + files.at(i));
    }
}
}

QString Versions::directory(const QString &root, const QString &path)
{
    // Two documents of the same name in different folders do not share copies.
    const QByteArray hash = QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return root + QStringLiteral("/versions/") + QString::fromLatin1(hash);
}

QString Versions::save(Database &db, const QString &dir, QString *error)
{
    if (!db.isOpen()) {
        give(error, QStringLiteral("No hay ninguna base de datos abierta."));
        return {};
    }
    if (!QDir().mkpath(dir)) {
        give(error, QStringLiteral("No se puede crear la carpeta de versiones."));
        return {};
    }
    QString target = dir + QLatin1Char('/') + QDateTime::currentDateTime().toString(stampFormat) + QStringLiteral(".db");
    for (int n = 2; QFileInfo::exists(target); ++n) {
        target = dir + QLatin1Char('/') + QDateTime::currentDateTime().toString(stampFormat) + QStringLiteral("-%1.db").arg(n);
    }
    // SQLite writes the copy itself: a whole, consistent file even with
    // changes of other connections on the way.
    if (!db.exec(QStringLiteral("VACUUM INTO ?"), {target})) {
        QFile::remove(target);
        give(error, db.lastError().isEmpty() ? QStringLiteral("No se pudo guardar la versión.") : db.lastError());
        return {};
    }
    prune(dir);
    return target;
}

QVariantList Versions::list(const QString &dir)
{
    QVariantList list;
    const QLocale locale(QLocale::Spanish, QLocale::Spain);
    const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : files) {
        QDateTime when = QDateTime::fromString(info.completeBaseName().left(stampFormat.size()), stampFormat);
        if (!when.isValid()) {
            when = info.lastModified();
        }
        const qint64 days = when.date().daysTo(QDate::currentDate());
        const QString day = days == 0 ? QStringLiteral("Hoy") : days == 1 ? QStringLiteral("Ayer") : locale.toString(when.date(), QStringLiteral("d 'de' MMMM 'de' yyyy"));
        list.append(QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()},
                                {QStringLiteral("name"), info.fileName()},
                                {QStringLiteral("when"), when.toString(Qt::ISODate)},
                                {QStringLiteral("label"), day + QStringLiteral(", ") + when.toString(QStringLiteral("HH:mm:ss"))},
                                {QStringLiteral("size"), info.size()}});
    }
    return list;
}

QVariantMap Versions::describe(const QString &version)
{
    QVariantMap out{{QStringLiteral("ok"), false}};
    const QString connection = QStringLiteral("version-") + QUuid::createUuid().toString(QUuid::Id128);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(version);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!QFileInfo(version).isFile() || !db.open()) {
            out.insert(QStringLiteral("error"), QStringLiteral("No se puede leer esta versión."));
        } else {
            QVariantList tables;
            qint64 rows = 0;
            int objects = 0;
            QStringList names;
            QSqlQuery schema(db);
            bool ok = schema.exec(QStringLiteral("SELECT name FROM sqlite_schema WHERE type = 'table' AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\' ORDER BY name COLLATE NOCASE"));
            while (ok && schema.next()) {
                names << schema.value(0).toString();
            }
            schema.finish();
            for (const QString &name : std::as_const(names)) {
                QSqlQuery count(db);
                const qint64 n = count.exec(QStringLiteral("SELECT count(*) FROM ") + Sql::ident(name)) && count.next() ? count.value(0).toLongLong() : 0;
                if (name == QLatin1String("_wonderland_objects")) {
                    objects = int(n);
                }
                if (name.startsWith(QLatin1String("_wonderland_"))) {
                    continue;
                }
                rows += n;
                tables.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("rows"), n}});
            }
            out.insert(QStringLiteral("ok"), ok);
            if (!ok) {
                out.insert(QStringLiteral("error"), QStringLiteral("No se puede leer esta versión."));
            }
            out.insert(QStringLiteral("tables"), tables);
            out.insert(QStringLiteral("rows"), rows);
            out.insert(QStringLiteral("objects"), objects);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return out;
}

bool Versions::restore(Database &db, const QString &dir, const QString &version, QString *error)
{
    if (!db.isOpen() || db.readOnly()) {
        return give(error, QStringLiteral("La base de datos está abierta solo para leer."));
    }
    if (!describe(version).value(QStringLiteral("ok")).toBool()) {
        return give(error, QStringLiteral("No se puede leer esta versión."));
    }
    QString why;
    if (save(db, dir, &why).isEmpty()) {
        return give(error, why);
    }
    const QString path = db.path();
    const QString aside = path + QStringLiteral(".restaurando");
    db.close();
    QFile::remove(aside);
    if (!QFile::rename(path, aside)) {
        db.open(path);
        return give(error, QStringLiteral("No se puede reemplazar el archivo."));
    }
    if (!QFile::copy(version, path)) {
        QFile::rename(aside, path);
        db.open(path);
        return give(error, QStringLiteral("No se pudo copiar la versión."));
    }
    QFile::setPermissions(path, QFile::permissions(aside));
    // What the journal of the file replaced said no longer applies.
    QFile::remove(path + QStringLiteral("-wal"));
    QFile::remove(path + QStringLiteral("-shm"));
    QFile::remove(path + QStringLiteral("-journal"));
    if (!db.open(path)) {
        QFile::remove(path);
        QFile::rename(aside, path);
        db.open(path);
        return give(error, QStringLiteral("La versión no se pudo abrir; se ha dejado todo como estaba."));
    }
    QFile::remove(aside);
    return true;
}
