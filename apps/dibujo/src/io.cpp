#include "io.h"

#include "render.h"
#include "store.h"
#include "zip.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

static const QString versionStamp = QStringLiteral("yyyyMMdd-HHmmss-zzz");

QByteArray Io::pack(const Document &source)
{
    Document doc = source;
    doc.pruneResources();
    QList<Zip::Entry> entries;
    // First and uncompressed, so the type can be told from the first bytes.
    entries << Zip::Entry{QStringLiteral("mimetype"), QByteArray(MimeType), true};
    entries << Zip::Entry{QStringLiteral("document.json"), QJsonDocument(doc.toJson()).toJson(QJsonDocument::Indented), false};
    for (auto it = doc.resources.constBegin(); it != doc.resources.constEnd(); ++it) {
        // Images are compressed already.
        entries << Zip::Entry{QStringLiteral("resources/") + it.key(), it.value(), true};
    }
    const QImage preview = Render::thumbnail(doc, QSize(480, 320), QColor(QStringLiteral("#f4f5f7")));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    preview.save(&buffer, "PNG");
    entries << Zip::Entry{QStringLiteral("preview.png"), png, true};
    return Zip::write(entries);
}

bool Io::unpack(const QByteArray &bytes, Document *doc, QString *error)
{
    QList<Zip::Entry> entries;
    if (!Zip::read(bytes, &entries, error)) {
        return false;
    }
    QByteArray json;
    bool typed = false;
    QMap<QString, QByteArray> resources;
    for (const Zip::Entry &entry : entries) {
        if (entry.name == QLatin1String("mimetype")) {
            typed = entry.data.trimmed() == MimeType;
        } else if (entry.name == QLatin1String("document.json")) {
            json = entry.data;
        } else if (entry.name.startsWith(QLatin1String("resources/"))) {
            const QString name = entry.name.mid(10);
            // A name is a name: never a path out of the container.
            if (!name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))) {
                resources.insert(name, entry.data);
            }
        }
    }
    QJsonParseError parse;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parse);
    if (!typed || json.isEmpty() || !parsed.isObject()) {
        if (error) {
            *error = QStringLiteral("no es un dibujo de Wonderland");
        }
        return false;
    }
    *doc = Document::fromJson(parsed.object());
    doc->resources = resources;
    return true;
}

bool Io::writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool Io::save(const Document &doc, const QString &path, QString *error)
{
    return writeFile(path, pack(doc), error);
}

bool Io::load(const QString &path, Document *doc, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return unpack(file.readAll(), doc, error);
}

QImage Io::preview(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QList<Zip::Entry> entries;
    if (!Zip::read(file.readAll(), &entries)) {
        return {};
    }
    for (const Zip::Entry &entry : entries) {
        if (entry.name == QLatin1String("preview.png")) {
            return QImage::fromData(entry.data, "PNG");
        }
    }
    return {};
}

// --- Versions -------------------------------------------------------------------

static QString versionDir(const QString &dataDir, const QString &uid)
{
    // The uid comes out of a file: keep only what a uuid is made of.
    QString safe;
    for (const QChar c : uid) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-')) {
            safe += c;
        }
    }
    return dataDir + QStringLiteral("/versions/") + (safe.isEmpty() ? QStringLiteral("none") : safe);
}

QString Io::addVersion(const QString &dataDir, const Document &doc, const QDateTime &when)
{
    const QString dir = versionDir(dataDir, doc.uid);
    QString path = dir + QLatin1Char('/') + when.toString(versionStamp) + QStringLiteral(".wdib");
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
    const QDir dir(versionDir(dataDir, uid));
    const QStringList names = dir.entryList({QStringLiteral("*.wdib")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &name : names) {
        const QDateTime when = QDateTime::fromString(name.chopped(5), versionStamp);
        if (when.isValid()) {
            list << Version{dir.filePath(name), when};
        }
    }
    return list;
}

// --- Recovery -------------------------------------------------------------------

static QString recoveryBase(const QString &dataDir, const QString &uid)
{
    return versionDir(dataDir, uid).replace(QStringLiteral("/versions/"), QStringLiteral("/recovery/"));
}

bool Io::writeRecovery(const QString &dataDir, const Document &doc, const QString &origin)
{
    const QString base = recoveryBase(dataDir, doc.uid);
    if (!save(doc, base + QStringLiteral(".wdib"))) {
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
    const QString base = recoveryBase(dataDir, uid);
    QFile::remove(base + QStringLiteral(".wdib"));
    QFile::remove(base + QStringLiteral(".json"));
}

QList<Io::Recovery> Io::recoveries(const QString &dataDir)
{
    QList<Recovery> list;
    const QDir dir(dataDir + QStringLiteral("/recovery"));
    for (const QString &name : dir.entryList({QStringLiteral("*.wdib")}, QDir::Files, QDir::Time)) {
        const QJsonObject about = Store::read(dir.filePath(name.chopped(5) + QStringLiteral(".json")));
        list << Recovery{dir.filePath(name), about.value(QLatin1String("title")).toString(), about.value(QLatin1String("origin")).toString(),
                         QDateTime::fromString(about.value(QLatin1String("when")).toString(), Qt::ISODate)};
    }
    return list;
}
