#include "fakebackend.h"

#include <QTimer>

FakeBackend::FakeBackend(QObject *parent)
    : Backend(parent)
{
}

void FakeBackend::loadSample()
{
    m_adapter = {.present = true, .powered = true, .discoverable = false, .discovering = false, .discoverableTimeout = 180,
                 .name = QStringLiteral("wonderland"), .address = QStringLiteral("AC:F2:3C:00:00:01")};
    const auto add = [this](const char *address, const char *name, const char *kind, bool paired, bool connected, int battery) {
        DeviceInfo device;
        device.address = QString::fromLatin1(address);
        device.name = QString::fromUtf8(name);
        device.kind = QString::fromLatin1(kind);
        device.paired = paired;
        device.trusted = paired;
        device.connected = connected;
        device.battery = battery;
        m_devices.insert(device.address, device);
    };
    add("10:00:00:00:00:01", "AirPods Pro de Alicia", "headphones", true, true, 82);
    add("10:00:00:00:00:02", "MX Master 3S", "mouse", true, true, 55);
    add("10:00:00:00:00:03", "Keychron K2", "keyboard", true, false, -1);
    add("10:00:00:00:00:04", "DualSense Wireless Controller", "gamepad", true, false, -1);
    add("20:00:00:00:00:01", "Galaxy S24", "phone", false, false, -1);
    add("20:00:00:00:00:02", "LG Soundbar", "speaker", false, false, -1);
    add("20:00:00:00:00:03", "Pixel Watch", "watch", false, false, -1);
    add("20:00:00:00:00:04", "20-00-00-00-00-04", "other", false, false, -1);
    Q_EMIT adapterChanged();
    Q_EMIT devicesReset();
}

void FakeBackend::setAdapter(const AdapterInfo &adapter)
{
    const bool other = adapter.present != m_adapter.present || adapter.address != m_adapter.address;
    m_adapter = adapter;
    if (other) {
        m_devices.clear();
        Q_EMIT devicesReset();
    }
    Q_EMIT adapterChanged();
}

void FakeBackend::addDevice(const DeviceInfo &device)
{
    m_devices.insert(device.address, device);
    Q_EMIT deviceChanged(device);
}

void FakeBackend::dropDevice(const QString &address)
{
    if (m_devices.remove(address) > 0) {
        Q_EMIT deviceRemoved(address);
    }
}

void FakeBackend::failNext(const QString &operation, const QString &error)
{
    m_failures.insert(operation, error);
}

void FakeBackend::askCodeFor(const QString &address, quint32 code)
{
    m_codes.insert(address, code);
}

void FakeBackend::request(const PairingRequest &request)
{
    m_pairing = request.address;
    Q_EMIT pairingRequested(request);
}

void FakeBackend::later(const QString &address, const QString &operation, std::function<void(DeviceInfo &)> change)
{
    QTimer::singleShot(0, this, [this, address, operation, change] {
        const QString error = m_failures.take(operation);
        if (!m_devices.contains(address)) {
            Q_EMIT finished(address, operation, QStringLiteral("org.bluez.Error.DoesNotExist"));
            return;
        }
        if (error.isEmpty()) {
            change(m_devices[address]);
            Q_EMIT deviceChanged(m_devices.value(address));
        }
        Q_EMIT finished(address, operation, error);
    });
}

void FakeBackend::setPowered(bool powered)
{
    QTimer::singleShot(0, this, [this, powered] {
        const QString error = m_failures.take(QStringLiteral("power"));
        if (error.isEmpty()) {
            m_adapter.powered = powered;
            if (!powered) {
                m_adapter.discovering = false;
                m_adapter.discoverable = false;
                for (DeviceInfo &device : m_devices) {
                    if (device.connected) {
                        device.connected = false;
                        Q_EMIT deviceChanged(device);
                    }
                }
            }
            Q_EMIT adapterChanged();
        }
        Q_EMIT finished({}, QStringLiteral("power"), error);
    });
}

