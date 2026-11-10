#include "bluezbackend.h"

#include <BluezQt/Adapter>
#include <BluezQt/Agent>
#include <BluezQt/Battery>
#include <BluezQt/Device>
#include <BluezQt/InitManagerJob>
#include <BluezQt/Manager>
#include <BluezQt/PendingCall>
#include <BluezQt/Request>

#include <QDBusObjectPath>

static DeviceInfo infoOf(const BluezQt::DevicePtr &device)
{
    DeviceInfo info;
    info.address = device->address();
    info.name = device->name();
    info.kind = device->type() == BluezQt::Device::Wearable ? QStringLiteral("watch") : Devices::kindOf(device->icon());
    info.paired = device->isPaired();
    info.trusted = device->isTrusted();
    info.blocked = device->isBlocked();
    info.connected = device->isConnected();
    info.battery = device->battery() ? device->battery()->percentage() : -1;
    return info;
}

class PairingAgent : public BluezQt::Agent
{
    Q_OBJECT

public:
    explicit PairingAgent(BluezBackend *backend)
        : BluezQt::Agent(backend)
        , m_backend(backend)
    {
    }

    QDBusObjectPath objectPath() const override { return QDBusObjectPath(QStringLiteral("/org/wonderland/Bluetooth/Agent")); }

    void requestPinCode(BluezQt::DevicePtr device, const BluezQt::Request<QString> &request) override
    {
        m_backend->ask({QStringLiteral("pin"), device->address(), device->name(), {}}, [request](bool accept, const QString &text) {
            if (accept && !text.isEmpty()) {
                request.accept(text);
            } else {
                request.reject();
            }
        });
    }

    void requestPasskey(BluezQt::DevicePtr device, const BluezQt::Request<quint32> &request) override
    {
        m_backend->ask({QStringLiteral("passkey"), device->address(), device->name(), {}}, [request](bool accept, const QString &text) {
            bool number = false;
            const quint32 passkey = QString(text).remove(QLatin1Char(' ')).toUInt(&number);
            if (accept && number && passkey <= 999999) {
                request.accept(passkey);
            } else {
                request.reject();
            }
        });
    }

    void displayPinCode(BluezQt::DevicePtr device, const QString &pinCode) override
    {
        m_backend->ask({QStringLiteral("show"), device->address(), device->name(), pinCode}, {});
    }

    void displayPasskey(BluezQt::DevicePtr device, const QString &passkey, const QString &) override
    {
        m_backend->ask({QStringLiteral("show"), device->address(), device->name(), Devices::passkey(passkey.toUInt())}, {});
    }

    void requestConfirmation(BluezQt::DevicePtr device, const QString &passkey, const BluezQt::Request<> &request) override
    {
        m_backend->ask({QStringLiteral("confirm"), device->address(), device->name(), Devices::passkey(passkey.toUInt())}, yesNo(request));
    }

    void requestAuthorization(BluezQt::DevicePtr device, const BluezQt::Request<> &request) override
    {
        m_backend->ask({QStringLiteral("authorize"), device->address(), device->name(), {}}, yesNo(request));
    }

    void authorizeService(BluezQt::DevicePtr device, const QString &, const BluezQt::Request<> &request) override
    {
        // A device paired here may use what it offers; a stranger has to pair first.
        if (device->isPaired()) {
            request.accept();
        } else {
            request.reject();
        }
    }

    void cancel() override
    {
        m_backend->m_answer = {};
        Q_EMIT m_backend->pairingDismissed();
    }

private:
    static std::function<void(bool, const QString &)> yesNo(const BluezQt::Request<> &request)
    {
        return [request](bool accept, const QString &) {
            if (accept) {
                request.accept();
            } else {
                request.reject();
            }
        };
    }

    BluezBackend *m_backend;
};

