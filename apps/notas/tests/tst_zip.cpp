#include "zip.h"

#include <QTemporaryDir>
#include <QTest>

class TestZip : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void roundTrip()
    {
        ZipWriter writer;
        const QByteArray text = QByteArray("Notas de la reunión. ").repeated(400);
        QByteArray noise;
        for (int i = 0; i < 300; ++i) {
            noise.append(char((i * 7919) % 251));
        }
        writer.add(QStringLiteral("mimetype"), "application/x-wonderland-notas");
        writer.add(QStringLiteral("páginas/año.json"), text);
        writer.add(QStringLiteral("files/ruido.bin"), noise);
        writer.add(QStringLiteral("vacío.txt"), QByteArray());
        const QByteArray archive = writer.finish();
        // Deflate did something.
        QVERIFY(archive.size() < text.size());

        const ZipReader reader(archive);
        QVERIFY(reader.valid());
        QCOMPARE(reader.names(), QStringList({QStringLiteral("mimetype"), QStringLiteral("páginas/año.json"), QStringLiteral("files/ruido.bin"), QStringLiteral("vacío.txt")}));
        bool ok = false;
        QCOMPARE(reader.file(QStringLiteral("páginas/año.json"), &ok), text);
        QVERIFY(ok);
        QCOMPARE(reader.file(QStringLiteral("files/ruido.bin"), &ok), noise);
        QVERIFY(ok);
        QCOMPARE(reader.file(QStringLiteral("vacío.txt"), &ok), QByteArray());
        QVERIFY(ok);
        reader.file(QStringLiteral("no-está"), &ok);
        QVERIFY(!ok);
    }

    void firstMemberIsStored()
    {
        // The kind of file can be read at a fixed place, as in ODF and EPUB.
        ZipWriter writer;
        writer.add(QStringLiteral("mimetype"), "application/x-wonderland-notas");
        const QByteArray archive = writer.finish();
        QCOMPARE(archive.mid(30, 8), QByteArray("mimetype"));
        QCOMPARE(archive.mid(38, 30), QByteArray("application/x-wonderland-notas"));
    }

    void damage()
    {
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray("hola hola hola hola hola hola hola hola hola hola").repeated(20));
        QByteArray archive = writer.finish();
        QVERIFY(!ZipReader(QByteArray("esto no es un zip")).valid());
        QVERIFY(!ZipReader(QByteArray()).valid());
        QVERIFY(!ZipReader(archive.left(archive.size() / 2)).valid());
        // A flipped byte in the data: the CRC notices.
        archive[40] = char(archive.at(40) ^ 0x55);
        bool ok = true;
        ZipReader(archive).file(QStringLiteral("a.txt"), &ok);
        QVERIFY(!ok);
    }

    void files()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.zip"));
        ZipWriter writer;
        writer.add(QStringLiteral("uno"), "1");
        QVERIFY(writer.save(path));
        const ZipReader reader = ZipReader::open(path);
        QVERIFY(reader.valid());
        QCOMPARE(reader.file(QStringLiteral("uno")), QByteArray("1"));
        QVERIFY(!ZipReader::open(dir.filePath(QStringLiteral("nada.zip"))).valid());
    }

    void names()
    {
        QVERIFY(ZipReader::safeName(QStringLiteral("pages/a/files/b.png")));
        QVERIFY(!ZipReader::safeName(QStringLiteral("../fuera")));
        QVERIFY(!ZipReader::safeName(QStringLiteral("a/../../b")));
        QVERIFY(!ZipReader::safeName(QStringLiteral("/etc/passwd")));
        QVERIFY(!ZipReader::safeName(QStringLiteral("a\\b")));
        QVERIFY(!ZipReader::safeName(QString()));
    }
};

QTEST_GUILESS_MAIN(TestZip)
#include "tst_zip.moc"
