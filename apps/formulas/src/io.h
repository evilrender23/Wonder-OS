#pragma once

#include "formats.h"

#include <QDateTime>
#include <QList>

/// What Fórmulas keeps beside the files: the versions each save by hand
/// leaves behind, and the recovery copy of unsaved work.
namespace Io
{
inline constexpr int MaxVersions = 20;

struct Version {
    QString path;
    QDateTime when;
};
/// Files a copy under the document's uid and drops the oldest past MaxVersions.
QString addVersion(const QString &dataDir, const FormulaDoc &doc, const QDateTime &when = QDateTime::currentDateTime());
/// Newest first.
QList<Version> versions(const QString &dataDir, const QString &uid);

struct Recovery {
    QString path;
    QString title;
    /// The file the document came from; empty if it was never saved.
    QString origin;
    QDateTime when;
};
bool writeRecovery(const QString &dataDir, const FormulaDoc &doc, const QString &origin);
void dropRecovery(const QString &dataDir, const QString &uid);
QList<Recovery> recoveries(const QString &dataDir);
}
