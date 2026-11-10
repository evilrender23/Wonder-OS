#include "bluetooth.h"

Bluetooth::Bluetooth(Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
    , m_mine(new DeviceModel(this))
    , m_nearby(new DeviceModel(this))
{
    connect(backend, &Backend::adapterChanged, this, &Bluetooth::refreshAdapter);
    connect(backend, &Backend::devicesReset, this, &Bluetooth::reload);
    connect(backend, &Backend::deviceChanged, this, [this](const DeviceInfo &device) {
        m_devices.insert(device.address, device);
        place(device);
    });
    connect(backend, &Backend::deviceRemoved, this, &Bluetooth::drop);
    connect(backend, &Backend::finished, this, &Bluetooth::onFinished);
    connect(backend, &Backend::pairingRequested, this, [this](const PairingRequest &request) {
        setRequest({{QStringLiteral("kind"), request.kind},
                    {QStringLiteral("address"), request.address},
                    {QStringLiteral("name"), request.name},
                    {QStringLiteral("code"), request.code}});
    });
    connect(backend, &Backend::pairingDismissed, this, [this] { setRequest({}); });
    m_adapter = backend->adapter();
    reload();
}

void Bluetooth::refreshAdapter()
{
    const AdapterInfo adapter = m_backend->adapter();
    if (adapter == m_adapter) {
        return;
    }
    const bool power = adapter.powered != m_adapter.powered;
    m_adapter = adapter;
    Q_EMIT adapterChanged();
    if (power) {
        if (!m_adapter.powered) {
            m_activity.clear();
        }
        // What is in range is only known while the adapter is on.
        for (const DeviceInfo &device : std::as_const(m_devices)) {
            place(device);
        }
        m_backend->setScanning(m_watching && m_adapter.powered);
    }
}

void Bluetooth::reload()
{
    m_devices.clear();
    m_activity.clear();
    m_mine->clear();
    m_nearby->clear();
    for (const DeviceInfo &device : m_backend->devices()) {
        m_devices.insert(device.address, device);
        place(device);
    }
    touch();
}

void Bluetooth::place(const DeviceInfo &raw)
{
    DeviceInfo device = raw;
    device.activity = m_activity.value(device.address);
    // A blocked device stays in sight, or there would be nowhere to unblock it.
    if (device.paired || device.blocked) {
        m_nearby->remove(device.address);
        m_mine->upsert(device);
    } else {
        m_mine->remove(device.address);
        if (m_adapter.powered && !Devices::isUnnamed(device.name, device.address)) {
            m_nearby->upsert(device);
        } else {
            m_nearby->remove(device.address);
        }
    }
    touch();
}

void Bluetooth::drop(const QString &address)
{
    m_devices.remove(address);
    m_activity.remove(address);
    m_mine->remove(address);
    m_nearby->remove(address);
    touch();
}

void Bluetooth::touch()
{
    ++m_revision;
    Q_EMIT revisionChanged();
}

void Bluetooth::setActivity(const QString &address, const QString &activity)
{
    if (activity.isEmpty()) {
        m_activity.remove(address);
    } else {
        m_activity.insert(address, activity);
    }
    if (m_devices.contains(address)) {
        place(m_devices.value(address));
    }
}

void Bluetooth::setRequest(const QVariantMap &request)
{
    if (request != m_request) {
        m_request = request;
        Q_EMIT requestChanged();
    }
}

void Bluetooth::setWatching(bool watching)
{
    if (watching == m_watching) {
        return;
    }
    m_watching = watching;
    Q_EMIT watchingChanged();
    m_backend->setScanning(m_watching && m_adapter.powered);
}

void Bluetooth::setPowered(bool powered)
{
    if (powered != m_adapter.powered) {
        m_backend->setPowered(powered);
    }
}

void Bluetooth::setDiscoverable(bool discoverable)
{
    m_backend->setDiscoverable(discoverable);
}

void Bluetooth::rename(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty() && trimmed != m_adapter.name) {
        m_backend->setAdapterName(trimmed);
    }
}

QVariantMap Bluetooth::device(const QString &address) const
{
    const auto found = m_devices.constFind(address);
    if (found == m_devices.constEnd()) {
        return {};
    }
    DeviceInfo device = *found;
    device.activity = m_activity.value(address);
    return Devices::toMap(device);
}

void Bluetooth::connectTo(const QString &address)
{
    const auto found = m_devices.constFind(address);
    if (found == m_devices.constEnd() || m_activity.contains(address)) {
        return;
    }
    if (found->paired) {
        setActivity(address, QStringLiteral("connecting"));
        m_backend->connectDevice(address);
    } else {
        setActivity(address, QStringLiteral("pairing"));
        m_backend->pair(address);
    }
}

void Bluetooth::disconnectFrom(const QString &address)
{
    if (!m_devices.contains(address) || m_activity.contains(address)) {
        return;
    }
    setActivity(address, QStringLiteral("disconnecting"));
    m_backend->disconnectDevice(address);
}

void Bluetooth::forget(const QString &address)
{
    if (m_devices.contains(address)) {
        m_backend->forget(address);
    }
}

void Bluetooth::setTrusted(const QString &address, bool trusted)
{
    m_backend->setTrusted(address, trusted);
}

void Bluetooth::setBlocked(const QString &address, bool blocked)
{
    m_backend->setBlocked(address, blocked);
}

void Bluetooth::renameDevice(const QString &address, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (!trimmed.isEmpty() && m_devices.contains(address) && m_devices.value(address).name != trimmed) {
        m_backend->rename(address, trimmed);
    }
}

void Bluetooth::answer(bool accept, const QString &text)
{
    if (m_request.isEmpty()) {
        return;
    }
    const QString kind = m_request.value(QStringLiteral("kind")).toString();
    const QString address = m_request.value(QStringLiteral("address")).toString();
    setRequest({});
    // A code that is only shown has no answer: closing it gives the pairing up.
    if (kind == QLatin1String("show")) {
        if (!accept) {
            m_backend->cancelPairing(address);
        }
        return;
    }
    m_backend->answerPairing(accept, text.trimmed());
}

void Bluetooth::onFinished(const QString &address, const QString &operation, const QString &error)
{
    const QString name = m_devices.value(address).name;
    if (operation == QLatin1String("pair")) {
        setRequest({});
        if (error.isEmpty()) {
            // Trusted, so it connects on its own from now on, like any device paired here.
            m_backend->setTrusted(address, true);
            setActivity(address, QStringLiteral("connecting"));
            m_backend->connectDevice(address);
            return;
        }
        setActivity(address, {});
    } else if (operation == QLatin1String("connect") || operation == QLatin1String("disconnect")) {
        setActivity(address, {});
        // Many devices connect by themselves the moment they are paired.
        if (error.contains(QLatin1String("AlreadyConnected")) || error.contains(QLatin1String("NotConnected"))) {
            return;
        }
    }
    if (!error.isEmpty()) {
        const QString text = Devices::failure(operation, name.isEmpty() ? address : name, error);
        if (!text.isEmpty()) {
            Q_EMIT notice(text);
        }
    }
}
