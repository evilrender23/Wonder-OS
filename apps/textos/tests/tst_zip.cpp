#include "zip.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class TestZip : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        ZipWriter writer;
        const QByteArray text = QByteArray("Hola, mundo. ").repeated(500);
        writer.add(QStringLiteral("mimetype"), "application/test", false);
        writer.add(QStringLiteral("carpeta/texto.txt"), text);
        writer.add(QStringLiteral("vacío.txt"), QByteArray());
        const QByteArray archive = writer.finish();
        // The repeated text must really have been deflated.
        QVERIFY(archive.size() < text.size() / 4);

        const ZipReader reader(archive);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.names(), QStringList({QStringLiteral("mimetype"), QStringLiteral("carpeta/texto.txt"), QStringLiteral("vacío.txt")}));
        QCOMPARE(reader.file(QStringLiteral("mimetype")), QByteArray("application/test"));
        QCOMPARE(reader.file(QStringLiteral("carpeta/texto.txt")), text);
        QVERIFY(!reader.file(QStringLiteral("vacío.txt")).isNull());
        QVERIFY(reader.file(QStringLiteral("vacío.txt")).isEmpty());
        QVERIFY(reader.file(QStringLiteral("no está")).isNull());
        QVERIFY(reader.contains(QStringLiteral("mimetype")));
        QVERIFY(!reader.contains(QStringLiteral("otro")));
    }

    void storedFirstEntryIsPlain()
    {
        // An .odt is recognised by its first bytes: PK header, then «mimetype» and its value unpacked.
        ZipWriter writer;
        writer.add(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text", false);
        const QByteArray archive = writer.finish();
        QCOMPARE(archive.mid(30, 8), QByteArray("mimetype"));
        QCOMPARE(archive.mid(38, 39), QByteArray("application/vnd.oasis.opendocument.text"));
    }

    void binary()
    {
        QByteArray noise;
        quint32 seed = 12345;
        for (int i = 0; i < 70000; ++i) {
            seed = seed * 1664525u + 1013904223u;
            noise.append(char(seed >> 24));
        }
        ZipWriter writer;
        writer.add(QStringLiteral("ruido.bin"), noise);
        const ZipReader reader(writer.finish());
        QCOMPARE(reader.file(QStringLiteral("ruido.bin")), noise);
    }

    void rejectsGarbage()
    {
        QVERIFY(!ZipReader(QByteArray()).isValid());
        QVERIFY(!ZipReader(QByteArray("esto no es un zip, ni se le parece en nada")).isValid());
        // A truncated archive loses its directory.
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray("abc").repeated(100));
        const QByteArray archive = writer.finish();
        QVERIFY(!ZipReader(archive.left(archive.size() - 30)).isValid());
    }

    void detectsCorruption()
    {
        ZipWriter writer;
        const QByteArray text = QByteArray("abcdefghij").repeated(200);
        writer.add(QStringLiteral("a.txt"), text, false);
        QByteArray archive = writer.finish();
        archive[100] = char(archive.at(100) ^ 0x55);
        const ZipReader reader(archive);
        QVERIFY(reader.isValid());
        QVERIFY(reader.file(QStringLiteral("a.txt")).isNull());
    }

    void saveAndReadFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("prueba.zip"));
        ZipWriter writer;
        writer.add(QStringLiteral("uno.txt"), "uno");
        writer.add(QStringLiteral("dos/tres.txt"), QByteArray("tres ").repeated(50));
        QVERIFY(writer.save(path));
        const ZipReader reader = ZipReader::fromFile(path);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.file(QStringLiteral("uno.txt")), QByteArray("uno"));
        QVERIFY(!ZipReader::fromFile(dir.filePath(QStringLiteral("no-existe.zip"))).isValid());
    }

    void readsWhatUnzipToolsWrite()
    {
        // An archive made by another program, when one is at hand.
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (python.isEmpty()) {
            QSKIP("python3 is not installed");
        }
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("fuera.zip"));
        QProcess process;
        process.start(python, {QStringLiteral("-I"), QStringLiteral("-c"),
            QStringLiteral("import sys, zipfile\n"
                           "z = zipfile.ZipFile(sys.argv[1], 'w', zipfile.ZIP_DEFLATED)\n"
                           "z.writestr('hola.txt', 'hola ' * 300)\n"
                           "z.writestr('sub/ñ.txt', 'eñe')\n"
                           "z.close()\n"), path});
        QVERIFY(process.waitForFinished(20000));
        QCOMPARE(process.exitCode(), 0);
        const ZipReader reader = ZipReader::fromFile(path);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.file(QStringLiteral("hola.txt")), QByteArray("hola ").repeated(300));
        QCOMPARE(reader.file(QStringLiteral("sub/ñ.txt")), QByteArray("eñe"));

        // And the other way: what this writes, python can test and read.
        const QString ours = dir.filePath(QStringLiteral("nuestro.zip"));
        ZipWriter writer;
        writer.add(QStringLiteral("mimetype"), "x/y", false);
        writer.add(QStringLiteral("dir/año.txt"), QByteArray("contenido ").repeated(100));
        QVERIFY(writer.save(ours));
        QProcess check;
        check.start(python, {QStringLiteral("-I"), QStringLiteral("-c"),
            QStringLiteral("import sys, zipfile\n"
                           "z = zipfile.ZipFile(sys.argv[1])\n"
                           "assert z.testzip() is None\n"
                           "assert z.read('dir/año.txt') == b'contenido ' * 100\n"
                           "assert z.namelist() == ['mimetype', 'dir/año.txt']\n"), ours});
        QVERIFY(check.waitForFinished(20000));
        QCOMPARE(check.exitCode(), 0);
    }
};

QTEST_GUILESS_MAIN(TestZip)
#include "tst_zip.moc"
