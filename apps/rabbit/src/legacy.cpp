#include "legacy.h"

#include <QFile>
#include <QFileInfo>

static void move(const QString &from, const QString &to)
{
    if (QFileInfo::exists(from) && !QFileInfo::exists(to)) {
        QFile::rename(from, to);
    }
}

void Legacy::adopt(const QString &configRoot, const QString &dataRoot)
{
    move(configRoot + QStringLiteral("/wonderland/wonderland-finder.conf"), configRoot + QStringLiteral("/wonderland/wonderland-rabbit.conf"));
    move(dataRoot + QStringLiteral("/wonderland/wonderland-finder"), dataRoot + QStringLiteral("/wonderland/wonderland-rabbit"));
}