BluezBackend::BluezBackend(QObject *parent)
    : Backend(parent)
    , m_manager(new BluezQt::Manager(this))
    , m_agent(new PairingAgent(this))
{
    connect(m_manager, &BluezQt::Manager::adapterAdded, this, &BluezBackend::pickAdapter);
    connect(m_manager, &BluezQt::Manager::adapterRemoved, this, &BluezBackend::pickAdapter);
    connect(m_manager, &BluezQt::Manager::usableAdapterChanged, this, &BluezBackend::pickAdapter);
    connect(m_manager, &BluezQt::Manager::bluetoothBlockedChanged, this, [this](bool blocked) {
        // Unblocking the radio does not switch the adapter on by itself.
        if (!blocked && m_wantPower && m_adapter && !m_adapter->isPowered()) {
            report(m_adapter->setPowered(true), {}, QStringLiteral("power"));
        }
        m_wantPower = false;
    });
    connect(m_manager, &BluezQt::Manager::operationalChanged, this, [this](bool operational) {
        if (operational) {
            m_manager->registerAgent(m_agent);
            m_manager->requestDefaultAgent(m_agent);
        }
        pickAdapter();
    });
    connect(m_manager, &BluezQt::Manager::deviceAdded, this, [this](const BluezQt::DevicePtr &device) {
        if (device->adapter() == m_adapter) {
            watch(device);
            Q_EMIT deviceChanged(infoOf(device));
        }
    });
    connect(m_manager, &BluezQt::Manager::deviceRemoved, this, [this](const BluezQt::DevicePtr &device) {
        device->disconnect(this);
        Q_EMIT deviceRemoved(device->address());
    });

    BluezQt::InitManagerJob *job = m_manager->init();
    connect(job, &BluezQt::InitManagerJob::result, this, [this] {
        if (m_manager->isOperational()) {
            m_manager->registerAgent(m_agent);
            m_manager->requestDefaultAgent(m_agent);
        }
        pickAdapter();
    });
    job->start();
}

BluezBackend::~BluezBackend()
{
    if (m_manager->isOperational()) {
        m_manager->unregisterAgent(m_agent);
    }
}

void BluezBackend::pickAdapter()
{
    BluezQt::AdapterPtr adapter = m_manager->usableAdapter();
    if (!adapter && !m_manager->adapters().isEmpty()) {
        // Switched off, it is still the adapter to switch on.
        adapter = m_adapter && m_manager->adapters().contains(m_adapter) ? m_adapter : m_manager->adapters().constFirst();
    }
    if (adapter == m_adapter) {
        Q_EMIT adapterChanged();
        return;
    }
    if (m_adapter) {
        m_adapter->disconnect(this);
        for (const BluezQt::DevicePtr &device : m_adapter->devices()) {
            device->disconnect(this);
        }
    }
    m_adapter = adapter;
    if (m_adapter) {
        const auto changed = [this] { Q_EMIT adapterChanged(); };
        connect(m_adapter.data(), &BluezQt::Adapter::nameChanged, this, changed);
        connect(m_adapter.data(), &BluezQt::Adapter::poweredChanged, this, [this] {
            Q_EMIT adapterChanged();
            applyScanning();
        });
        connect(m_adapter.data(), &BluezQt::Adapter::discoverableChanged, this, changed);
        connect(m_adapter.data(), &BluezQt::Adapter::discoverableTimeoutChanged, this, changed);
        connect(m_adapter.data(), &BluezQt::Adapter::discoveringChanged, this, changed);
        for (const BluezQt::DevicePtr &device : m_adapter->devices()) {
            watch(device);
        }
    }
    Q_EMIT adapterChanged();
    Q_EMIT devicesReset();
    applyScanning();
}

void BluezBackend::watch(const BluezQt::DevicePtr &device)
{
    const QWeakPointer<BluezQt::Device> weak = device;
    const auto changed = [this, weak] {
        if (const BluezQt::DevicePtr strong = weak.toStrongRef()) {
            Q_EMIT deviceChanged(infoOf(strong));
        }
    };
    connect(device.data(), &BluezQt::Device::deviceChanged, this, changed, Qt::UniqueConnection);
    const auto follow = [this, changed](const BluezQt::BatteryPtr &battery) {
        if (battery) {
            connect(battery.data(), &BluezQt::Battery::percentageChanged, this, changed);
        }
        changed();
    };
    connect(device.data(), &BluezQt::Device::batteryChanged, this, follow);
    if (device->battery()) {
        connect(device->battery().data(), &BluezQt::Battery::percentageChanged, this, changed);
    }
}

AdapterInfo BluezBackend::adapter() const
{
    AdapterInfo info;
    if (m_adapter) {
        info.present = true;
        info.powered = m_adapter->isPowered();
        info.discoverable = m_adapter->isDiscoverable();
        info.discovering = m_adapter->isDiscovering();
        info.discoverableTimeout = int(m_adapter->discoverableTimeout());
        info.name = m_adapter->name();
        info.address = m_adapter->address();
    }
    return info;
}

