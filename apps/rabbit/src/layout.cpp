#include "layout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <cmath>
#include <sys/xattr.h>

static const char idAttribute[] = "user.wonderland.folder-id";

struct FolderData {
    QString id;
    QString location;
    QString localPath;
    QString view;
    QString arrange;
    int iconSize = 0;
    QSizeF cell;
    QHash<QString, QPointF> icons;
    bool dirty = false;
    bool onDisk = false;
};

static QHash<QString, std::weak_ptr<FolderData>> &openFolders()
{
    static QHash<QString, std::weak_ptr<FolderData>> folders;
    return folders;
}

static QList<FolderLayout *> &instances()
{
    static QList<FolderLayout *> list;
    return list;
}

QString FolderLayout::dataDirectory()
{
    QString base = qEnvironmentVariable("WONDERLAND_RABBIT_DATA");
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return base + QStringLiteral("/folders");
}

QString FolderLayout::folderId(const QString &path)
{
    char buffer[64];
    const ssize_t length = getxattr(QFile::encodeName(path).constData(), idAttribute, buffer, sizeof(buffer));
    if (length <= 0) {
        return QString();
    }
    const QString id = QString::fromLatin1(buffer, int(length));
    // The id names a file: take nothing but what we write ourselves.
    static const QString allowed = QStringLiteral("0123456789abcdef");
    for (const QChar c : id) {
        if (!allowed.contains(c)) {
            return QString();
        }
    }
    return id;
}

static bool writeFolderId(const QString &path, const QString &id)
{
    const QByteArray value = id.toLatin1();
    return setxattr(QFile::encodeName(path).constData(), idAttribute, value.constData(), value.size(), 0) == 0;
}

static QString newId()
{
    return QUuid::createUuid().toString(QUuid::Id128);
}

static QString hashed(const QString &prefix, const QString &text)
{
    return prefix + QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
}

static QString fileFor(const QString &id)
{
    return FolderLayout::dataDirectory() + QLatin1Char('/') + id + QStringLiteral(".json");
}

static bool readFolder(FolderData &data)
{
    QFile file(fileFor(data.id));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    data.location = object.value(QLatin1String("location")).toString();
    data.view = object.value(QLatin1String("view")).toString();
    data.arrange = object.value(QLatin1String("arrange")).toString();
    data.iconSize = object.value(QLatin1String("iconSize")).toInt();
    const QJsonArray cell = object.value(QLatin1String("cell")).toArray();
    if (cell.size() == 2) {
        data.cell = QSizeF(cell.at(0).toDouble(), cell.at(1).toDouble());
    }
    const QJsonObject icons = object.value(QLatin1String("icons")).toObject();
    for (auto it = icons.begin(); it != icons.end(); ++it) {
        const QJsonArray point = it.value().toArray();
        if (point.size() == 2) {
            data.icons.insert(it.key(), QPointF(point.at(0).toDouble(), point.at(1).toDouble()));
        }
    }
    data.onDisk = true;
    return true;
}

static void writeFolder(FolderData &data)
{
    data.dirty = false;
    const QString path = fileFor(data.id);
    if (data.icons.isEmpty() && data.view.isEmpty() && data.arrange.isEmpty() && data.iconSize == 0) {
        // Nothing of its own left: the folder is shown with the defaults again.
        QFile::remove(path);
        data.onDisk = false;
        return;
    }
    QJsonObject icons;
    for (auto it = data.icons.begin(); it != data.icons.end(); ++it) {
        icons.insert(it.key(), QJsonArray{std::round(it.value().x() * 10) / 10, std::round(it.value().y() * 10) / 10});
    }
    QJsonObject object;
    object.insert(QLatin1String("location"), data.location);
    if (!data.view.isEmpty()) {
        object.insert(QLatin1String("view"), data.view);
    }
    if (!data.arrange.isEmpty()) {
        object.insert(QLatin1String("arrange"), data.arrange);
    }
    if (data.iconSize > 0) {
        object.insert(QLatin1String("iconSize"), data.iconSize);
    }
    if (data.cell.isValid()) {
        object.insert(QLatin1String("cell"), QJsonArray{data.cell.width(), data.cell.height()});
    }
    object.insert(QLatin1String("icons"), icons);
    QDir().mkpath(FolderLayout::dataDirectory());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        data.onDisk = file.commit();
    }
}

FolderLayout::FolderLayout(QObject *parent)
    : QObject(parent)
{
    instances() << this;
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &FolderLayout::flush);
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &FolderLayout::flush);
    }
}

FolderLayout::~FolderLayout()
{
    release();
    instances().removeAll(this);
}

void FolderLayout::release()
{
    flush();
    m_data.reset();
}

void FolderLayout::flush()
{
    m_saveTimer.stop();
    if (m_data && m_data->dirty) {
        writeFolder(*m_data);
    }
}

