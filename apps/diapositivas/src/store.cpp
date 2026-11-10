#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

Store::Store(const QString &path)
    : m_path(path)
    , m_data(read(path))
{
}

QJsonValue Store::value(const QString &key, const QJsonValue &fallback) const
{
    const auto it = m_data.constFind(key);
    return it == m_data.constEnd() ? fallback : it.value();
}

void Store::set(const QString &key, const QJsonValue &value)
{
    if (m_data.value(key) == value) {
        return;
    }
    m_data.insert(key, value);
    write(m_path, m_data);
}

QJsonObject Store::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool Store::write(const QString &path, const QJsonObject &object)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return file.commit();
}
