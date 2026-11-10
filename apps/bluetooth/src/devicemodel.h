#pragma once

#include "devices.h"

#include <QAbstractListModel>

/// A list of devices kept in the order the window shows them. Its one role, `device`,
/// is the device as a map (see Devices::toMap).
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit DeviceModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_devices.size(); }
    const QList<DeviceInfo> &devices() const { return m_devices; }
    int indexOf(const QString &address) const;
    bool contains(const QString &address) const { return indexOf(address) >= 0; }

    /// Adds the device or replaces the one with its address, moving it where it sorts.
    void upsert(const DeviceInfo &device);
    void remove(const QString &address);
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    QList<DeviceInfo> m_devices;
};