void FakeBackend::setDiscoverable(bool discoverable)
{
    m_adapter.discoverable = discoverable && m_adapter.powered;
    Q_EMIT adapterChanged();
}

void FakeBackend::setAdapterName(const QString &name)
{
    m_adapter.name = name;
    Q_EMIT adapterChanged();
}

void FakeBackend::setScanning(bool scanning)
{
    const bool discovering = scanning && m_adapter.powered;
    if (discovering != m_adapter.discovering) {
        m_adapter.discovering = discovering;
        Q_EMIT adapterChanged();
    }
}

void FakeBackend::connectDevice(const QString &address)
{
    later(address, QStringLiteral("connect"), [](DeviceInfo &device) { device.connected = true; });
}

void FakeBackend::disconnectDevice(const QString &address)
{
    later(address, QStringLiteral("disconnect"), [](DeviceInfo &device) { device.connected = false; });
}

void FakeBackend::pair(const QString &address)
{
    if (m_codes.contains(address)) {
        QTimer::singleShot(0, this, [this, address] {
            request({QStringLiteral("confirm"), address, m_devices.value(address).name, Devices::passkey(m_codes.value(address))});
        });
        return;
    }
    later(address, QStringLiteral("pair"), [](DeviceInfo &device) { device.paired = true; });
}

void FakeBackend::finishPairing(QString address, bool accepted)
{
    m_pairing.clear();
    Q_EMIT pairingDismissed();
    if (accepted) {
        later(address, QStringLiteral("pair"), [](DeviceInfo &device) { device.paired = true; });
    } else {
        Q_EMIT finished(address, QStringLiteral("pair"), QStringLiteral("org.bluez.Error.AuthenticationCanceled"));
    }
}

void FakeBackend::cancelPairing(const QString &address)
{
    if (m_pairing == address) {
        finishPairing(address, false);
    }
}

void FakeBackend::answerPairing(bool accept, const QString &)
{
    if (const QString address = m_pairing; !address.isEmpty()) {
        finishPairing(address, accept);
    }
}

void FakeBackend::forget(const QString &address)
{
    QTimer::singleShot(0, this, [this, address] {
        const QString error = m_failures.take(QStringLiteral("forget"));
        if (error.isEmpty()) {
            dropDevice(address);
        }
        Q_EMIT finished(address, QStringLiteral("forget"), error);
    });
}

void FakeBackend::setTrusted(const QString &address, bool trusted)
{
    later(address, QStringLiteral("trust"), [trusted](DeviceInfo &device) { device.trusted = trusted; });
}

void FakeBackend::setBlocked(const QString &address, bool blocked)
{
    later(address, QStringLiteral("block"), [blocked](DeviceInfo &device) {
        device.blocked = blocked;
        if (blocked) {
            device.connected = false;
        }
    });
}

void FakeBackend::rename(const QString &address, const QString &name)
{
    later(address, QStringLiteral("rename"), [name](DeviceInfo &device) { device.name = name; });
}

void FakeBackend::simulate(const QString &what)
{
    const QString phone = QStringLiteral("20:00:00:00:00:01");
    const QString keyboard = QStringLiteral("10:00:00:00:00:03");
    if (what == QLatin1String("confirm")) {
        request({what, phone, m_devices.value(phone).name, Devices::passkey(482915)});
    } else if (what == QLatin1String("show")) {
        request({what, keyboard, m_devices.value(keyboard).name, Devices::passkey(73024)});
    } else if (what == QLatin1String("pin") || what == QLatin1String("authorize")) {
        request({what, phone, m_devices.value(phone).name, {}});
    } else if (what == QLatin1String("off")) {
        setPowered(false);
    } else if (what == QLatin1String("none")) {
        setAdapter({});
    } else if (what == QLatin1String("empty")) {
        const QStringList addresses = m_devices.keys();
        for (const QString &address : addresses) {
            dropDevice(address);
        }
    } else if (what == QLatin1String("fail")) {
        failNext(QStringLiteral("connect"), QStringLiteral("br-connection-page-timeout"));
        connectDevice(keyboard);
    }
}