/// True if `other` is a folder that exists and isn't `path` reached another way (a symlink).
static bool isAnotherFolder(const QString &other, const QString &path)
{
    const QFileInfo info(other);
    return info.isDir() && info.canonicalFilePath() != QFileInfo(path).canonicalFilePath();
}

void FolderLayout::setUrl(const QUrl &url)
{
    QUrl clean = url.adjusted(QUrl::NormalizePathSegments);
    if (clean.path().size() > 1) {
        clean = clean.adjusted(QUrl::StripTrailingSlash);
    }
    if (clean == m_url) {
        return;
    }
    release();
    m_url = clean;
    if (m_url.isValid() && !m_url.isEmpty()) {
        const QString path = m_url.isLocalFile() ? m_url.toLocalFile() : QString();
        const QString location = path.isEmpty() ? m_url.toString() : path;
        const QString attribute = path.isEmpty() ? QString() : folderId(path);
        const QString id = attribute.isEmpty() ? hashed(path.isEmpty() ? QStringLiteral("u-") : QStringLiteral("p-"), location) : QStringLiteral("x-") + attribute;

        m_data = openFolders().value(id).lock();
        if (m_data && !attribute.isEmpty() && m_data->localPath != path && isAnotherFolder(m_data->localPath, path) && folderId(m_data->localPath) == attribute) {
            // Open in another window under another path, both carrying the id: this one is a copy.
            auto copy = std::make_shared<FolderData>(*m_data);
            m_data = copy;
            m_data->localPath = path;
            m_data->location = location;
            const QString fresh = newId();
            m_data->id = writeFolderId(path, fresh) ? QStringLiteral("x-") + fresh : hashed(QStringLiteral("p-"), location);
            m_data->dirty = true;
            openFolders().insert(m_data->id, m_data);
            writeFolder(*m_data);
        } else if (!m_data) {
            m_data = std::make_shared<FolderData>();
            m_data->id = id;
            readFolder(*m_data);
            const QString known = m_data->location;
            m_data->location = location;
            m_data->localPath = path;
            if (!attribute.isEmpty() && !known.isEmpty() && known != location) {
                if (isAnotherFolder(known, path) && folderId(known) == attribute) {
                    // The folder was copied along with its id: the copy starts
                    // from the same layout and goes its own way from here.
                    const QString fresh = newId();
                    m_data->id = writeFolderId(path, fresh) ? QStringLiteral("x-") + fresh : hashed(QStringLiteral("p-"), location);
                    m_data->onDisk = false;
                }
                // Otherwise it was renamed or moved: same folder, new address.
                m_data->dirty = true;
            }
            if (!path.isEmpty()) {
                // Files deleted behind our back leave their place free.
                const QStringList names = m_data->icons.keys();
                for (const QString &name : names) {
                    const QFileInfo info(path + QLatin1Char('/') + name);
                    if (!info.exists() && !info.isSymLink()) {
                        m_data->icons.remove(name);
                        m_data->dirty = true;
                    }
                }
            }
            openFolders().insert(m_data->id, m_data);
            if (m_data->dirty) {
                writeFolder(*m_data);
            }
        }
    }
    Q_EMIT urlChanged();
    Q_EMIT storedChanged();
}

void FolderLayout::touch()
{
    if (!m_data) {
        return;
    }
    if (m_data->id.startsWith(QLatin1String("p-")) && !m_data->localPath.isEmpty()) {
        // First thing worth remembering: give the folder its id, so the
        // layout follows it when it is renamed or moved.
        const QString fresh = newId();
        if (writeFolderId(m_data->localPath, fresh)) {
            if (m_data->onDisk) {
                QFile::remove(fileFor(m_data->id));
                m_data->onDisk = false;
            }
            openFolders().remove(m_data->id);
            m_data->id = QStringLiteral("x-") + fresh;
            openFolders().insert(m_data->id, m_data);
        }
    }
    m_data->dirty = true;
    m_saveTimer.start();
    const QList<FolderLayout *> all = instances();
    for (FolderLayout *other : all) {
        if (other->m_data == m_data) {
            Q_EMIT other->storedChanged();
            if (other != this) {
                Q_EMIT other->changed();
            }
        }
    }
}

QString FolderLayout::view() const
{
    return m_data ? m_data->view : QString();
}

void FolderLayout::setView(const QString &view)
{
    if (m_data && m_data->view != view) {
        m_data->view = view;
        touch();
    }
}

int FolderLayout::iconSize() const
{
    return m_data ? m_data->iconSize : 0;
}

void FolderLayout::setIconSize(int size)
{
    if (m_data && m_data->iconSize != size) {
        m_data->iconSize = size;
        touch();
    }
}

QString FolderLayout::arrange() const
{
    return m_data ? m_data->arrange : QString();
}

