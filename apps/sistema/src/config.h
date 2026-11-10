#pragma once

#include "tomldoc.h"

#include <QObject>
#include <QStringList>
#include <QVariant>

class QFileSystemWatcher;

/// The compositor's config.toml, as the settings pages see it.
///
/// Every change is checked by the compositor itself (`driftwm --check-config`) before it reaches
/// the file, so a control can never leave a config the compositor refuses.
class Config : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString path READ path CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool checked READ checked CONSTANT)

public:
    Config(const QString &path, const QString &validator, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QString error() const { return m_error; }
    QString path() const { return m_path; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    /// Whether the compositor binary is there to check changes.
    bool checked() const { return !m_validator.isEmpty(); }
    QString text() const { return m_doc.text(); }

    Q_INVOKABLE bool has(const QStringList &path) const;
    Q_INVOKABLE QVariant get(const QStringList &path) const;
    /// The keys set in a table, as {key, value} in file order.
    Q_INVOKABLE QVariantList entries(const QStringList &table) const;

    /// `kind` is "bool", "int", "float", "string", or empty to go by the value's own type.
    Q_INVOKABLE bool set(const QStringList &path, const QVariant &value, const QString &kind = QString());
    Q_INVOKABLE bool unset(const QStringList &path);
    /// Several changes as one: each is {path, value, kind}, or {path, remove: true}.
    Q_INVOKABLE bool apply(const QVariantList &changes);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();

Q_SIGNALS:
    void changed();
    void errorChanged();

private:
    struct Check {
        bool ok = true;
        QString error;
        QStringList warnings;
    };

    Check check(const QString &text) const;
    bool commit(const QString &text, bool undoable);
    bool write(const QString &text);
    void setError(const QString &error);
    void fileChanged();

    QString m_path;
    QString m_validator;
    TomlDoc m_doc;
    QStringList m_warnings;
    QStringList m_undo;
    QString m_error;
    bool m_broken = false;
    bool m_backedUp = false;
    int m_revision = 0;
    QFileSystemWatcher *m_watcher;
};
