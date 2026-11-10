#pragma once

#include "backend.h"
#include "devicemodel.h"

#include <QHash>
#include <QObject>
#include <QVariantMap>

/// What the window shows and does: the adapter, the devices that are yours and the ones in
/// range, and the steps of connecting to one (pair, trust, connect).
class Bluetooth : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool present READ present NOTIFY adapterChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY adapterChanged)
    Q_PROPERTY(bool discoverable READ discoverable NOTIFY adapterChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY adapterChanged)
    Q_PROPERTY(int discoverableMinutes READ discoverableMinutes NOTIFY adapterChanged)
    Q_PROPERTY(QString name READ name NOTIFY adapterChanged)
    Q_PROPERTY(QString address READ address NOTIFY adapterChanged)
    Q_PROPERTY(DeviceModel *mine READ mine CONSTANT)
    Q_PROPERTY(DeviceModel *nearby READ nearby CONSTANT)
    /// The question a pairing device is asking, as {kind, address, name, code}; no `kind` when none.
    Q_PROPERTY(QVariantMap request READ request NOTIFY requestChanged)
    /// Grows with every change to a device, for what reads one through device().
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    /// While true (the window is open) the adapter looks for devices in range.
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Bluetooth(Backend *backend, QObject *parent = nullptr);

    bool present() const { return m_adapter.present; }
    bool powered() const { return m_adapter.powered; }
    bool discoverable() const { return m_adapter.discoverable; }
    bool scanning() const { return m_adapter.discovering; }
    int discoverableMinutes() const { return (m_adapter.discoverableTimeout + 59) / 60; }
    QString name() const { return m_adapter.name; }
    QString address() const { return m_adapter.address; }
    DeviceModel *mine() const { return m_mine; }
    DeviceModel *nearby() const { return m_nearby; }
    QVariantMap request() const { return m_request; }
    int revision() const { return m_revision; }
    bool watching() const { return m_watching; }
    void setWatching(bool watching);

    Q_INVOKABLE void setPowered(bool powered);
    Q_INVOKABLE void setDiscoverable(bool discoverable);
    Q_INVOKABLE void rename(const QString &name);

    /// The device as a map (see Devices::toMap); empty if it is gone.
    Q_INVOKABLE QVariantMap device(const QString &address) const;
    /// Pairs first if the device is new, and from then on connects to it on its own.
    Q_INVOKABLE void connectTo(const QString &address);
    Q_INVOKABLE void disconnectFrom(const QString &address);
    Q_INVOKABLE void forget(const QString &address);
    Q_INVOKABLE void setTrusted(const QString &address, bool trusted);
    Q_INVOKABLE void setBlocked(const QString &address, bool blocked);
    Q_INVOKABLE void renameDevice(const QString &address, const QString &name);
    Q_INVOKABLE void answer(bool accept, const QString &text = {});

Q_SIGNALS:
    void adapterChanged();
    void requestChanged();
    void revisionChanged();
    void watchingChanged();
    /// Something went wrong that the person should read.
    void notice(const QString &text);

private:
    void refreshAdapter();
    void reload();
    void place(const DeviceInfo &device);
    void drop(const QString &address);
    void setActivity(const QString &address, const QString &activity);
    void onFinished(const QString &address, const QString &operation, const QString &error);
    void setRequest(const QVariantMap &request);
    void touch();

    Backend *m_backend;
    DeviceModel *m_mine;
    DeviceModel *m_nearby;
    AdapterInfo m_adapter;
    QHash<QString, DeviceInfo> m_devices;
    QHash<QString, QString> m_activity;
    QVariantMap m_request;
    int m_revision = 0;
    bool m_watching = false;
};