void FolderLayout::setArrange(const QString &arrange)
{
    if (m_data && m_data->arrange != arrange) {
        m_data->arrange = arrange;
        touch();
    }
}

bool FolderLayout::arranged() const
{
    return m_data && !m_data->icons.isEmpty();
}

static int columnsFor(qreal width, qreal cellWidth)
{
    return qMax(1, int(std::floor((width + 0.5) / qMax<qreal>(1, cellWidth))));
}

/// The grid cells taken by icons, wherever on or off the grid they sit.
class Occupancy
{
public:
    Occupancy(qreal cellWidth, qreal cellHeight)
        : m_width(qMax<qreal>(1, cellWidth))
        , m_height(qMax<qreal>(1, cellHeight))
    {
    }

    void add(const QPointF &position)
    {
        const int column = int(std::round(position.x() / m_width));
        const int row = int(std::round(position.y() / m_height));
        for (int c = column - 1; c <= column + 1; ++c) {
            for (int r = row - 1; r <= row + 1; ++r) {
                // An icon dropped between cells takes every cell it mostly covers.
                if (c >= 0 && r >= 0 && std::abs(position.x() - c * m_width) < m_width * 0.6 && std::abs(position.y() - r * m_height) < m_height * 0.6) {
                    m_taken.insert(key(c, r));
                }
            }
        }
    }

    bool taken(int column, int row) const
    {
        return m_taken.contains(key(column, row));
    }

    QPointF take(int column, int row)
    {
        m_taken.insert(key(column, row));
        return QPointF(column * m_width, row * m_height);
    }

    /// The first free cell in reading order, from `cursor` on.
    QPointF takeNext(int columns, int &cursor)
    {
        while (taken(cursor % columns, cursor / columns)) {
            ++cursor;
        }
        const QPointF position = take(cursor % columns, cursor / columns);
        ++cursor;
        return position;
    }

    /// The free cell nearest to `position`.
    QPointF takeNearest(const QPointF &position)
    {
        const int column = qMax(0, int(std::round(position.x() / m_width)));
        const int row = qMax(0, int(std::round(position.y() / m_height)));
        for (int ring = 0;; ++ring) {
            int bestColumn = -1;
            int bestRow = -1;
            qreal best = 0;
            for (int c = qMax(0, column - ring); c <= column + ring; ++c) {
                for (int r = qMax(0, row - ring); r <= row + ring; ++r) {
                    if (qMax(std::abs(c - column), std::abs(r - row)) != ring || taken(c, r)) {
                        continue;
                    }
                    const qreal distance = std::hypot(c * m_width - position.x(), r * m_height - position.y());
                    if (bestColumn < 0 || distance < best) {
                        best = distance;
                        bestColumn = c;
                        bestRow = r;
                    }
                }
            }
            if (bestColumn >= 0) {
                return take(bestColumn, bestRow);
            }
        }
    }

private:
    static quint64 key(int column, int row)
    {
        return (quint64(quint32(column)) << 32) | quint32(row);
    }

    qreal m_width;
    qreal m_height;
    QSet<quint64> m_taken;
};

static QVariantMap toMap(const QStringList &names, const QHash<QString, QPointF> &positions)
{
    QVariantMap map;
    for (const QString &name : names) {
        map.insert(name, positions.value(name));
    }
    return map;
}

QVariantMap FolderLayout::flow(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight) const
{
    const int columns = columnsFor(width, cellWidth);
    QVariantMap map;
    for (int i = 0; i < names.size(); ++i) {
        map.insert(names.at(i), QPointF((i % columns) * cellWidth, (i / columns) * cellHeight));
    }
    return map;
}

void FolderLayout::rescale(qreal cellWidth, qreal cellHeight)
{
    const QSizeF cell(cellWidth, cellHeight);
    if (!m_data || m_data->cell == cell) {
        return;
    }
    if (m_data->cell.isValid() && !m_data->icons.isEmpty()) {
        // A new icon size: the arrangement grows or shrinks with the icons, so they neither pile up nor drift apart.
        const qreal sx = cellWidth / m_data->cell.width();
        const qreal sy = cellHeight / m_data->cell.height();
        for (auto it = m_data->icons.begin(); it != m_data->icons.end(); ++it) {
            it.value() = QPointF(it.value().x() * sx, it.value().y() * sy);
        }
    }
    m_data->cell = cell;
    if (!m_data->icons.isEmpty()) {
        touch();
    }
}

