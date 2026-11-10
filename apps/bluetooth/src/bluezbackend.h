#pragma once

#include "backend.h"

#include <BluezQt/Types>

#include <functional>

namespace BluezQt {
class Manager;
class PendingCall;
}
class PairingAgent;

/// The machine's Bluetooth, through BlueZ. While it lives it is also the agent BlueZ asks
/// when a device wants a code typed or confirmed.
class BluezBackend : public Backend
{
    Q_OBJECT

public:
    explicit BluezBackend(QObject *parent = nullptr);
    ~BluezBackend() override;

    AdapterInfo adapter() const override;
    QList<DeviceInfo> devices() const override;

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
    friend class PairingAgent;

    void pickAdapter();
    void watch(const BluezQt::DevicePtr &device);
    void report(BluezQt::PendingCall *call, const QString &address, const QString &operation);
    void run(const QString &address, const QString &operation, const std::function<BluezQt::PendingCall *(const BluezQt::DevicePtr &)> &call);
    void ask(const PairingRequest &request, std::function<void(bool, const QString &)> answer);
    void applyScanning();

    BluezQt::Manager *m_manager;
    PairingAgent *m_agent;
    BluezQt::AdapterPtr m_adapter;
    std::function<void(bool, const QString &)> m_answer;
    bool m_scanning = false;
    bool m_wantPower = false;
};
