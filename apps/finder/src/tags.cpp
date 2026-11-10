#include "tags.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <sys/xattr.h>

static const char *const attribute = "user.xdg.tags";

static QStringList readAttribute(const QString &path)
{
    const QByteArray file = QFile::encodeName(path);
    const ssize_t length = getxattr(file.constData(), attribute, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    QByteArray value(length, Qt::Uninitialized);
    const ssize_t read = getxattr(file.constData(), attribute, value.data(), value.size());
    if (read <= 0) {
        return {};
    }
    value.truncate(read);
    return QString::fromUtf8(value).split(QLatin1Char(','), Qt::SkipEmptyParts);
}

static bool writeAttribute(const QString &path, const QStringList &tags)
{
    const QByteArray file = QFile::encodeName(path);
    if (tags.isEmpty()) {
        return removexattr(file.constData(), attribute) == 0 || errno == ENODATA;
    }
    const QByteArray value = tags.join(QLatin1Char(',')).toUtf8();
    return setxattr(file.constData(), attribute, value.constData(), value.size(), 0) == 0;
}

Tags *Tags::self()
{
    static Tags instance;
    return &instance;
}

Tags::Tags()
{
    load();
}

QStringList Tags::names()
{
    return {QStringLiteral("Red"),  QStringLiteral("Orange"), QStringLiteral("Yellow"), QStringLiteral("Green"),
            QStringLiteral("Blue"), QStringLiteral("Purple"), QStringLiteral("Gray")};
}

QString Tags::label(const QString &name)
{
    static const char *const labels[] = {
        QT_TRANSLATE_NOOP("Tags", "Red"),  QT_TRANSLATE_NOOP("Tags", "Orange"), QT_TRANSLATE_NOOP("Tags", "Yellow"), QT_TRANSLATE_NOOP("Tags", "Green"),
        QT_TRANSLATE_NOOP("Tags", "Blue"), QT_TRANSLATE_NOOP("Tags", "Purple"), QT_TRANSLATE_NOOP("Tags", "Gray"),
    };
    const int index = names().indexOf(name);
    return index < 0 ? name : QCoreApplication::translate("Tags", labels[index]);
}

QStringList Tags::tagsFor(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QStringList present = readAttribute(path);
    QStringList result;
    for (const QString &name : names()) {
        if (present.contains(name)) {
            result << name;
        }
    }
    return result;
}

bool Tags::setTag(const QString &path, const QString &name, bool on)
{
    if (path.isEmpty() || !names().contains(name)) {
        return false;
    }
    QStringList present = readAttribute(path);
    if (present.contains(name) == on) {
        return true;
    }
    if (on) {
        present << name;
    } else {
        present.removeAll(name);
    }
    if (!writeAttribute(path, present)) {
        return false;
    }
    QStringList &indexed = m_index[path];
    indexed = tagsFor(path);
    if (indexed.isEmpty()) {
        m_index.remove(path);
    }
    save();
    Q_EMIT changed(path);
    return true;
}

QStringList Tags::filesFor(const QString &name)
{
    QStringList files;
    bool stale = false;
    for (auto it = m_index.begin(); it != m_index.end();) {
        const QStringList actual = tagsFor(it.key());
        if (actual.isEmpty()) {
            it = m_index.erase(it);
            stale = true;
            continue;
        }
        if (actual != it.value()) {
            it.value() = actual;
            stale = true;
        }
        if (actual.contains(name)) {
            files << it.key();
        }
        ++it;
    }
    if (stale) {
        save();
    }
    files.sort(Qt::CaseInsensitive);
    return files;
}

void Tags::pathRenamed(const QString &from, const QString &to)
{
    bool touched = false;
    const QString prefix = from + QLatin1Char('/');
    const QStringList keys = m_index.keys();
    for (const QString &key : keys) {
        if (key == from || key.startsWith(prefix)) {
            m_index.insert(to + key.mid(from.size()), m_index.take(key));
            touched = true;
        }
    }
    if (touched) {
        save();
    }
}

QString Tags::indexPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/tags.json");
}

void Tags::load()
{
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        QStringList tags;
        for (const QJsonValue &value : it.value().toArray()) {
            tags << value.toString();
        }
        m_index.insert(it.key(), tags);
    }
}

void Tags::save() const
{
    QDir().mkpath(QFileInfo(indexPath()).absolutePath());
    QJsonObject root;
    for (auto it = m_index.begin(); it != m_index.end(); ++it) {
        root.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }
    QSaveFile file(indexPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
