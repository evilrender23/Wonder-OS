#pragma once

#include <QJsonObject>
#include <QString>

/// A small JSON file of settings, written whole on every change.
class Store
{
public:
    explicit Store(const QString &path);

    QJsonValue value(const QString &key, const QJsonValue &fallback = {}) const;
    void set(const QString &key, const QJsonValue &value);

    /// Reads a JSON object; an absent or broken file is an empty one.
    static QJsonObject read(const QString &path);
    /// Writes through a temporary file, so a crash never leaves half a file.
    static bool write(const QString &path, const QJsonObject &object);

private:
    QString m_path;
    QJsonObject m_data;
};
