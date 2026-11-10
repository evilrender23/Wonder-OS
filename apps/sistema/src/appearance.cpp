#include "appearance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

static const QLatin1String folderTintKey("folder_tint");

Appearance::Appearance(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_watcher(new QFileSystemWatcher(this))
{
    // The folder, not the file: every writer replaces the file, and a watch on it would be lost.
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    m_watcher->addPath(QFileInfo(m_path).absolutePath());
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Appearance::reload);
    reload();
}

QString Appearance::defaultPath()
{
    QString home = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (home.isEmpty()) {
        home = QDir::homePath() + QStringLiteral("/.config");
    }
    return home + QStringLiteral("/wonderland/appearance.json");
}

QString Appearance::cleanColor(const QString &text)
{
    static const QRegularExpression hex(QStringLiteral("^#?([0-9a-fA-F]{6})$"));
    const QRegularExpressionMatch match = hex.match(text.trimmed());
    return match.hasMatch() ? QLatin1Char('#') + match.captured(1).toLower() : QString();
}

static QJsonObject read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

void Appearance::reload()
{
    const QString tint = cleanColor(read(m_path).value(folderTintKey).toString());
    if (tint != m_folderTint) {
        m_folderTint = tint;
        Q_EMIT folderTintChanged();
    }
}

void Appearance::setError(const QString &error)
{
    if (error != m_error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
}

bool Appearance::setFolderTint(const QString &color)
{
    const QString tint = cleanColor(color);
    if (tint.isEmpty() && !color.trimmed().isEmpty()) {
        setError(tr("«%1» no es un color: se escribe como #rrggbb.").arg(color.trimmed()));
        return false;
    }
    // Whatever else the file holds stays as it is.
    QJsonObject object = read(m_path);
    if (tint.isEmpty()) {
        object.remove(folderTintKey);
    } else {
        object.insert(folderTintKey, tint);
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        setError(tr("No se puede guardar %1: %2").arg(m_path, file.errorString()));
        return false;
    }
    setError(QString());
    reload();
    return true;
}
