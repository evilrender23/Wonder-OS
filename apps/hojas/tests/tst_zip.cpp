#include "zip.h"

#include <QtTest>

class TestZip : public QObject
{
    Q_OBJECT
private slots:
    void crc()
    {
        // The check value of CRC-32.
        QCOMPARE(Zip::crc32(QByteArrayLiteral("123456789")), quint32(0xCBF43926));
        QCOMPARE(Zip::crc32(QByteArray()), quint32(0));
    }

    void deflateRoundTrip()
    {
        QByteArray data;
        for (int i = 0; i < 5000; ++i) {
            data += "row " + QByteArray::number(i % 37) + ";";
        }
        const QByteArray packed = Zip::deflateRaw(data);
        QVERIFY(packed.size() < data.size() / 4);
        QCOMPARE(Zip::inflateRaw(packed, data.size()), data);
    }

    void roundTrip()
    {
        const QByteArray big(100000, 'x');
        QByteArray noise;
        for (int i = 0; i < 4096; ++i) {
            noise += char((i * 2654435761u) >> 13);
        }
        ZipWriter writer;
        writer.add(QStringLiteral("mimetype"), QByteArrayLiteral("application/test"), true);
        writer.add(QStringLiteral("xl/hoja ñ.xml"), big);
        writer.add(QStringLiteral("empty"), QByteArray());
        writer.add(QStringLiteral("noise.bin"), noise);
        const QByteArray archive = writer.finish();
        QVERIFY(archive.size() < 20000);
        // The first entry is stored, so its bytes sit in the open after the local header.
        QCOMPARE(archive.mid(30, 8), QByteArrayLiteral("mimetype"));
        QCOMPARE(archive.mid(38, 16), QByteArrayLiteral("application/test"));

        const ZipReader reader(archive);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.names(), QStringList({QStringLiteral("mimetype"), QStringLiteral("xl/hoja ñ.xml"), QStringLiteral("empty"), QStringLiteral("noise.bin")}));
        QCOMPARE(reader.file(QStringLiteral("xl/hoja ñ.xml")), big);
        QCOMPARE(reader.file(QStringLiteral("noise.bin")), noise);
        QCOMPARE(reader.file(QStringLiteral("empty")).size(), 0);
        QVERIFY(!reader.file(QStringLiteral("empty")).isNull());
        QVERIFY(reader.file(QStringLiteral("absent")).isNull());
        QVERIFY(!reader.contains(QStringLiteral("absent")));
    }

    void broken()
    {
        QVERIFY(!ZipReader(QByteArray()).isValid());
        QVERIFY(!ZipReader(QByteArrayLiteral("not a zip at all, just some text")).isValid());
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray(5000, 'a'));
        QByteArray archive = writer.finish();
        // A flipped byte in the data fails the CRC.
        archive[40] = char(archive.at(40) ^ 0x55);
        const ZipReader reader(archive);
        QVERIFY(reader.file(QStringLiteral("a.txt")).isNull());
        // Cut short, there is no directory to read.
        QVERIFY(!ZipReader(archive.left(archive.size() / 2)).isValid());
    }

    void systemUnzipReads()
    {
        // What it writes, the system's own tools read; and the other way round.
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) {
            QSKIP("python3 not found");
        }
        QTemporaryDir dir;
        ZipWriter writer;
        writer.add(QStringLiteral("a/b.txt"), QByteArray(3000, 'z') + "fin");
        writer.add(QStringLiteral("c.txt"), QByteArrayLiteral("hola"));
        QFile out(dir.filePath(QStringLiteral("ours.zip")));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(writer.finish());
        out.close();
        const QString script = QStringLiteral(
            "import sys,zipfile\n"
            "z=zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None\n"
            "assert z.read('a/b.txt')==b'z'*3000+b'fin' and z.read('c.txt')==b'hola'\n"
            "w=zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED)\n"
            "w.writestr('uno.txt','1'*4000); w.writestr(zipfile.ZipInfo('dos.txt'),'dos'); w.close()\n");
        QProcess process;
        process.start(python, {QStringLiteral("-c"), script, out.fileName(), dir.filePath(QStringLiteral("theirs.zip"))});
        QVERIFY(process.waitForFinished(20000));
        QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        QFile in(dir.filePath(QStringLiteral("theirs.zip")));
        QVERIFY(in.open(QIODevice::ReadOnly));
        const ZipReader reader(in.readAll());
        QVERIFY(reader.isValid());
        QCOMPARE(reader.file(QStringLiteral("uno.txt")), QByteArray(4000, '1'));
        QCOMPARE(reader.file(QStringLiteral("dos.txt")), QByteArrayLiteral("dos"));
    }
};

QTEST_GUILESS_MAIN(TestZip)
#include "tst_zip.moc"
