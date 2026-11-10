#include "bluetooth.h"
#include "fakebackend.h"

#include <QSignalSpy>
#include <QtTest>

static const QString airpods = QStringLiteral("10:00:00:00:00:01");
static const QString keyboard = QStringLiteral("10:00:00:00:00:03");
static const QString phone = QStringLiteral("20:00:00:00:00:01");
static const QString nameless = QStringLiteral("20:00:00:00:00:04");

static QStringList names(const DeviceModel *model)
{
    QStringList out;
    for (const DeviceInfo &device : model->devices()) {
        out << device.name;
    }
    return out;
}

class TestBluetooth : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void splitsYoursFromTheOnesInRange()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        QVERIFY(bt.present());
        QVERIFY(bt.powered());
        QCOMPARE(bt.name(), QStringLiteral("wonderland"));
        QCOMPARE(names(bt.mine()), QStringList({"AirPods Pro de Alicia", "MX Master 3S", "DualSense Wireless Controller", "Keychron K2"}));
        // The one BlueZ only knows by its address is left out.
        QCOMPARE(names(bt.nearby()), QStringList({"Galaxy S24", "LG Soundbar", "Pixel Watch"}));
        QVERIFY(bt.device(nameless).value(QStringLiteral("name")).toString().startsWith(QStringLiteral("20-00")));
        QVERIFY(bt.device(QStringLiteral("nobody")).isEmpty());
    }

    void connectsAndDisconnectsAPairedDevice()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);

        bt.connectTo(keyboard);
        QCOMPARE(bt.device(keyboard).value(QStringLiteral("status")).toString(), QStringLiteral("Conectando…"));
        QVERIFY(bt.device(keyboard).value(QStringLiteral("busy")).toBool());
        QTRY_VERIFY(bt.device(keyboard).value(QStringLiteral("connected")).toBool());
        QTRY_VERIFY(!bt.device(keyboard).value(QStringLiteral("busy")).toBool());
        QCOMPARE(bt.mine()->indexOf(keyboard), 1);

        bt.disconnectFrom(keyboard);
        QCOMPARE(bt.device(keyboard).value(QStringLiteral("status")).toString(), QStringLiteral("Desconectando…"));
        QTRY_COMPARE(bt.device(keyboard).value(QStringLiteral("status")).toString(), QStringLiteral("No conectado"));
    }

    void aNewDeviceIsPairedTrustedAndConnected()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);

        bt.connectTo(phone);
        QCOMPARE(bt.device(phone).value(QStringLiteral("status")).toString(), QStringLiteral("Enlazando…"));
        QTRY_VERIFY(bt.device(phone).value(QStringLiteral("connected")).toBool());
        QTRY_VERIFY(!bt.device(phone).value(QStringLiteral("busy")).toBool());
        QVERIFY(bt.device(phone).value(QStringLiteral("paired")).toBool());
        QVERIFY(bt.device(phone).value(QStringLiteral("trusted")).toBool());
        QVERIFY(bt.mine()->contains(phone));
        QVERIFY(!bt.nearby()->contains(phone));
    }

    void pairingWaitsForTheCodeToBeConfirmed()
    {
        FakeBackend backend;
        backend.loadSample();
        backend.askCodeFor(phone, 42);
        Bluetooth bt(&backend);
        QSignalSpy notices(&bt, &Bluetooth::notice);

        bt.connectTo(phone);
        QTRY_COMPARE(bt.request().value(QStringLiteral("kind")).toString(), QStringLiteral("confirm"));
        QCOMPARE(bt.request().value(QStringLiteral("code")).toString(), QStringLiteral("000 042"));
        QCOMPARE(bt.request().value(QStringLiteral("name")).toString(), QStringLiteral("Galaxy S24"));
        QVERIFY(!bt.device(phone).value(QStringLiteral("paired")).toBool());

        bt.answer(true);
        QVERIFY(bt.request().isEmpty());
        QTRY_VERIFY(bt.device(phone).value(QStringLiteral("connected")).toBool());
        QCOMPARE(notices.count(), 0);
    }

    void refusingTheCodeLeavesTheDeviceAsItWasAndSaysNothing()
    {
        FakeBackend backend;
        backend.loadSample();
        backend.askCodeFor(phone, 123456);
        Bluetooth bt(&backend);
        QSignalSpy notices(&bt, &Bluetooth::notice);

        bt.connectTo(phone);
        QTRY_VERIFY(!bt.request().isEmpty());
        bt.answer(false);
        QVERIFY(bt.request().isEmpty());
        QTRY_VERIFY(!bt.device(phone).value(QStringLiteral("busy")).toBool());
        QVERIFY(!bt.device(phone).value(QStringLiteral("paired")).toBool());
        QVERIFY(bt.nearby()->contains(phone));
        QCOMPARE(notices.count(), 0);
    }

    void aCodeThatIsOnlyShownCancelsThePairingWhenClosed()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        QSignalSpy finished(&backend, &Backend::finished);

        backend.request({QStringLiteral("show"), keyboard, QStringLiteral("Keychron K2"), QStringLiteral("123 456")});
        QCOMPARE(bt.request().value(QStringLiteral("kind")).toString(), QStringLiteral("show"));
        bt.answer(false);
        QVERIFY(bt.request().isEmpty());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("pair"));
    }

    void aFailedConnectionIsToldAndTheDeviceIsFreeAgain()
    {
        FakeBackend backend;
        backend.loadSample();
        backend.failNext(QStringLiteral("connect"), QStringLiteral("br-connection-page-timeout"));
        Bluetooth bt(&backend);
        QSignalSpy notices(&bt, &Bluetooth::notice);

        bt.connectTo(keyboard);
        QTRY_COMPARE(notices.count(), 1);
        QVERIFY(notices.first().first().toString().contains(QStringLiteral("«Keychron K2»")));
        QVERIFY(!bt.device(keyboard).value(QStringLiteral("busy")).toBool());
        QVERIFY(!bt.device(keyboard).value(QStringLiteral("connected")).toBool());

        // Paired devices often connect on their own: that is not a failure.
        backend.failNext(QStringLiteral("connect"), QStringLiteral("org.bluez.Error.AlreadyConnected"));
        bt.connectTo(keyboard);
        QTRY_VERIFY(!bt.device(keyboard).value(QStringLiteral("busy")).toBool());
        QCOMPARE(notices.count(), 1);
    }

    void aBusyDeviceIgnoresASecondOrder()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        QSignalSpy finished(&backend, &Backend::finished);
        bt.connectTo(keyboard);
        bt.connectTo(keyboard);
        bt.disconnectFrom(keyboard);
        QTRY_VERIFY(!bt.device(keyboard).value(QStringLiteral("busy")).toBool());
        QTest::qWait(20);
        QCOMPARE(finished.count(), 1);
        QVERIFY(bt.device(keyboard).value(QStringLiteral("connected")).toBool());
    }

    void forgetsTrustsBlocksAndRenames()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);

        bt.setTrusted(keyboard, false);
        QTRY_VERIFY(!bt.device(keyboard).value(QStringLiteral("trusted")).toBool());

        bt.renameDevice(keyboard, QStringLiteral("  Teclado del salón "));
        QTRY_COMPARE(bt.device(keyboard).value(QStringLiteral("name")).toString(), QStringLiteral("Teclado del salón"));
        bt.renameDevice(keyboard, QStringLiteral("   "));
        QTest::qWait(10);
        QCOMPARE(bt.device(keyboard).value(QStringLiteral("name")).toString(), QStringLiteral("Teclado del salón"));

        bt.setBlocked(airpods, true);
        QTRY_COMPARE(bt.device(airpods).value(QStringLiteral("status")).toString(), QStringLiteral("Bloqueado"));
        QVERIFY(bt.mine()->contains(airpods));

        // Blocked but never paired: still listed, or it could never be unblocked.
        bt.setBlocked(phone, true);
        QTRY_VERIFY(bt.mine()->contains(phone));
        QVERIFY(!bt.nearby()->contains(phone));
        bt.setBlocked(phone, false);
        QTRY_VERIFY(bt.nearby()->contains(phone));

        bt.forget(keyboard);
        QTRY_VERIFY(!bt.mine()->contains(keyboard));
        QVERIFY(bt.device(keyboard).isEmpty());
    }

    void turningItOffHidesWhatWasInRangeAndStopsLooking()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        bt.setWatching(true);
        QVERIFY(bt.scanning());

        bt.setPowered(false);
        QTRY_VERIFY(!bt.powered());
        QVERIFY(!bt.scanning());
        QCOMPARE(bt.nearby()->count(), 0);
        QCOMPARE(bt.mine()->count(), 4);
        QVERIFY(!bt.device(airpods).value(QStringLiteral("connected")).toBool());

        bt.setPowered(true);
        QTRY_VERIFY(bt.powered());
        // The window is still open: it looks again without being asked.
        QVERIFY(bt.scanning());
        QCOMPARE(bt.nearby()->count(), 3);

        bt.setWatching(false);
        QVERIFY(!bt.scanning());
    }

    void adapterNameAndVisibility()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        QSignalSpy changed(&bt, &Bluetooth::adapterChanged);

        bt.rename(QStringLiteral(" madriguera "));
        QCOMPARE(bt.name(), QStringLiteral("madriguera"));
        bt.rename(QString());
        QCOMPARE(bt.name(), QStringLiteral("madriguera"));
        QCOMPARE(changed.count(), 1);

        bt.setDiscoverable(true);
        QVERIFY(bt.discoverable());
        QCOMPARE(bt.discoverableMinutes(), 3);
    }

    void devicesComeAndGoWhileTheWindowIsOpen()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        const int before = bt.revision();

        DeviceInfo tv;
        tv.address = QStringLiteral("30:00:00:00:00:01");
        tv.name = QStringLiteral("30-00-00-00-00-01");
        backend.addDevice(tv);
        QVERIFY(!bt.nearby()->contains(tv.address));

        // Its name arrives a moment after the device does.
        tv.name = QStringLiteral("Tele del salón");
        tv.kind = QStringLiteral("tv");
        backend.addDevice(tv);
        QVERIFY(bt.nearby()->contains(tv.address));
        QVERIFY(bt.revision() > before);

        backend.dropDevice(tv.address);
        QVERIFY(!bt.nearby()->contains(tv.address));
    }

    void anotherAdapterStartsFromItsOwnDevices()
    {
        FakeBackend backend;
        backend.loadSample();
        Bluetooth bt(&backend);
        backend.setAdapter({});
        QVERIFY(!bt.present());
        QCOMPARE(bt.mine()->count(), 0);
        QCOMPARE(bt.nearby()->count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestBluetooth)
#include "tst_bluetooth.moc"
