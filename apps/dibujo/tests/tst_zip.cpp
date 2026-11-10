#include "zip.h"

#include <QtTest>

class TestZip : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void crc()
    {
        QCOMPARE(Zip::crc32(QByteArrayLiteral("123456789")), quint32(0xCBF43926));
        QCOMPARE(Zip::crc32(QByteArray()), quint32(0));
    }
    void deflateRoundTrip()
    {
        const QByteArray data = QByteArray("maquetar ").repeated(500);
        const QByteArray packed = Zip::deflateRaw(data);
        QVERIFY(packed.size() < data.size() / 4);
        QByteArray back;
        QVERIFY(Zip::inflateRaw(packed, data.size(), &back));
        QCOMPARE(back, data);
        // More than was promised is refused.
        QVERIFY(!Zip::inflateRaw(packed, data.size() - 1, &back));
    }
    void roundTrip()
    {
        QByteArray binary;
        for (int i = 0; i < 70000; ++i) {
            binary.append(char((i * 31) ^ (i >> 3)));
        }
        const QList<Zip::Entry> entries{{QStringLiteral("mimetype"), QByteArrayLiteral("application/x-test"), true},
                                        {QStringLiteral("carpeta/ñandú.json"), QByteArray("{\"a\":1}").repeated(100), false},
                                        {QStringLiteral("vacío"), QByteArray(), false},
                                        {QStringLiteral("bin"), binary, false}};
        const QByteArray zip = Zip::write(entries);
        // The first entry is stored: its name and bytes are readable as they are.
        QCOMPARE(zip.mid(0, 4), QByteArray("PK\x03\x04"));
        QCOMPARE(zip.mid(30, 8), QByteArray("mimetype"));
        QCOMPARE(zip.mid(38, 18), QByteArray("application/x-test"));
        QList<Zip::Entry> back;
        QString error;
        QVERIFY2(Zip::read(zip, &back, &error), qPrintable(error));
        QCOMPARE(back.size(), entries.size());
        for (int i = 0; i < entries.size(); ++i) {
            QCOMPARE(back.at(i).name, entries.at(i).name);
            QCOMPARE(back.at(i).data, entries.at(i).data);
        }
    }
    void refusesRubbish()
    {
        QList<Zip::Entry> entries;
        QString error;
        QVERIFY(!Zip::read(QByteArray(), &entries, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Zip::read(QByteArray("esto no es un zip, ni de lejos").repeated(10), &entries));
        const QByteArray zip = Zip::write({{QStringLiteral("a.txt"), QByteArray("hola mundo").repeated(50), false}});
        QVERIFY(!Zip::read(zip.left(zip.size() / 2), &entries));
        // A flipped byte in the data fails the checksum.
        QByteArray broken = zip;
        broken[40] = char(broken.at(40) ^ 0x55);
        QVERIFY(!Zip::read(broken, &entries));
    }
};

QTEST_GUILESS_MAIN(TestZip)
#include "tst_zip.moc"
