#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;

/// How the desktop looks beyond the compositor: what its own apps share. Kept in
/// `~/.config/wonderland/appearance.json`, which the file manager and the desktop icons watch.
class Appearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString folderTint READ folderTint NOTIFY folderTintChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit Appearance(const QString &path, QObject *parent = nullptr);

    /// Where the file is unless told otherwise.
    static QString defaultPath();
    QString path() const { return m_path; }

    /// The colour every folder takes, as "#rrggbb"; empty for the usual blue.
    QString folderTint() const { return m_folderTint; }
    /// Saves it; an empty colour puts the usual blue back. False if it isn't a colour or can't be written.
    Q_INVOKABLE bool setFolderTint(const QString &color);
    /// "#rrggbb" in lower case from "#RRGGBB" or "rrggbb"; empty for anything else.
    Q_INVOKABLE static QString cleanColor(const QString &text);

    QString error() const { return m_error; }

Q_SIGNALS:
    void folderTintChanged();
    void errorChanged();

private:
    void reload();
    void setError(const QString &error);

    QString m_path;
    QString m_folderTint;
    QString m_error;
    QFileSystemWatcher *m_watcher;
};
