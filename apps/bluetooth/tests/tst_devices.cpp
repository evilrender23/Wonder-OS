#include "devices.h"

#include <QtTest>

class TestDevices : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kindComesFromTheBluezIcon_data()
    {
        QTest::addColumn<QString>("icon");
        QTest::addColumn<QString>("kind");
        QTest::newRow("headset") << "audio-headset" << "headphones";
        QTest::newRow("headphones") << "audio-headphones" << "headphones";
        QTest::newRow("speaker") << "audio-card" << "speaker";
        QTest::newRow("keyboard") << "input-keyboard" << "keyboard";
        QTest::newRow("mouse") << "input-mouse" << "mouse";
        QTest::newRow("gamepad") << "input-gaming" << "gamepad";
        QTest::newRow("phone") << "phone" << "phone";
        QTest::newRow("computer") << "computer" << "computer";
        QTest::newRow("camera") << "camera-photo" << "camera";
        QTest::newRow("unknown") << "preferences-system-bluetooth" << "other";
        QTest::newRow("none") << "" << "other";
    }
    void kindComesFromTheBluezIcon()
    {
        QFETCH(QString, icon);
        QFETCH(QString, kind);
        QCOMPARE(Devices::kindOf(icon), kind);
    }

    void everyKindHasALabelAndASymbol()
    {
        for (const char *kind : {"headphones", "speaker", "keyboard", "mouse", "gamepad", "phone", "computer", "tv", "printer", "camera", "watch", "pen"}) {
            const QString k = QString::fromLatin1(kind);
            QVERIFY2(Devices::kindLabel(k) != QLatin1String("Dispositivo"), kind);
            QVERIFY2(Devices::symbol(k) != QLatin1String("bluetooth"), kind);
            QVERIFY2(QFile::exists(QStringLiteral(SYMBOLS_DIR "/") + Devices::symbol(k) + QStringLiteral(".svg")), kind);
        }
        QCOMPARE(Devices::kindLabel(QStringLiteral("other")), QStringLiteral("Dispositivo"));
        QCOMPARE(Devices::symbol(QStringLiteral("other")), QStringLiteral("bluetooth"));
    }

    void aNameMadeOfTheAddressIsNoName()
    {
        QVERIFY(Devices::isUnnamed(QStringLiteral("4C-87-5D-00-11-22"), QStringLiteral("4C:87:5D:00:11:22")));
        QVERIFY(Devices::isUnnamed(QStringLiteral("4c-87-5d-00-11-22"), QStringLiteral("4C:87:5D:00:11:22")));
        QVERIFY(Devices::isUnnamed(QStringLiteral("  "), QStringLiteral("4C:87:5D:00:11:22")));
        QVERIFY(!Devices::isUnnamed(QStringLiteral("WH-1000XM5"), QStringLiteral("4C:87:5D:00:11:22")));
    }

    void statusSaysWhatTheDeviceIsDoing()
    {
        DeviceInfo device;
        device.kind = QStringLiteral("mouse");
        QCOMPARE(Devices::status(device), QStringLiteral("Ratón"));
        device.paired = true;
        QCOMPARE(Devices::status(device), QStringLiteral("No conectado"));
        device.connected = true;
        QCOMPARE(Devices::status(device), QStringLiteral("Conectado"));
        device.battery = 0;
        QCOMPARE(Devices::status(device), QStringLiteral("Conectado · 0 %"));
        device.battery = 82;
        QCOMPARE(Devices::status(device), QStringLiteral("Conectado · 82 %"));
        device.activity = QStringLiteral("disconnecting");
        QCOMPARE(Devices::status(device), QStringLiteral("Desconectando…"));
        device.activity = QStringLiteral("connecting");
        QCOMPARE(Devices::status(device), QStringLiteral("Conectando…"));
        device.activity = QStringLiteral("pairing");
        QCOMPARE(Devices::status(device), QStringLiteral("Enlazando…"));
        device.activity.clear();
        device.connected = false;
        device.blocked = true;
        QCOMPARE(Devices::status(device), QStringLiteral("Bloqueado"));
    }

    void connectedGoFirstThenByName()
    {
        DeviceInfo a, b;
        a.name = QStringLiteral("zeta");
        a.connected = true;
        b.name = QStringLiteral("Alfa");
        QVERIFY(Devices::before(a, b));
        QVERIFY(!Devices::before(b, a));
        a.connected = false;
        QVERIFY(Devices::before(b, a));
        a.name = QStringLiteral("alfa 10");
        b.name = QStringLiteral("Alfa 9");
        QVERIFY(Devices::before(b, a));
        a.name = b.name;
        a.address = QStringLiteral("01");
        b.address = QStringLiteral("02");
        QVERIFY(Devices::before(a, b));
        QVERIFY(!Devices::before(b, a));
    }

    void passkeyKeepsItsLeadingZeros()
    {
        QCOMPARE(Devices::passkey(123456), QStringLiteral("123 456"));
        QCOMPARE(Devices::passkey(42), QStringLiteral("000 042"));
        QCOMPARE(Devices::passkey(0), QStringLiteral("000 000"));
    }

    void failureIsWordedForThePerson()
    {
        QVERIFY(Devices::failure(QStringLiteral("pair"), QStringLiteral("X"), QStringLiteral("org.bluez.Error.AuthenticationCanceled")).isEmpty());
        const QString connect = Devices::failure(QStringLiteral("connect"), QStringLiteral("Cascos"), QStringLiteral("br-connection-page-timeout"));
        QVERIFY(connect.contains(QStringLiteral("«Cascos»")));
        QVERIFY(connect.contains(QStringLiteral("encendido")));
        QVERIFY(Devices::failure(QStringLiteral("pair"), QStringLiteral("X"), QStringLiteral("org.bluez.Error.AuthenticationRejected")).contains(QStringLiteral("código")));
        QVERIFY(!Devices::failure(QStringLiteral("forget"), QStringLiteral("X"), QStringLiteral("boom")).isEmpty());
        QVERIFY(!Devices::failure(QStringLiteral("power"), {}, QStringLiteral("boom")).isEmpty());
        QVERIFY(!Devices::failure(QStringLiteral("rename"), QStringLiteral("X"), QStringLiteral("boom")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDevices)
#include "tst_devices.moc"
