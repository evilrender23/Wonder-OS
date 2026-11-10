#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJSValue>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryFile>

namespace {

TomlDoc::Kind kindFromName(const QString &name)
{
    if (name == QLatin1String("bool")) {
        return TomlDoc::Kind::Bool;
    }
    if (name == QLatin1String("int")) {
        return TomlDoc::Kind::Int;
    }
    if (name == QLatin1String("float")) {
        return TomlDoc::Kind::Float;
    }
    if (name == QLatin1String("string")) {
        return TomlDoc::Kind::String;
    }
    return TomlDoc::Kind::Auto;
}

QVariant plain(const QVariant &value)
{
    return value.userType() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}

QStringList pathFrom(const QVariant &value)
{
    return plain(value).toStringList();
}

} // namespace

Config::Config(const QString &path, const QString &validator, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_validator(validator)
    , m_watcher(new QFileSystemWatcher(this))
{
    reload();
    // Editors replace the file rather than write into it, which a watch on the file alone loses.
    const QString dir = QFileInfo(m_path).absolutePath();
    QDir().mkpath(dir);
    m_watcher->addPath(dir);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Config::fileChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &Config::fileChanged);
}

void Config::reload()
{
    QString text;
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly)) {
        text = QString::fromUtf8(file.readAll());
    }
    QString problem;
    TomlDoc doc;
    if (!doc.parse(text, &problem)) {
        m_broken = true;
        setError(tr("config.toml tiene un error de sintaxis (%1). Corrígelo a mano para poder cambiar ajustes desde aquí.").arg(problem));
    } else {
        m_broken = false;
        m_doc = doc;
        m_warnings = check(text).warnings;
        setError(QString());
    }
    if (QFileInfo::exists(m_path) && !m_watcher->files().contains(m_path)) {
        m_watcher->addPath(m_path);
    }
    ++m_revision;
    Q_EMIT changed();
}

void Config::fileChanged()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    if (QString::fromUtf8(file.readAll()) != m_doc.text() || m_broken) {
        m_undo.clear();
        reload();
    }
}

bool Config::has(const QStringList &path) const
{
    return m_doc.contains(path);
}

QVariant Config::get(const QStringList &path) const
{
    return m_doc.value(path);
}

QVariantList Config::entries(const QStringList &table) const
{
    QVariantList out;
    for (const QString &key : m_doc.keys(table)) {
        out << QVariantMap{{QStringLiteral("key"), key}, {QStringLiteral("value"), m_doc.value(table + QStringList{key})}};
    }
    return out;
}

bool Config::set(const QStringList &path, const QVariant &value, const QString &kind)
{
    return apply({QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("value"), value}, {QStringLiteral("kind"), kind}}});
}

bool Config::unset(const QStringList &path)
{
    return apply({QVariantMap{{QStringLiteral("path"), path}, {QStringLiteral("remove"), true}}});
}

bool Config::apply(const QVariantList &changes)
{
    if (m_broken) {
        ++m_revision;
        Q_EMIT changed();
        return false;
    }
    TomlDoc next = m_doc;
    for (const QVariant &item : changes) {
        const QVariantMap change = plain(item).toMap();
        const QStringList path = pathFrom(change.value(QStringLiteral("path")));
        if (path.isEmpty()) {
            continue;
        }
        if (change.value(QStringLiteral("remove")).toBool()) {
            next.remove(path);
        } else {
            next.set(path, plain(change.value(QStringLiteral("value"))), kindFromName(change.value(QStringLiteral("kind")).toString()));
        }
    }
    return commit(next.text(), true);
}

void Config::undo()
{
    if (m_undo.isEmpty() || m_broken) {
        return;
    }
    const QString previous = m_undo.takeLast();
    if (!commit(previous, false)) {
        m_undo << previous;
    }
}

void Config::clearError()
{
    setError(QString());
}

void Config::setError(const QString &error)
{
    if (error != m_error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
}

bool Config::commit(const QString &text, bool undoable)
{
    const QString before = m_doc.text();
    bool ok = text == before;
    if (!ok) {
        TomlDoc next;
        QString problem;
        const Check result = next.parse(text, &problem) ? check(text) : Check{false, problem, {}};
        QStringList fresh;
        for (const QString &warning : result.warnings) {
            if (!m_warnings.contains(warning)) {
                fresh << warning;
            }
        }
        if (!result.ok) {
            setError(result.error);
        } else if (!fresh.isEmpty()) {
            setError(fresh.join(QLatin1Char('\n')));
        } else if (!write(text)) {
            setError(tr("No se pudo escribir %1.").arg(m_path));
        } else {
            ok = true;
            m_doc = next;
            m_warnings = result.warnings;
            if (undoable) {
                m_undo << before;
                if (m_undo.size() > 100) {
                    m_undo.removeFirst();
                }
            }
            setError(QString());
        }
    }
    // Also after a refusal: controls re-read the value and snap back to what is on disk.
    ++m_revision;
    Q_EMIT changed();
    return ok;
}

bool Config::write(const QString &text)
{
    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());
    // A config linked from a dotfiles repo stays a link: write through to its target.
    const QString target = info.isSymLink() && !info.canonicalFilePath().isEmpty() ? info.canonicalFilePath() : m_path;
    if (!m_backedUp && QFileInfo::exists(target)) {
        const QString backup = m_path + QLatin1String(".sistema.bak");
        QFile::remove(backup);
        QFile::copy(target, backup);
    }
    m_backedUp = true;
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(text.toUtf8());
    return file.commit();
}

Config::Check Config::check(const QString &text) const
{
    Check result;
    if (m_validator.isEmpty()) {
        return result;
    }
    QTemporaryFile file(QDir::tempPath() + QLatin1String("/sistema-XXXXXX.toml"));
    if (!file.open()) {
        return result;
    }
    file.write(text.toUtf8());
    file.flush();

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.remove(QStringLiteral("RUST_LOG"));
    process.setProcessEnvironment(env);
    process.start(m_validator, {QStringLiteral("--config"), file.fileName(), QStringLiteral("--check-config")});
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        return result;
    }
    QString output = QString::fromUtf8(process.readAll());
    static const QRegularExpression ansi(QStringLiteral("\x1b\\[[0-9;]*m"));
    output.remove(ansi);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.ok = false;
        result.error = output.trimmed();
        result.error.remove(QStringLiteral("driftwm: config error: "));
        return result;
    }
    // The compositor colours its log lines even into a pipe.
    static const QRegularExpression colour(QStringLiteral("\x1b\\[[0-9;]*m"));
    static const QRegularExpression warning(QStringLiteral("\\bWARN\\s+\\S+:\\s+(?:config:\\s*)?(.*)$"));
    for (QString line : output.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch match = warning.match(line.remove(colour));
        if (match.hasMatch()) {
            result.warnings << match.captured(1).trimmed();
        }
    }
    return result;
}
