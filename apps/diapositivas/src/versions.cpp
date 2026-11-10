#include "versions.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

static const QString stampFormat = QStringLiteral("yyyyMMdd-HHmmss-zzz");

Versions::Versions(const QString &dir, int keep)
    : m_dir(dir)
    , m_keep(qMax(1, keep))
{
}

QString Versions::folder(const QString &document) const
{
    const QByteArray hash = QCryptographicHash::hash(QFileInfo(document).absoluteFilePath().toUtf8(), QCryptographicHash::Sha1).toHex();
    return m_dir + QLatin1Char('/') + QString::fromLatin1(hash.left(16));
}

QList<Versions::Entry> Versions::list(const QString &document) const
{
    QList<Entry> out;
    if (document.isEmpty()) {
        return out;
    }
    const QDir dir(folder(document));
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files, QDir::Name | QDir::Reversed)) {
        const QDateTime when = QDateTime::fromString(info.completeBaseName(), stampFormat);
        if (when.isValid()) {
            out << Entry{info.absoluteFilePath(), when, info.size()};
        }
    }
    return out;
}

QString Versions::add(const QString &document, const QByteArray &bytes, const QDateTime &when)
{
    if (document.isEmpty() || bytes.isEmpty()) {
        return {};
    }
    const QList<Entry> have = list(document);
    if (!have.isEmpty()) {
        QFile newest(have.first().file);
        if (newest.open(QIODevice::ReadOnly) && newest.readAll() == bytes) {
            return {};
        }
    }
    const QString dir = folder(document);
    if (!QDir().mkpath(dir)) {
        return {};
    }
    QDateTime stamp = when;
    // Two saves in the same millisecond are still two versions.
    while (QFileInfo::exists(dir + QLatin1Char('/') + stamp.toString(stampFormat) + QLatin1Char('.') + QFileInfo(document).suffix())) {
        stamp = stamp.addMSecs(1);
    }
    const QString path = dir + QLatin1Char('/') + stamp.toString(stampFormat) + QLatin1Char('.') + QFileInfo(document).suffix();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return {};
    }
    const QList<Entry> all = list(document);
    for (int i = m_keep; i < all.size(); ++i) {
        QFile::remove(all.at(i).file);
    }
    return path;
}

bool Versions::owns(const QString &file) const
{
    const QString root = QFileInfo(m_dir).absoluteFilePath() + QLatin1Char('/');
    return QFileInfo(file).absoluteFilePath().startsWith(root) && QFileInfo(file).isFile();
}