QVariantMap FolderLayout::place(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight)
{
    if (!arranged()) {
        return flow(names, width, cellWidth, cellHeight);
    }
    rescale(cellWidth, cellHeight);
    QStringList fresh;
    Occupancy occupancy(cellWidth, cellHeight);
    for (const QString &name : names) {
        const auto it = m_data->icons.constFind(name);
        if (it == m_data->icons.constEnd()) {
            fresh << name;
        } else {
            occupancy.add(it.value());
        }
    }
    if (!fresh.isEmpty()) {
        const int columns = columnsFor(width, cellWidth);
        int cursor = 0;
        for (const QString &name : std::as_const(fresh)) {
            m_data->icons.insert(name, occupancy.takeNext(columns, cursor));
        }
        touch();
    }
    return toMap(names, m_data->icons);
}

void FolderLayout::freeze(const QStringList &names, qreal width, qreal cellWidth, qreal cellHeight)
{
    if (!m_data) {
        return;
    }
    const int columns = columnsFor(width, cellWidth);
    m_data->icons.clear();
    m_data->cell = QSizeF(cellWidth, cellHeight);
    for (int i = 0; i < names.size(); ++i) {
        m_data->icons.insert(names.at(i), QPointF((i % columns) * cellWidth, (i / columns) * cellHeight));
    }
    touch();
}

void FolderLayout::moveTo(const QVariantMap &positions, bool snap, qreal cellWidth, qreal cellHeight)
{
    if (!m_data || positions.isEmpty()) {
        return;
    }
    rescale(cellWidth, cellHeight);
    Occupancy occupancy(cellWidth, cellHeight);
    if (snap) {
        for (auto it = m_data->icons.constBegin(); it != m_data->icons.constEnd(); ++it) {
            if (!positions.contains(it.key())) {
                occupancy.add(it.value());
            }
        }
    }
    for (auto it = positions.constBegin(); it != positions.constEnd(); ++it) {
        const QPointF wanted = it.value().toPointF();
        const QPointF inside(qMax<qreal>(0, wanted.x()), qMax<qreal>(0, wanted.y()));
        m_data->icons.insert(it.key(), snap ? occupancy.takeNearest(inside) : inside);
    }
    touch();
}

void FolderLayout::reserve(const QStringList &names, const QPointF &at, qreal width, qreal cellWidth, qreal cellHeight)
{
    if (!m_data || names.isEmpty() || !arranged()) {
        return;
    }
    rescale(cellWidth, cellHeight);
    Occupancy occupancy(cellWidth, cellHeight);
    for (auto it = m_data->icons.constBegin(); it != m_data->icons.constEnd(); ++it) {
        if (!names.contains(it.key())) {
            occupancy.add(it.value());
        }
    }
    const QPointF inside(qMax<qreal>(0, at.x()), qMax<qreal>(0, at.y()));
    const bool snap = m_data->arrange == QLatin1String("grid");
    const int columns = columnsFor(width, cellWidth);
    int cursor = qMax(0, int(std::round(inside.y() / cellHeight))) * columns + qBound(0, int(std::round(inside.x() / cellWidth)), columns - 1);
    for (int i = 0; i < names.size(); ++i) {
        if (i == 0 && !snap) {
            occupancy.add(inside);
            m_data->icons.insert(names.at(i), inside);
        } else if (i == 0) {
            m_data->icons.insert(names.at(i), occupancy.takeNearest(inside));
        } else {
            m_data->icons.insert(names.at(i), occupancy.takeNext(columns, cursor));
        }
    }
    touch();
}

void FolderLayout::cleanUp(const QStringList &names, qreal cellWidth, qreal cellHeight)
{
    if (!m_data || !arranged()) {
        return;
    }
    rescale(cellWidth, cellHeight);
    QStringList ordered;
    for (const QString &name : names) {
        if (m_data->icons.contains(name)) {
            ordered << name;
        }
    }
    // Top to bottom, left to right: where two icons want the same cell, the one already nearer to it keeps it.
    std::stable_sort(ordered.begin(), ordered.end(), [this, cellHeight](const QString &a, const QString &b) {
        const QPointF pa = m_data->icons.value(a);
        const QPointF pb = m_data->icons.value(b);
        const int ra = int(std::round(pa.y() / cellHeight));
        const int rb = int(std::round(pb.y() / cellHeight));
        return ra != rb ? ra < rb : pa.x() < pb.x();
    });
    Occupancy occupancy(cellWidth, cellHeight);
    for (const QString &name : std::as_const(ordered)) {
        m_data->icons.insert(name, occupancy.takeNearest(m_data->icons.value(name)));
    }
    touch();
}

void FolderLayout::rename(const QString &from, const QString &to)
{
    if (!m_data || from == to) {
        return;
    }
    const auto it = m_data->icons.constFind(from);
    if (it != m_data->icons.constEnd()) {
        const QPointF position = it.value();
        m_data->icons.remove(from);
        m_data->icons.insert(to, position);
        touch();
    }
}

void FolderLayout::reset()
{
    if (m_data && !m_data->icons.isEmpty()) {
        m_data->icons.clear();
        touch();
    }
}
