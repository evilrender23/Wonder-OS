#include "format.h"

#include <QCoreApplication>
#include <QLocale>
#include <QMimeDatabase>

namespace Format
{
static QString tr(const char *text)
{
    return QCoreApplication::translate("Format", text);
}

static QString number(double value, int decimals)
{
    const QLocale locale;
    QString text = locale.toString(value, 'f', decimals);
    if (decimals > 0) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(locale.decimalPoint())) {
            text.chop(locale.decimalPoint().size());
        }
    }
    return text;
}

QString size(quint64 bytes)
{
    if (bytes == 0) {
        return tr("Zero bytes");
    }
    if (bytes == 1) {
        return tr("1 byte");
    }
    if (bytes < 1000) {
        return tr("%1 bytes").arg(bytes);
    }
    const double kb = bytes / 1000.0;
    if (kb < 999.5) {
        return tr("%1 KB").arg(number(kb, 0));
    }
    const double mb = kb / 1000.0;
    if (mb < 999.95) {
        return tr("%1 MB").arg(number(mb, 1));
    }
    const double gb = mb / 1000.0;
    if (gb < 999.995) {
        return tr("%1 GB").arg(number(gb, 2));
    }
    return tr("%1 TB").arg(number(gb / 1000.0, 2));
}

QString date(const QDateTime &when, const QDateTime &now)
{
    if (!when.isValid()) {
        return QStringLiteral("--");
    }
    const QLocale locale;
    const QDateTime local = when.toLocalTime();
    const QString time = locale.toString(local.time(), QLocale::ShortFormat);
    const qint64 days = local.date().daysTo(now.toLocalTime().date());
    if (days == 0) {
        return tr("Today, %1").arg(time);
    }
    if (days == 1) {
        return tr("Yesterday, %1").arg(time);
    }
    return locale.toString(local.date(), QStringLiteral("d MMM yyyy")) + QStringLiteral(", ") + time;
}

QString longDate(const QDateTime &when)
{
    if (!when.isValid()) {
        return QStringLiteral("--");
    }
    const QLocale locale;
    const QDateTime local = when.toLocalTime();
    return locale.toString(local.date(), QLocale::LongFormat) + QStringLiteral(", ") + locale.toString(local.time(), QLocale::ShortFormat);
}

QString capitalized(const QString &text)
{
    if (text.isEmpty()) {
        return text;
    }
    return text.at(0).toUpper() + text.mid(1);
}

QString uniqueName(const QString &fileName, bool isDir, const QString &word, const QStringList &taken)
{
    QString base = fileName;
    QString extension;
    if (!isDir) {
        const QString suffix = QMimeDatabase().suffixForFileName(fileName);
        const int dot = suffix.isEmpty() ? fileName.lastIndexOf(QLatin1Char('.')) : fileName.size() - suffix.size() - 1;
        if (dot > 0) {
            base = fileName.left(dot);
            extension = fileName.mid(dot);
        }
    }
    const auto free = [&taken](const QString &name) {
        return !taken.contains(name, Qt::CaseInsensitive);
    };
    if (word.isEmpty() && free(fileName)) {
        return fileName;
    }
    const QString stem = word.isEmpty() ? base : base + QLatin1Char(' ') + word;
    if (!word.isEmpty() && free(stem + extension)) {
        return stem + extension;
    }
    for (int n = 2;; ++n) {
        const QString candidate = stem + QLatin1Char(' ') + QString::number(n) + extension;
        if (free(candidate)) {
            return candidate;
        }
    }
}
}
