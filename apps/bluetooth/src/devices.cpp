#include "devices.h"

#include <QCollator>
#include <QLocale>

namespace Devices {

QString kindOf(const QString &icon)
{
    static const QList<QPair<QLatin1String, QLatin1String>> kinds = {
        {QLatin1String("audio-headset"), QLatin1String("headphones")},
        {QLatin1String("audio-headphones"), QLatin1String("headphones")},
        {QLatin1String("audio-card"), QLatin1String("speaker")},
        {QLatin1String("audio-speakers"), QLatin1String("speaker")},
        {QLatin1String("multimedia-player"), QLatin1String("speaker")},
        {QLatin1String("input-keyboard"), QLatin1String("keyboard")},
        {QLatin1String("input-mouse"), QLatin1String("mouse")},
        {QLatin1String("input-gaming"), QLatin1String("gamepad")},
        {QLatin1String("input-tablet"), QLatin1String("pen")},
        {QLatin1String("phone"), QLatin1String("phone")},
        {QLatin1String("modem"), QLatin1String("phone")},
        {QLatin1String("computer"), QLatin1String("computer")},
        {QLatin1String("video-display"), QLatin1String("tv")},
        {QLatin1String("printer"), QLatin1String("printer")},
        {QLatin1String("scanner"), QLatin1String("printer")},
        {QLatin1String("camera"), QLatin1String("camera")},
        {QLatin1String("watch"), QLatin1String("watch")},
    };
    for (const auto &[prefix, kind] : kinds) {
        if (icon.startsWith(prefix)) {
            return kind;
        }
    }
    return QStringLiteral("other");
}

QString kindLabel(const QString &kind)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("headphones"), QStringLiteral("Auriculares")},
        {QStringLiteral("speaker"), QStringLiteral("Altavoz")},
        {QStringLiteral("keyboard"), QStringLiteral("Teclado")},
        {QStringLiteral("mouse"), QStringLiteral("Ratón")},
        {QStringLiteral("gamepad"), QStringLiteral("Mando")},
        {QStringLiteral("phone"), QStringLiteral("Teléfono")},
        {QStringLiteral("computer"), QStringLiteral("Ordenador")},
        {QStringLiteral("tv"), QStringLiteral("Pantalla")},
        {QStringLiteral("printer"), QStringLiteral("Impresora")},
        {QStringLiteral("camera"), QStringLiteral("Cámara")},
        {QStringLiteral("watch"), QStringLiteral("Reloj")},
        {QStringLiteral("pen"), QStringLiteral("Tableta gráfica")},
    };
    return labels.value(kind, QStringLiteral("Dispositivo"));
}

QString symbol(const QString &kind)
{
    static const QHash<QString, QString> symbols = {
        {QStringLiteral("headphones"), QStringLiteral("headphones")},
        {QStringLiteral("speaker"), QStringLiteral("speaker")},
        {QStringLiteral("keyboard"), QStringLiteral("keyboard")},
        {QStringLiteral("mouse"), QStringLiteral("mouse")},
        {QStringLiteral("gamepad"), QStringLiteral("gamepad-2")},
        {QStringLiteral("phone"), QStringLiteral("smartphone")},
        {QStringLiteral("computer"), QStringLiteral("laptop")},
        {QStringLiteral("tv"), QStringLiteral("tv")},
        {QStringLiteral("printer"), QStringLiteral("printer")},
        {QStringLiteral("camera"), QStringLiteral("camera")},
        {QStringLiteral("watch"), QStringLiteral("watch")},
        {QStringLiteral("pen"), QStringLiteral("pen-tool")},
    };
    return symbols.value(kind, QStringLiteral("bluetooth"));
}

bool isUnnamed(const QString &name, const QString &address)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() || QString(trimmed).replace(QLatin1Char('-'), QLatin1Char(':')).compare(address, Qt::CaseInsensitive) == 0;
}

QString status(const DeviceInfo &device)
{
    if (device.activity == QLatin1String("connecting")) {
        return QStringLiteral("Conectando…");
    }
    if (device.activity == QLatin1String("disconnecting")) {
        return QStringLiteral("Desconectando…");
    }
    if (device.activity == QLatin1String("pairing")) {
        return QStringLiteral("Enlazando…");
    }
    if (device.blocked) {
        return QStringLiteral("Bloqueado");
    }
    if (device.connected) {
        return device.battery >= 0 ? QStringLiteral("Conectado · %1 %").arg(device.battery) : QStringLiteral("Conectado");
    }
    return device.paired ? QStringLiteral("No conectado") : kindLabel(device.kind);
}

bool before(const DeviceInfo &a, const DeviceInfo &b)
{
    if (a.connected != b.connected) {
        return a.connected;
    }
    static const QCollator collator = [] {
        // The C locale compares bytes: no case folding, and «9» after «10».
        QCollator c(QLocale().language() == QLocale::C ? QLocale(QLocale::Spanish, QLocale::Spain) : QLocale());
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    const int order = collator.compare(a.name, b.name);
    return order != 0 ? order < 0 : a.address < b.address;
}

QString passkey(quint32 code)
{
    const QString digits = QStringLiteral("%1").arg(code % 1000000, 6, 10, QLatin1Char('0'));
    return digits.left(3) + QLatin1Char(' ') + digits.mid(3);
}

QString failure(const QString &operation, const QString &name, const QString &detail)
{
    const QString lower = detail.toLower();
    if (lower.contains(QLatin1String("cancel"))) {
        return {};
    }
    QString what;
    if (operation == QLatin1String("connect")) {
        what = QStringLiteral("No se ha podido conectar con «%1».").arg(name);
    } else if (operation == QLatin1String("disconnect")) {
        what = QStringLiteral("No se ha podido desconectar «%1».").arg(name);
    } else if (operation == QLatin1String("pair")) {
        what = QStringLiteral("No se ha podido enlazar con «%1».").arg(name);
    } else if (operation == QLatin1String("forget")) {
        what = QStringLiteral("No se ha podido olvidar «%1».").arg(name);
    } else if (operation == QLatin1String("power")) {
        return QStringLiteral("No se ha podido cambiar el estado del Bluetooth.");
    } else {
        return QStringLiteral("No se ha podido aplicar el cambio.");
    }
    if (lower.contains(QLatin1String("rejected")) || lower.contains(QLatin1String("authentication"))) {
        return what + QStringLiteral(" El dispositivo no ha aceptado el código.");
    }
    if (operation == QLatin1String("connect") || operation == QLatin1String("pair")) {
        return what + QStringLiteral(" Comprueba que está encendido, cerca y en modo de enlace.");
    }
    return what;
}

QVariantMap toMap(const DeviceInfo &device)
{
    return {
        {QStringLiteral("address"), device.address},
        {QStringLiteral("name"), device.name},
        {QStringLiteral("kind"), device.kind},
        {QStringLiteral("kindLabel"), kindLabel(device.kind)},
        {QStringLiteral("symbol"), symbol(device.kind)},
        {QStringLiteral("status"), status(device)},
        {QStringLiteral("busy"), !device.activity.isEmpty()},
        {QStringLiteral("paired"), device.paired},
        {QStringLiteral("trusted"), device.trusted},
        {QStringLiteral("blocked"), device.blocked},
        {QStringLiteral("connected"), device.connected},
        {QStringLiteral("battery"), device.battery},
    };
}

}
