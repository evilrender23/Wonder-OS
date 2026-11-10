#pragma once

#include <QDateTime>
#include <QString>

namespace Format
{
/// Finder-style size: decimal units, "Zero bytes", "12 KB", "4.5 MB".
QString size(quint64 bytes);
/// "Today, 14:32", "Yesterday, 9:05", "12 Mar 2025, 18:21".
QString date(const QDateTime &when, const QDateTime &now = QDateTime::currentDateTime());
/// "Monday, 9 November 2026, 17:29", as the Info window shows it.
QString longDate(const QDateTime &when);
QString capitalized(const QString &text);
/// "name 2.ext", "name copy.ext", "name copy 2.ext": the first name not in `taken`.
QString uniqueName(const QString &fileName, bool isDir, const QString &word, const QStringList &taken);
}
