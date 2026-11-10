#include "app.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>

#include <xkbcommon/xkbcommon.h>

namespace {

QString barStatePath()
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
}

/// config.reference.toml documents each default as a commented `# key = value` line;
/// `# #` lines are prose. Dropping the first marker leaves a config that spells out every default.
QString uncommented(const QString &reference)
{
    QStringList lines;
    for (const QString &line : reference.split(QLatin1Char('\n'))) {
        lines << (line.startsWith(QLatin1String("# ")) && !line.startsWith(QLatin1String("# #")) ? line.mid(2) : line);
    }
    return lines.join(QLatin1Char('\n'));
}

QString nameOf(xkb_keysym_t sym)
{
    char buffer[64];
    if (sym == XKB_KEY_NoSymbol || xkb_keysym_get_name(sym, buffer, sizeof buffer) <= 0) {
        return {};
    }
    const QString name = QString::fromLatin1(buffer);
    // Media keys read better as XKB writes them; the rest follow the reference's lower case.
    return name.startsWith(QLatin1String("XF86")) ? name : name.toLower();
}

} // namespace

App::App(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_xkb(xkb_context_new(XKB_CONTEXT_NO_FLAGS))
{
    QFile reference(QStringLiteral(":/reference/config.reference.toml"));
    if (reference.open(QIODevice::ReadOnly)) {
        QString error;
        if (!m_reference.parse(uncommented(QString::fromUtf8(reference.readAll())), &error)) {
            qWarning("config.reference.toml: %s", qPrintable(error));
        }
    }

    const QString bar = barStatePath();
    QDir().mkpath(QFileInfo(bar).absolutePath());
    m_watcher->addPath(QFileInfo(bar).absolutePath());
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    refreshDark();
}

App::~App()
{
    xkb_keymap_unref(m_keymap);
    xkb_context_unref(m_xkb);
}

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
}

void App::refreshDark()
{
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    // WonderlandWM's bar owns the light/dark switch for the whole desktop.
    QFile file(barStatePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QString mode = QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("themeMode")).toString();
        if (!mode.isEmpty()) {
            dark = mode == QLatin1String("dark");
        }
    }
    if (m_appearance != Appearance::Auto) {
        dark = m_appearance == Appearance::Dark;
    }
    if (dark != m_dark) {
        m_dark = dark;
        Q_EMIT darkChanged();
    }
}

QVariant App::fallback(const QStringList &path) const
{
    return m_reference.value(path);
}

QVariantList App::fallbackEntries(const QStringList &table) const
{
    QVariantList out;
    for (const QString &key : m_reference.keys(table)) {
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("value"), m_reference.value(table + QStringList{key})}};
    }
    return out;
}

QString App::keyName(quint32 scanCode, const QString &text, const QString &layout, const QString &variant)
{
    const QString names = layout + QLatin1Char('/') + variant;
    if (m_xkb && (!m_keymap || names != m_keymapNames)) {
        xkb_keymap_unref(m_keymap);
        // Only the first layout of a multi-layout setup: bindings are written against it.
        const QByteArray first = layout.section(QLatin1Char(','), 0, 0).trimmed().toUtf8();
        const QByteArray firstVariant = variant.section(QLatin1Char(','), 0, 0).trimmed().toUtf8();
        xkb_rule_names rule{};
        rule.layout = first.constData();
        rule.variant = firstVariant.constData();
        m_keymap = xkb_keymap_new_from_names(m_xkb, &rule, XKB_KEYMAP_COMPILE_NO_FLAGS);
        m_keymapNames = names;
    }
    if (m_keymap && scanCode != 0) {
        const xkb_keysym_t *syms = nullptr;
        if (xkb_keymap_key_get_syms_by_level(m_keymap, scanCode, 0, 0, &syms) > 0) {
            return nameOf(syms[0]);
        }
    }
    const QList<uint> typed = text.toLower().toUcs4();
    return typed.size() == 1 ? nameOf(xkb_utf32_to_keysym(typed.first())) : QString();
}

QString App::canonicalKey(const QString &name) const
{
    const xkb_keysym_t sym = xkb_keysym_from_name(name.trimmed().toUtf8().constData(), XKB_KEYSYM_CASE_INSENSITIVE);
    return nameOf(sym).toLower();
}

void App::openFile(const QString &path) const
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString App::localPath(const QUrl &url) const
{
    return url.toLocalFile();
}
