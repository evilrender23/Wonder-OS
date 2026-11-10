#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

/// A Bluetooth device as the window shows it.
struct DeviceInfo {
    QString address;
    QString name;
    /// headphones, speaker, keyboard, mouse, gamepad, phone, computer, tv, printer, camera, watch, pen or other.
    QString kind = QStringLiteral("other");
    /// What is being done to it right now: connecting, disconnecting, pairing, or nothing.
    QString activity;
    bool paired = false;
    bool trusted = false;
    bool blocked = false;
    bool connected = false;
    /// Percent, or -1 when the device does not report one.
    int battery = -1;

    bool operator==(const DeviceInfo &other) const = default;
};

struct AdapterInfo {
    bool present = false;
    bool powered = false;
    bool discoverable = false;
    bool discovering = false;
    /// Seconds after which the adapter stops being discoverable on its own; 0 is never.
    int discoverableTimeout = 0;
    QString name;
    QString address;

    bool operator==(const AdapterInfo &other) const = default;
};

/// What a device asks of the person while it pairs.
struct PairingRequest {
    /// confirm (does this code match?), show (type this code on the device), pin, passkey
    /// (type the device's code here), authorize (let it pair?), or empty for none.
    QString kind;
    QString address;
    QString name;
    QString code;
};

namespace Devices {

/// The kind of a device from the icon name BlueZ gives it (audio-headset, input-mouse…).
QString kindOf(const QString &icon);
QString kindLabel(const QString &kind);
QString symbol(const QString &kind);

/// BlueZ names a device it knows no name for after its address, with dashes.
bool isUnnamed(const QString &name, const QString &address);

/// «Conectado · 82 %», «No conectado», «Conectando…»; for a device not yet paired, its kind.
QString status(const DeviceInfo &device);

/// The order of a list: connected first, then by name without regard to case or accents.
bool before(const DeviceInfo &a, const DeviceInfo &b);

/// A six-digit pairing code as devices show it: «012 345».
QString passkey(quint32 code);

/// Why an operation (connect, disconnect, pair, forget, rename…) failed, for the person.
/// Empty when the failure is their own doing (they cancelled).
QString failure(const QString &operation, const QString &name, const QString &detail);

QVariantMap toMap(const DeviceInfo &device);

}
