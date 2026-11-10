#pragma once

#include "model.h"

#include <QDateTime>
#include <QImage>

/// Drawings on disk: the .wdib container (a ZIP with `document.json`, the
/// images under `resources/` and a `preview.png`), the versions each manual
/// save leaves behind, and the recovery copy of unsaved work.
namespace Io
{
inline constexpr char MimeType[] = "application/x-wonderland-dibujo";
inline constexpr int MaxVersions = 20;

QByteArray pack(const Document &doc);
bool unpack(const QByteArray &bytes, Document *doc, QString *error = nullptr);
bool save(const Document &doc, const QString &path, QString *error = nullptr);
bool load(const QString &path, Document *doc, QString *error = nullptr);
/// The picture a .wdib carries of itself, without reading the rest.
QImage preview(const QString &path);
bool writeFile(const QString &path, const QByteArray &bytes, QString *error = nullptr);

struct Version {
    QString path;
    QDateTime when;
};
/// Files a copy under the drawing's uid and drops the oldest past MaxVersions.
QString addVersion(const QString &dataDir, const Document &doc, const QDateTime &when = QDateTime::currentDateTime());
/// Newest first.
QList<Version> versions(const QString &dataDir, const QString &uid);

struct Recovery {
    QString path;
    QString title;
    /// The file the drawing came from; empty if it was never saved.
    QString origin;
    QDateTime when;
};
bool writeRecovery(const QString &dataDir, const Document &doc, const QString &origin);
void dropRecovery(const QString &dataDir, const QString &uid);
QList<Recovery> recoveries(const QString &dataDir);
}
