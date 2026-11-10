#pragma once

#include "backend.h"

#include <QHash>

/// A Bluetooth that lives in memory: what the tests drive and what `--demo` shows.
/// Every operation ends on the next turn of the event loop, as the real ones end later.
class FakeBackend : public Backend
{
    Q_OBJECT

public:
    explicit FakeBackend(QObject *parent = nullptr);

    /// An adapter with a few paired devices and a few in range.
    void loadSample();
    void setAdapter(const AdapterInfo &adapter);
    void addDevice(const DeviceInfo &device);
    void dropDevice(const QString &address);
    /// The next operation of this kind fails with this error.
    void failNext(const QString &operation, const QString &error);
    /// Pairing with this device stops to ask for a confirmation of this code.
    void askCodeFor(const QString &address, quint32 code);
    void request(const PairingRequest &request);
    /// For the pictures: confirm, show, pin, authorize (a pairing question), off, none
    /// (no adapter), empty (no devices), fail (a connection that fails).
    Q_INVOKABLE void simulate(const QString &what);

    AdapterInfo adapter() const override { return m_adapter; }
    QList<DeviceInfo> devices() const override { return m_devices.values(); }

    void setPowered(bool powered) override;
    void setDiscoverable(bool discoverable) override;
    void setAdapterName(const QString &name) override;
    void setScanning(bool scanning) override;
    void connectDevice(const QString &address) override;
    void disconnectDevice(const QString &address) override;
    void pair(const QString &address) override;
    void cancelPairing(const QString &address) override;
    void forget(const QString &address) override;
    void setTrusted(const QString &address, bool trusted) override;
    void setBlocked(const QString &address, bool blocked) override;
    void rename(const QString &address, const QString &name) override;
    void answerPairing(bool accept, const QString &text) override;

private:
    void later(const QString &address, const QString &operation, std::function<void(DeviceInfo &)> change);
    void finishPairing(QString address, bool accepted);

    AdapterInfo m_adapter;
    QHash<QString, DeviceInfo> m_devices;
    QHash<QString, QString> m_failures;
    QHash<QString, quint32> m_codes;
    QString m_pairing;
};
