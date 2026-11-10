#include "io.h"
#include "templates.h"
#include "zip.h"

#include <QBuffer>
#include <QtTest>

class TestIo : public QObject
{
    Q_OBJECT

    static QByteArray picture()
    {
        QImage image(8, 6, QImage::Format_ARGB32);
        image.fill(QColor(200, 30, 30));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

private Q_SLOTS:
    void container()
    {
        Document doc = Templates::make(QStringLiteral("flow"));
        Shape image;
        image.id = doc.freshId();
        image.kind = QStringLiteral("image");
        image.image = doc.addResource(picture(), QStringLiteral("png"));
        image.w = 80;
        image.h = 60;
        doc.shapes << image;

        const QByteArray bytes = Io::pack(doc);
        // The type comes first and unpacked, so that `file` can tell.
        QVERIFY(bytes.startsWith("PK"));
        QCOMPARE(bytes.mid(30, 8), QByteArrayLiteral("mimetype"));
        QCOMPARE(bytes.mid(38, int(strlen(Io::MimeType))), QByteArray(Io::MimeType));

        Document back;
        QString error;
        QVERIFY2(Io::unpack(bytes, &back, &error), qPrintable(error));
        QCOMPARE(back.uid, doc.uid);
        QCOMPARE(back.title, doc.title);
        QCOMPARE(back.shapes.size(), doc.shapes.size());
        for (int i = 0; i < doc.shapes.size(); ++i) {
            QVERIFY2(back.shapes.at(i) == doc.shapes.at(i), qPrintable(doc.shapes.at(i).name));
        }
        QCOMPARE(back.resources.value(image.image), picture());
    }
    void refusals()
    {
        Document doc;
        QString error;
        QVERIFY(!Io::unpack(QByteArrayLiteral("esto no es un zip"), &doc, &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir dir;
        error.clear();
        QVERIFY(!Io::load(dir.filePath(QStringLiteral("nada.wdib")), &doc, &error));
        QVERIFY(!error.isEmpty());
    }
    void files()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("uno.wdib"));
        const Document doc = Templates::make(QStringLiteral("mindmap"));
        QString error;
        QVERIFY2(Io::save(doc, path, &error), qPrintable(error));
        Document back;
        QVERIFY2(Io::load(path, &back, &error), qPrintable(error));
        QCOMPARE(back.shapes.size(), doc.shapes.size());
        const QImage preview = Io::preview(path);
        QVERIFY(!preview.isNull());
        QVERIFY(preview.width() >= 200);
        // Saving over a file leaves no litter beside it.
        QVERIFY(Io::save(doc, path, &error));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files).size(), 1);
    }
    void versions()
    {
        QTemporaryDir dir;
        Document doc = Templates::make(QStringLiteral("blank"));
        doc.uid = QStringLiteral("abc");
        const QDateTime start(QDate(2026, 3, 1), QTime(10, 0));
        for (int i = 0; i < Io::MaxVersions + 5; ++i) {
            doc.title = QStringLiteral("v%1").arg(i);
            QVERIFY(!Io::addVersion(dir.path(), doc, start.addSecs(i * 60)).isEmpty());
        }
        const QList<Io::Version> list = Io::versions(dir.path(), QStringLiteral("abc"));
        QCOMPARE(list.size(), int(Io::MaxVersions));
        // Newest first; the oldest five are gone.
        QCOMPARE(list.first().when, start.addSecs((Io::MaxVersions + 4) * 60));
        QCOMPARE(list.last().when, start.addSecs(5 * 60));
        Document newest;
        QVERIFY(Io::load(list.first().path, &newest));
        QCOMPARE(newest.title, QStringLiteral("v%1").arg(Io::MaxVersions + 4));
        QVERIFY(Io::versions(dir.path(), QStringLiteral("otro")).isEmpty());
    }
    void recovery()
    {
        QTemporaryDir dir;
        Document doc = Templates::make(QStringLiteral("wireframe"));
        doc.title = QStringLiteral("A medias");
        QVERIFY(Io::recoveries(dir.path()).isEmpty());
        QVERIFY(Io::writeRecovery(dir.path(), doc, QStringLiteral("/algún/sitio.wdib")));
        QVERIFY(Io::writeRecovery(dir.path(), doc, QStringLiteral("/algún/sitio.wdib")));
        const QList<Io::Recovery> found = Io::recoveries(dir.path());
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().title, QStringLiteral("A medias"));
        QCOMPARE(found.first().origin, QStringLiteral("/algún/sitio.wdib"));
        Document back;
        QVERIFY(Io::load(found.first().path, &back));
        QCOMPARE(back.uid, doc.uid);
        QCOMPARE(back.shapes.size(), doc.shapes.size());
        Io::dropRecovery(dir.path(), doc.uid);
        QVERIFY(Io::recoveries(dir.path()).isEmpty());
    }
    void templates()
    {
        QStringList keys;
        for (const QVariant &entry : Templates::list()) {
            keys << entry.toMap().value(QStringLiteral("key")).toString();
        }
        QCOMPARE(keys, QStringList({QStringLiteral("blank"), QStringLiteral("flow"), QStringLiteral("mindmap"), QStringLiteral("wireframe"), QStringLiteral("poster")}));
        QSet<QString> uids;
        for (const QString &key : keys + QStringList{QStringLiteral("sample")}) {
            const Document doc = Templates::make(key);
            QVERIFY(!doc.uid.isEmpty());
            uids << doc.uid;
            QVERIFY2(!doc.title.isEmpty(), qPrintable(key));
            if (key != QLatin1String("blank")) {
                QVERIFY2(doc.shapes.size() > 5, qPrintable(key));
            }
            QSet<QString> ids;
            for (const Shape &s : doc.shapes) {
                QVERIFY2(!ids.contains(s.id), qPrintable(key));
                ids << s.id;
                // Every connector holds on to something that is there.
                QVERIFY(s.from.isEmpty() || doc.contains(s.from));
                QVERIFY(s.to.isEmpty() || doc.contains(s.to));
            }
        }
        QCOMPARE(uids.size(), keys.size() + 1);
    }
};

QTEST_MAIN(TestIo)
#include "tst_io.moc"
