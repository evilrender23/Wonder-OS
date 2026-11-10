#pragma once

#include "devices.h"

#include <QObject>

/// The Bluetooth of the machine, as the manager drives it. BlueZ in the app; a made-up one
/// in the tests and the pictures.
class Backend : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual AdapterInfo adapter() const = 0;
    virtual QList<DeviceInfo> devices() const = 0;

    virtual void setPowered(bool powered) = 0;
    virtual void setDiscoverable(bool discoverable) = 0;
    virtual void setAdapterName(const QString &name) = 0;
    virtual void setScanning(bool scanning) = 0;

    virtual void connectDevice(const QString &address) = 0;
    virtual void disconnectDevice(const QString &address) = 0;
    virtual void pair(const QString &address) = 0;
    virtual void cancelPairing(const QString &address) = 0;
    virtual void forget(const QString &address) = 0;
    virtual void setTrusted(const QString &address, bool trusted) = 0;
    virtual void setBlocked(const QString &address, bool blocked) = 0;
    virtual void rename(const QString &address, const QString &name) = 0;

    /// The person's answer to the last pairingRequested; `text` is the code they typed, if any.
    virtual void answerPairing(bool accept, const QString &text) = 0;

Q_SIGNALS:
    void adapterChanged();
    /// The adapter is another one, or gone: every device known so far is void.
    void devicesReset();
    void deviceChanged(const DeviceInfo &device);
    void deviceRemoved(const QString &address);
    /// An operation (connect, disconnect, pair, forget, power…) ended; `error` is empty if well.
    void finished(const QString &address, const QString &operation, const QString &error);
    void pairingRequested(const PairingRequest &request);
    void pairingDismissed();
};
