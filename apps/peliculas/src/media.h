#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

/// What a file is, by its name, and the small conversions the player needs.
namespace Media
{
bool isVideo(const QString &path);
bool isAudio(const QString &path);
/// A video or a sound: something the player takes.
bool isPlayable(const QString &path);
bool isSubtitle(const QString &path);

/// The name shown for a file: its own, without the ending.
QString title(const QString &path);
/// «Español», «Inglés»: a language as this desktop names it; empty when there is none.
QString languageName(QLocale::Language language);
/// 3:07, 1:02:45.
QString time(qint64 ms);
/// «90», «1:30», «1:02:03» as milliseconds; -1 when it is not a time.
qint64 parseTime(const QString &text);

/// Files stay as they are (whatever their ending: they were asked for by
/// name); folders become what can be played inside them, in the order a
/// person counts («2» before «10»), folders within included.
QStringList expand(const QStringList &paths);
/// What can be played in the folder of this file, itself included, in order.
QStringList siblings(const QString &path);
/// Orders names the way a person counts.
void sortNatural(QStringList &paths);
}
