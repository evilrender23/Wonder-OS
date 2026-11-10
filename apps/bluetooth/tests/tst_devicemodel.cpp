#include "devicemodel.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

static DeviceInfo device(const char *address, const char *name, bool connected = false)
{
    DeviceInfo d;
    d.address = QString::fromLatin1(address);
    d.name = QString::fromUtf8(name);
    d.connected = connected;
    return d;
}

static QStringList names(const DeviceModel &model)
{
    QStringList out;
    for (int row = 0; row < model.rowCount(); ++row) {
        out << model.data(model.index(row), Qt::UserRole).toMap().value(QStringLiteral("name")).toString();
    }
    return out;
}

class TestDeviceModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keepsItsOrderThroughEveryChange()
    {
        DeviceModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy counted(&model, &DeviceModel::countChanged);

        model.upsert(device("1", "Mouse"));
        model.upsert(device("2", "Auriculares"));
        model.upsert(device("3", "Teclado"));
        QCOMPARE(names(model), QStringList({"Auriculares", "Mouse", "Teclado"}));
        QCOMPARE(counted.count(), 3);

        model.upsert(device("3", "Teclado", true));
        QCOMPARE(names(model), QStringList({"Teclado", "Auriculares", "Mouse"}));
        QVERIFY(model.data(model.index(0), Qt::UserRole).toMap().value(QStringLiteral("connected")).toBool());

        model.upsert(device("2", "Auriculares", true));
        QCOMPARE(names(model), QStringList({"Auriculares", "Teclado", "Mouse"}));

        model.upsert(device("2", "Auriculares"));
        QCOMPARE(names(model), QStringList({"Teclado", "Auriculares", "Mouse"}));
        model.upsert(device("3", "Teclado"));
        QCOMPARE(names(model), QStringList({"Auriculares", "Mouse", "Teclado"}));

        model.upsert(device("1", "Zapatófono"));
        QCOMPARE(names(model), QStringList({"Auriculares", "Teclado", "Zapatófono"}));
        QCOMPARE(counted.count(), 3);
        QCOMPARE(model.count(), 3);
    }

    void aChangeInPlaceTellsOnlyThatRow()
    {
        DeviceModel model;
        model.upsert(device("1", "A"));
        model.upsert(device("2", "B"));
        QSignalSpy changed(&model, &DeviceModel::dataChanged);
        QSignalSpy moved(&model, &DeviceModel::rowsMoved);

        DeviceInfo b = device("2", "B");
        b.battery = 40;
        model.upsert(b);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toModelIndex().row(), 1);
        QCOMPARE(moved.count(), 0);

        model.upsert(b);
        QCOMPARE(changed.count(), 1);
    }

    void removesAndClears()
    {
        DeviceModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.upsert(device("1", "A"));
        model.upsert(device("2", "B"));
        QVERIFY(model.contains(QStringLiteral("2")));
        model.remove(QStringLiteral("1"));
        model.remove(QStringLiteral("nobody"));
        QCOMPARE(names(model), QStringList({"B"}));
        QVERIFY(!model.contains(QStringLiteral("1")));
        model.clear();
        QCOMPARE(model.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestDeviceModel)
#include "tst_devicemodel.moc"
