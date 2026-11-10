#include "devices.h"

#include <QCollator>
#include <QCoreApplication>
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
    // Translated when asked for: this table is built before the translator is installed.
    static const QHash<QString, const char *> labels = {
        {QStringLiteral("headphones"), QT_TRANSLATE_NOOP("Devices", "Auriculares")},
        {QStringLiteral("speaker"), QT_TRANSLATE_NOOP("Devices", "Altavoz")},
        {QStringLiteral("keyboard"), QT_TRANSLATE_NOOP("Devices", "Teclado")},
        {QStringLiteral("mouse"), QT_TRANSLATE_NOOP("Devices", "Ratón")},
        {QStringLiteral("gamepad"), QT_TRANSLATE_NOOP("Devices", "Mando")},
        {QStringLiteral("phone"), QT_TRANSLATE_NOOP("Devices", "Teléfono")},
        {QStringLiteral("computer"), QT_TRANSLATE_NOOP("Devices", "Ordenador")},
        {QStringLiteral("tv"), QT_TRANSLATE_NOOP("Devices", "Pantalla")},
        {QStringLiteral("printer"), QT_TRANSLATE_NOOP("Devices", "Impresora")},
        {QStringLiteral("camera"), QT_TRANSLATE_NOOP("Devices", "Cámara")},
        {QStringLiteral("watch"), QT_TRANSLATE_NOOP("Devices", "Reloj")},
        {QStringLiteral("pen"), QT_TRANSLATE_NOOP("Devices", "Tableta gráfica")},
    };
    return QCoreApplication::translate("Devices", labels.value(kind, QT_TRANSLATE_NOOP("Devices", "Dispositivo")));
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
        return QCoreApplication::translate("Devices", "Conectando…");
    }
    if (device.activity == QLatin1String("disconnecting")) {
        return QCoreApplication::translate("Devices", "Desconectando…");
    }
    if (device.activity == QLatin1String("pairing")) {
        return QCoreApplication::translate("Devices", "Enlazando…");
    }
    if (device.blocked) {
        return QCoreApplication::translate("Devices", "Bloqueado");
    }
    if (device.connected) {
        return device.battery >= 0 ? QCoreApplication::translate("Devices", "Conectado · %1 %").arg(device.battery) : QCoreApplication::translate("Devices", "Conectado");
    }
    return device.paired ? QCoreApplication::translate("Devices", "No conectado") : kindLabel(device.kind);
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
        what = QCoreApplication::translate("Devices", "No se ha podido conectar con «%1».").arg(name);
    } else if (operation == QLatin1String("disconnect")) {
        what = QCoreApplication::translate("Devices", "No se ha podido desconectar «%1».").arg(name);
    } else if (operation == QLatin1String("pair")) {
        what = QCoreApplication::translate("Devices", "No se ha podido enlazar con «%1».").arg(name);
    } else if (operation == QLatin1String("forget")) {
        what = QCoreApplication::translate("Devices", "No se ha podido olvidar «%1».").arg(name);
    } else if (operation == QLatin1String("power")) {
        return QCoreApplication::translate("Devices", "No se ha podido cambiar el estado del Bluetooth.");
    } else {
        return QCoreApplication::translate("Devices", "No se ha podido aplicar el cambio.");
    }
    if (lower.contains(QLatin1String("rejected")) || lower.contains(QLatin1String("authentication"))) {
        return what + QCoreApplication::translate("Devices", " El dispositivo no ha aceptado el código.");
    }
    if (operation == QLatin1String("connect") || operation == QLatin1String("pair")) {
        return what + QCoreApplication::translate("Devices", " Comprueba que está encendido, cerca y en modo de enlace.");
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
