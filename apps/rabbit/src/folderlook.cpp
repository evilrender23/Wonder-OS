#include "folderlook.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cerrno>
#include <sys/xattr.h>

static const char *const attribute = "user.wonderland.folder";

static QString cleanColor(QString color)
{
    if (color.startsWith(QLatin1Char('#'))) {
        color.remove(0, 1);
    }
    static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{6}$"));
    return hex.match(color).hasMatch() ? color.toLower() : QString();
}

static QString cleanSymbol(const QString &symbol)
{
    static const QRegularExpression name(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    return symbol.size() <= 40 && name.match(symbol).hasMatch() ? symbol : QString();
}

static QString cleanEmoji(const QString &emoji)
{
    // One emoji can be many code points (a family, a flag); a sentence is not an emoji.
    const QString trimmed = emoji.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 24) {
        return {};
    }
    // Keycaps (1️⃣, #️⃣) are the only emoji with a plain character in them.
    const bool keycap = trimmed.contains(QChar(0x20E3));
    for (const QChar c : trimmed) {
        if (c.isSpace() || c == QLatin1Char('~') || (c.unicode() < 0x80 && !keycap)) {
            return {};
        }
    }
    return trimmed;
}

QByteArray FolderLook::toAttribute() const
{
    if (isEmpty()) {
        return {};
    }
    QJsonObject object;
    if (!color.isEmpty()) {
        object.insert(QStringLiteral("color"), QString(QLatin1Char('#') + color));
    }
    if (!symbol.isEmpty()) {
        object.insert(QStringLiteral("symbol"), symbol);
    } else if (!emoji.isEmpty()) {
        object.insert(QStringLiteral("emoji"), emoji);
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

FolderLook FolderLook::fromAttribute(const QByteArray &value)
{
    FolderLook look;
    const QJsonObject object = QJsonDocument::fromJson(value).object();
    look.color = cleanColor(object.value(QStringLiteral("color")).toString());
    look.symbol = cleanSymbol(object.value(QStringLiteral("symbol")).toString());
    if (look.symbol.isEmpty()) {
        look.emoji = cleanEmoji(object.value(QStringLiteral("emoji")).toString());
    }
    return look;
}

QString FolderLook::iconName(const QString &base) const
{
    if (isEmpty()) {
        return base;
    }
    QString name = base + QLatin1Char('~') + color + QLatin1Char('~');
    if (!symbol.isEmpty()) {
        name += QStringLiteral("s~") + symbol;
    } else if (!emoji.isEmpty()) {
        // Hex, so the name survives any URL handling on its way to the image provider.
        name += QStringLiteral("e~") + QString::fromLatin1(emoji.toUtf8().toHex());
    }
    return name;
}

FolderLook FolderLook::fromIconName(const QString &name, QString *base)
{
    const QStringList parts = name.split(QLatin1Char('~'));
    if (base) {
        *base = parts.value(0);
    }
    FolderLook look;
    if (parts.size() < 2) {
        return look;
    }
    look.color = cleanColor(parts.value(1));
    if (parts.value(2) == QLatin1String("s")) {
        look.symbol = cleanSymbol(parts.value(3));
    } else if (parts.value(2) == QLatin1String("e")) {
        look.emoji = cleanEmoji(QString::fromUtf8(QByteArray::fromHex(parts.value(3).toLatin1())));
    }
    return look;
}

FolderLooks::FolderLooks()
{
    reloadTint();
}

QString FolderLooks::tintFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/wonderland/appearance.json");
}

void FolderLooks::reloadTint()
{
    QFile file(tintFile());
    const QJsonObject object = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    const QString tint = cleanColor(object.value(QStringLiteral("folder_tint")).toString());
    if (tint != m_tint) {
        m_tint = tint;
        Q_EMIT tintChanged();
    }
}

void FolderLooks::watchTint()
{
    if (m_watching) {
        return;
    }
    m_watching = true;
    // The folder, not the file: Sistema replaces the file, and a watch on it would be lost.
    const QString dir = QFileInfo(tintFile()).absolutePath();
    QDir().mkpath(dir);
    auto *watcher = new QFileSystemWatcher(this);
    watcher->addPath(dir);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &FolderLooks::reloadTint);
    reloadTint();
}

FolderLooks *FolderLooks::self()
{
    static FolderLooks instance;
    return &instance;
}

static QByteArray readAttribute(const QString &path)
{
    const QByteArray file = QFile::encodeName(path);
    const ssize_t length = getxattr(file.constData(), attribute, nullptr, 0);
    if (length <= 0 || length > 1024) {
        return {};
    }
    QByteArray value(length, Qt::Uninitialized);
    const ssize_t read = getxattr(file.constData(), attribute, value.data(), value.size());
    if (read <= 0) {
        return {};
    }
    value.truncate(read);
    return value;
}

FolderLook FolderLooks::lookFor(const QString &path) const
{
    return path.isEmpty() ? FolderLook() : FolderLook::fromAttribute(readAttribute(path));
}

QString FolderLooks::picturePath(const QByteArray &value, const QString &tint)
{
    // Without a tint the name is the one it always was, so the pictures already made still serve.
    const QByteArray named = tint.isEmpty() ? value : value + "\ntint=" + tint.toLatin1();
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wonderland/folder-looks/")
        + QString::fromLatin1(QCryptographicHash::hash(named, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".png");
}

QString FolderLooks::paint(const QByteArray &stored, const FolderLook &own) const
{
    // A folder with no colour of its own wears the tint.
    FolderLook look = own;
    if (look.color.isEmpty()) {
        look.color = m_tint;
    }
    if (!m_painter || look.isEmpty()) {
        return {};
    }
    const QString picture = picturePath(stored, m_tint);
    if (QFileInfo::exists(picture)) {
        return picture;
    }
    QDir().mkpath(QFileInfo(picture).absolutePath());
    // Written aside and renamed: a reader never meets half a picture.
    const QString aside = picture + QStringLiteral(".part");
    if (!m_painter(look).save(aside, "PNG") || !QFile::rename(aside, picture)) {
        QFile::remove(aside);
        return {};
    }
    return picture;
}

QString FolderLooks::ensurePicture(const QString &path) const
{
    // Named after the bytes as stored, not as we would write them: that is what a reader has.
    const QByteArray stored = readAttribute(path);
    return paint(stored, FolderLook::fromAttribute(stored));
}

bool FolderLooks::setLook(const QString &path, const FolderLook &given)
{
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return false;
    }
    FolderLook look;
    look.color = cleanColor(given.color);
    look.symbol = cleanSymbol(given.symbol);
    look.emoji = look.symbol.isEmpty() ? cleanEmoji(given.emoji) : QString();
    if (look == lookFor(path)) {
        return true;
    }
    const QByteArray file = QFile::encodeName(path);
    const QByteArray value = look.toAttribute();
    // The picture first: the attribute is what tells the desktop to go and look for it.
    paint(value, look);
    const bool written = value.isEmpty() ? (removexattr(file.constData(), attribute) == 0 || errno == ENODATA)
                                         : setxattr(file.constData(), attribute, value.constData(), value.size(), 0) == 0;
    if (written) {
        Q_EMIT changed(path);
    }
    return written;
}
