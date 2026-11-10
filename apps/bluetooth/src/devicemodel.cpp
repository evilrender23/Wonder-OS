#include "devicemodel.h"

#include <algorithm>

DeviceModel::DeviceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_devices.size() || role != Qt::UserRole) {
        return {};
    }
    return Devices::toMap(m_devices.at(index.row()));
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {{Qt::UserRole, "device"}};
}

int DeviceModel::indexOf(const QString &address) const
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).address == address) {
            return i;
        }
    }
    return -1;
}

void DeviceModel::upsert(const DeviceInfo &device)
{
    const int row = indexOf(device.address);
    if (row >= 0) {
        if (m_devices.at(row) == device) {
            return;
        }
        QList<DeviceInfo> rest = m_devices;
        rest.removeAt(row);
        const int target = std::lower_bound(rest.cbegin(), rest.cend(), device, Devices::before) - rest.cbegin();
        if (target == row) {
            m_devices[row] = device;
            Q_EMIT dataChanged(index(row), index(row));
            return;
        }
        // beginMoveRows counts the destination in the list as it is before the move.
        beginMoveRows({}, row, row, {}, target > row ? target + 1 : target);
        m_devices.removeAt(row);
        m_devices.insert(target, device);
        endMoveRows();
        Q_EMIT dataChanged(index(target), index(target));
        return;
    }
    const int target = std::lower_bound(m_devices.cbegin(), m_devices.cend(), device, Devices::before) - m_devices.cbegin();
    beginInsertRows({}, target, target);
    m_devices.insert(target, device);
    endInsertRows();
    Q_EMIT countChanged();
}

void DeviceModel::remove(const QString &address)
{
    const int row = indexOf(address);
    if (row < 0) {
        return;
    }
    beginRemoveRows({}, row, row);
    m_devices.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

void DeviceModel::clear()
{
    if (m_devices.isEmpty()) {
        return;
    }
    beginResetModel();
    m_devices.clear();
    endResetModel();
    Q_EMIT countChanged();
}
