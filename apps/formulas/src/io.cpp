#include "io.h"

#include "store.h"

#include <QDir>
#include <QFile>

namespace {

const QString versionStamp = QStringLiteral("yyyyMMdd-HHmmss-zzz");

QString versionDir(const QString &dataDir, const QString &uid)
{
    return dataDir + QStringLiteral("/versions/") + uid;
}

QString recoveryBase(const QString &dataDir, const QString &uid)
{
    return dataDir + QStringLiteral("/recovery/") + uid;
}

bool save(const FormulaDoc &doc, const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    return Formats::write(path, doc, nullptr);
}

}

QString Io::addVersion(const QString &dataDir, const FormulaDoc &doc, const QDateTime &when)
{
    if (doc.uid.isEmpty()) {
        return {};
    }
    const QString path = versionDir(dataDir, doc.uid) + QLatin1Char('/') + when.toString(versionStamp) + QStringLiteral(".wform");
    if (!save(doc, path)) {
        return {};
    }
    const QList<Version> all = versions(dataDir, doc.uid);
    for (int i = MaxVersions; i < all.size(); ++i) {
        QFile::remove(all.at(i).path);
    }
    return path;
}

QList<Io::Version> Io::versions(const QString &dataDir, const QString &uid)
{
    QList<Version> list;
    if (uid.isEmpty()) {
        return list;
    }
    const QDir dir(versionDir(dataDir, uid));
    const QStringList names = dir.entryList({QStringLiteral("*.wform")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &name : names) {
        const QDateTime when = QDateTime::fromString(name.chopped(6), versionStamp);
        if (when.isValid()) {
            list << Version{dir.filePath(name), when};
        }
    }
    return list;
}

bool Io::writeRecovery(const QString &dataDir, const FormulaDoc &doc, const QString &origin)
{
    if (doc.uid.isEmpty()) {
        return false;
    }
    const QString base = recoveryBase(dataDir, doc.uid);
    if (!save(doc, base + QStringLiteral(".wform"))) {
        return false;
    }
    return Store::write(base + QStringLiteral(".json"), QJsonObject{
        {QStringLiteral("title"), doc.title},
        {QStringLiteral("origin"), origin},
        {QStringLiteral("when"), QDateTime::currentDateTime().toString(Qt::ISODate)},
    });
}

void Io::dropRecovery(const QString &dataDir, const QString &uid)
{
    if (uid.isEmpty()) {
        return;
    }
    const QString base = recoveryBase(dataDir, uid);
    QFile::remove(base + QStringLiteral(".wform"));
    QFile::remove(base + QStringLiteral(".json"));
}

QList<Io::Recovery> Io::recoveries(const QString &dataDir)
{
    QList<Recovery> list;
    const QDir dir(dataDir + QStringLiteral("/recovery"));
    for (const QString &name : dir.entryList({QStringLiteral("*.wform")}, QDir::Files, QDir::Time)) {
        const QJsonObject about = Store::read(dir.filePath(name.chopped(6) + QStringLiteral(".json")));
        list << Recovery{dir.filePath(name), about.value(QLatin1String("title")).toString(), about.value(QLatin1String("origin")).toString(),
                         QDateTime::fromString(about.value(QLatin1String("when")).toString(), Qt::ISODate)};
    }
    return list;
}