QList<DeviceInfo> BluezBackend::devices() const
{
    QList<DeviceInfo> out;
    if (m_adapter) {
        for (const BluezQt::DevicePtr &device : m_adapter->devices()) {
            out << infoOf(device);
        }
    }
    return out;
}

void BluezBackend::report(BluezQt::PendingCall *call, const QString &address, const QString &operation)
{
    connect(call, &BluezQt::PendingCall::finished, this, [this, address, operation](BluezQt::PendingCall *done) {
        Q_EMIT finished(address, operation, done->error() == BluezQt::PendingCall::NoError ? QString() : done->errorText());
    });
}

void BluezBackend::run(const QString &address, const QString &operation, const std::function<BluezQt::PendingCall *(const BluezQt::DevicePtr &)> &call)
{
    const BluezQt::DevicePtr device = m_adapter ? m_adapter->deviceForAddress(address) : BluezQt::DevicePtr();
    if (!device) {
        Q_EMIT finished(address, operation, QStringLiteral("org.bluez.Error.DoesNotExist"));
        return;
    }
    report(call(device), address, operation);
}

void BluezBackend::setPowered(bool powered)
{
    if (!m_adapter) {
        return;
    }
    if (powered && m_manager->isBluetoothBlocked()) {
        m_wantPower = true;
        m_manager->setBluetoothBlocked(false);
        return;
    }
    report(m_adapter->setPowered(powered), {}, QStringLiteral("power"));
}

void BluezBackend::setDiscoverable(bool discoverable)
{
    if (m_adapter) {
        report(m_adapter->setDiscoverable(discoverable), {}, QStringLiteral("discoverable"));
    }
}

void BluezBackend::setAdapterName(const QString &name)
{
    if (m_adapter) {
        report(m_adapter->setName(name), {}, QStringLiteral("rename"));
    }
}

void BluezBackend::setScanning(bool scanning)
{
    m_scanning = scanning;
    applyScanning();
}

void BluezBackend::applyScanning()
{
    if (!m_adapter || !m_adapter->isPowered()) {
        return;
    }
    // BlueZ counts discovery per client: stopping ends this app's search, nobody else's.
    if (m_scanning) {
        m_adapter->startDiscovery();
    } else {
        m_adapter->stopDiscovery();
    }
}

void BluezBackend::connectDevice(const QString &address)
{
    run(address, QStringLiteral("connect"), [](const BluezQt::DevicePtr &device) { return device->connectToDevice(); });
}

void BluezBackend::disconnectDevice(const QString &address)
{
    run(address, QStringLiteral("disconnect"), [](const BluezQt::DevicePtr &device) { return device->disconnectFromDevice(); });
}

void BluezBackend::pair(const QString &address)
{
    run(address, QStringLiteral("pair"), [](const BluezQt::DevicePtr &device) { return device->pair(); });
}

void BluezBackend::cancelPairing(const QString &address)
{
    if (const BluezQt::DevicePtr device = m_adapter ? m_adapter->deviceForAddress(address) : BluezQt::DevicePtr()) {
        device->cancelPairing();
    }
}

void BluezBackend::forget(const QString &address)
{
    run(address, QStringLiteral("forget"), [this](const BluezQt::DevicePtr &device) { return m_adapter->removeDevice(device); });
}

void BluezBackend::setTrusted(const QString &address, bool trusted)
{
    run(address, QStringLiteral("trust"), [trusted](const BluezQt::DevicePtr &device) { return device->setTrusted(trusted); });
}

void BluezBackend::setBlocked(const QString &address, bool blocked)
{
    run(address, QStringLiteral("block"), [blocked](const BluezQt::DevicePtr &device) { return device->setBlocked(blocked); });
}

void BluezBackend::rename(const QString &address, const QString &name)
{
    run(address, QStringLiteral("rename"), [name](const BluezQt::DevicePtr &device) { return device->setName(name); });
}

void BluezBackend::ask(const PairingRequest &request, std::function<void(bool, const QString &)> answer)
{
    m_answer = std::move(answer);
    Q_EMIT pairingRequested(request);
}

void BluezBackend::answerPairing(bool accept, const QString &text)
{
    if (const auto answer = std::exchange(m_answer, {})) {
        answer(accept, text);
    }
}

#include "bluezbackend.moc"
