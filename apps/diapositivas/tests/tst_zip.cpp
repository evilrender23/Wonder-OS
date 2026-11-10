#include "zip.h"

#include <QtTest>

class TestZip : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void crcOfKnownText()
    {
        // The check value every CRC-32 table quotes.
        QCOMPARE(ZipWriter::crc32(QByteArrayLiteral("123456789")), quint32(0xcbf43926));
        QCOMPARE(ZipWriter::crc32(QByteArray()), quint32(0));
    }

    void deflateRoundTrip()
    {
        QByteArray data;
        for (int i = 0; i < 5000; ++i) {
            data += "una línea que se repite ";
        }
        const QByteArray packed = ZipWriter::deflate(data);
        QVERIFY(packed.size() < data.size() / 10);
        QCOMPARE(ZipWriter::inflate(packed), data);
        // Past the limit, nothing.
        QVERIFY(ZipWriter::inflate(packed, 100).isEmpty());
        QVERIFY(ZipWriter::inflate(QByteArrayLiteral("\xff\xff\xff\xff not deflate")).isEmpty());
    }

    void archiveRoundTrip()
    {
        QByteArray noise;
        for (int i = 0; i < 4096; ++i) {
            noise += char((i * 7919) ^ (i >> 3));
        }
        ZipWriter writer;
        writer.add(QStringLiteral("mimetype"), QByteArrayLiteral("application/test"), false);
        writer.add(QStringLiteral("ppt/presentación.xml"), QByteArray(20000, 'x'));
        writer.add(QStringLiteral("empty"), QByteArray());
        writer.add(QStringLiteral("media/noise.bin"), noise);
        const QByteArray bytes = writer.finish();

        const ZipReader reader(bytes);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.names(), QStringList({QStringLiteral("mimetype"), QStringLiteral("ppt/presentación.xml"),
                                             QStringLiteral("empty"), QStringLiteral("media/noise.bin")}));
        QCOMPARE(reader.file(QStringLiteral("mimetype")), QByteArrayLiteral("application/test"));
        QCOMPARE(reader.file(QStringLiteral("ppt/presentación.xml")), QByteArray(20000, 'x'));
        QVERIFY(reader.contains(QStringLiteral("empty")));
        QVERIFY(reader.file(QStringLiteral("empty")).isEmpty());
        QCOMPARE(reader.file(QStringLiteral("media/noise.bin")), noise);
        QVERIFY(!reader.contains(QStringLiteral("absent")));
        QVERIFY(reader.file(QStringLiteral("absent")).isEmpty());
        // The stored entry is there as it is, right after its header: ODF needs that of `mimetype`.
        QCOMPARE(bytes.mid(30, 8), QByteArrayLiteral("mimetype"));
        QCOMPARE(bytes.mid(38, 16), QByteArrayLiteral("application/test"));
        // The deflated one took less room.
        QVERIFY(bytes.size() < 20000);
    }

    void rejectsWhatIsNotAnArchive()
    {
        QVERIFY(!ZipReader(QByteArray()).isValid());
        QVERIFY(!ZipReader(QByteArrayLiteral("not a zip at all, just some text that is long enough")).isValid());
    }

    void damagedEntryIsEmpty()
    {
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray(3000, 'a') + QByteArray(3000, 'b'));
        QByteArray bytes = writer.finish();
        // A byte of the data flipped: the CRC no longer matches.
        bytes[30 + 5 + 4] = char(bytes.at(30 + 5 + 4) ^ 0x55);
        const ZipReader reader(bytes);
        QVERIFY(reader.isValid());
        QVERIFY(reader.file(QStringLiteral("a.txt")).isEmpty());
    }

    void truncatedArchive()
    {
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray(3000, 'a'));
        const QByteArray bytes = writer.finish();
        QVERIFY(!ZipReader(bytes.left(bytes.size() / 2)).isValid());
    }
};

QTEST_GUILESS_MAIN(TestZip)
#include "tst_zip.moc"
