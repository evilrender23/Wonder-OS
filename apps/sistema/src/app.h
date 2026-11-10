#pragma once

#include "tomldoc.h"

#include <QObject>
#include <QString>
#include <QVariant>

class QFileSystemWatcher;
struct xkb_context;
struct xkb_keymap;

/// What the pages need besides the config itself: the desktop's light/dark switch, the
/// compositor's built-in defaults, and key names as the compositor spells them.
class App : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

public:
    enum class Appearance { Auto, Light, Dark };

    explicit App(QObject *parent = nullptr);
    ~App() override;

    bool dark() const { return m_dark; }
    void setAppearance(Appearance appearance);

    /// The built-in default of a setting, from config.reference.toml.
    Q_INVOKABLE QVariant fallback(const QStringList &path) const;
    /// The built-in entries of a table (the default bindings), as {key, value}.
    Q_INVOKABLE QVariantList fallbackEntries(const QStringList &table) const;

    /// The unshifted name of a pressed key under the given layout, as a binding spells it.
    Q_INVOKABLE QString keyName(quint32 scanCode, const QString &text, const QString &layout, const QString &variant);
    /// One spelling per key, so two bindings on the same key compare equal; empty if unknown.
    Q_INVOKABLE QString canonicalKey(const QString &name) const;

    Q_INVOKABLE void openFile(const QString &path) const;
    Q_INVOKABLE QString localPath(const QUrl &url) const;

Q_SIGNALS:
    void darkChanged();

private:
    void refreshDark();

    TomlDoc m_reference;
    Appearance m_appearance = Appearance::Auto;
    bool m_dark = false;
    QFileSystemWatcher *m_watcher;
    xkb_context *m_xkb = nullptr;
    xkb_keymap *m_keymap = nullptr;
    QString m_keymapNames;
};
