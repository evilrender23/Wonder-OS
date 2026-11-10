#include "media.h"
#include "metadata.h"
#include "photomodel.h"
#include "sample.h"
#include "thumbnailer.h"

#include <QDirIterator>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

class TestMedia : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kinds()
    {
        QVERIFY(Media::isImage(QStringLiteral("/a/b/IMG_0001.JPG")));
        QVERIFY(Media::isImage(QStringLiteral("dibujo.png")));
        QVERIFY(!Media::isImage(QStringLiteral("notas.txt")));
        QVERIFY(!Media::isImage(QStringLiteral("/a/carpeta.jpg/archivo")));
        QVERIFY(!Media::isImage(QStringLiteral(".jpg")));
        QVERIFY(Media::isVideo(QStringLiteral("clip.MP4")));
        QVERIFY(Media::isVideo(QStringLiteral("clip.mkv")));
        QVERIFY(!Media::isVideo(QStringLiteral("clip.jpg")));
        QVERIFY(Media::isMedia(QStringLiteral("clip.webm")));
        QVERIFY(Media::isAnimated(QStringLiteral("gato.GIF")));
        QVERIFY(!Media::isAnimated(QStringLiteral("gato.png")));
    }

    void dateFromName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QDateTime>("expected");
        const auto at = [](int y, int mo, int d, int h = 12, int mi = 0, int s = 0) {
            return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
        };
        QTest::newRow("android") << QStringLiteral("IMG_20240131_101500.jpg") << at(2024, 1, 31, 10, 15, 0);
        QTest::newRow("dashes") << QStringLiteral("2024-01-31 10.15.00.png") << at(2024, 1, 31, 10, 15, 0);
        QTest::newRow("screenshot") << QStringLiteral("Screenshot_20231224-235959.png") << at(2023, 12, 24, 23, 59, 59);
        QTest::newRow("whatsapp") << QStringLiteral("IMG-20240131-WA0007.jpg") << at(2024, 1, 31);
        QTest::newRow("compact") << QStringLiteral("VID20240131101500.mp4") << at(2024, 1, 31, 10, 15, 0);
        QTest::newRow("date only") << QStringLiteral("boda 2019-06-15.jpg") << at(2019, 6, 15);
        QTest::newRow("iso") << QStringLiteral("Captura de pantalla_2025-03-02T08:05:09.png") << at(2025, 3, 2, 8, 5, 9);
        QTest::newRow("none") << QStringLiteral("DSC_0042.jpg") << QDateTime();
        QTest::newRow("counter") << QStringLiteral("IMG_4032.jpg") << QDateTime();
        QTest::newRow("bad month") << QStringLiteral("foto_20241301.jpg") << QDateTime();
        QTest::newRow("long number") << QStringLiteral("1706695200123.jpg") << QDateTime();
        QTest::newRow("size") << QStringLiteral("fondo_1920x1080.png") << QDateTime();
    }
    void dateFromName()
    {
        QFETCH(QString, name);
        QFETCH(QDateTime, expected);
        QCOMPARE(Media::dateFromName(name), expected);
    }

    void screenshots()
    {
        QVERIFY(Media::looksLikeScreenshot(QStringLiteral("/home/u/Imágenes/Screenshot_20240101.png")));
        QVERIFY(Media::looksLikeScreenshot(QStringLiteral("/home/u/Imágenes/Captura de pantalla 3.png")));
        QVERIFY(Media::looksLikeScreenshot(QStringLiteral("/home/u/Imágenes/Screenshots/a.png")));
        QVERIFY(!Media::looksLikeScreenshot(QStringLiteral("/home/u/Imágenes/Viajes/IMG_1.jpg")));
    }

    void turns()
    {
        // Four quarter turns come back to the start, from any orientation.
        for (int from = 1; from <= 8; ++from) {
            int now = from;
            for (int i = 0; i < 4; ++i) {
                now = Media::turned(now, true);
            }
            QCOMPARE(now, from);
            QCOMPARE(Media::turned(Media::turned(from, true), false), from);
        }
        QCOMPARE(Media::turned(1, true), 6);
        QCOMPARE(Media::turned(6, true), 3);
        QCOMPARE(Media::turned(1, false), 8);
        // Nonsense is read as «upright».
        QCOMPARE(Media::turned(0, true), 6);
        QCOMPARE(Media::turned(99, true), 6);
    }

    void folding()
    {
        QCOMPARE(Media::fold(QStringLiteral("Cumpleaños de ÁNGEL")), QStringLiteral("cumpleanos de angel"));
    }

    void titles()
    {
        const QDateTime when(QDate(2024, 1, 31), QTime(10, 15));
        QCOMPARE(PhotoModel::title(when, QStringLiteral("year")), QStringLiteral("2024"));
        QCOMPARE(PhotoModel::title(when, QStringLiteral("month")), QStringLiteral("Enero de 2024"));
        QCOMPARE(PhotoModel::title(when, QStringLiteral("day")), QStringLiteral("31 de enero de 2024"));
    }

    void sizes()
    {
        QCOMPARE(Metadata::sizeText(512), QStringLiteral("512 bytes"));
        QCOMPARE(Metadata::sizeText(812000), QStringLiteral("812 KB"));
        QCOMPARE(Metadata::sizeText(2400000), QStringLiteral("2,4 MB"));
    }

    void metadata()
    {
        QTemporaryDir dir;
        const QString path = Sample::picture(dir.filePath(QStringLiteral("a.jpg")), 64, 48);
        Sample::exif(path, QStringLiteral("2021:07:04 18:30:00"), 6, QStringLiteral("Canon"), QStringLiteral("Canon EOS R6"));
        MediaItem item;
        item.path = path;
        item.mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
        item.size = QFileInfo(path).size();
        Metadata::fill(item);
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(item.taken), QDateTime(QDate(2021, 7, 4), QTime(18, 30)));
        QCOMPARE(item.camera, QStringLiteral("Canon EOS R6"));
        // On its side: shown taller than wide.
        QCOMPARE(item.width, 48);
        QCOMPARE(item.height, 64);
        QVERIFY(!item.video);
        QCOMPARE(Metadata::orientation(path), 6);

        QVariantMap rows;
        for (const QVariant &row : Metadata::details(item)) {
            rows.insert(row.toMap().value(QStringLiteral("label")).toString(), row.toMap().value(QStringLiteral("value")));
        }
        QCOMPARE(rows.value(QStringLiteral("Fecha")).toString(), QStringLiteral("4 de julio de 2021, 18:30"));
        QCOMPARE(rows.value(QStringLiteral("Dimensiones")).toString(), QStringLiteral("48 × 64"));
        QCOMPARE(rows.value(QStringLiteral("Cámara")).toString(), QStringLiteral("Canon EOS R6"));
        QCOMPARE(rows.value(QStringLiteral("Tipo")).toString(), QStringLiteral("JPG"));
    }

    void dateFallsBack()
    {
        QTemporaryDir dir;
        // No EXIF: the name says when.
        const QString named = Sample::picture(dir.filePath(QStringLiteral("IMG_20200229_081500.png")));
        // Neither: the file's own date.
        const QString plain = Sample::picture(dir.filePath(QStringLiteral("dibujo.png")));
        const QDateTime written(QDate(2018, 5, 6), QTime(7, 8, 9));
        Sample::dated(plain, written);
        // A date in the name later than the file itself is not believed.
        const QString future = Sample::picture(dir.filePath(QStringLiteral("render_20991231.png")));
        Sample::dated(future, written);
        const auto taken = [](const QString &path) {
            MediaItem item;
            item.path = path;
            item.mtime = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            item.size = QFileInfo(path).size();
            Metadata::fill(item);
            return QDateTime::fromMSecsSinceEpoch(item.taken);
        };
        QCOMPARE(taken(named), QDateTime(QDate(2020, 2, 29), QTime(8, 15)));
        QCOMPARE(taken(plain), written);
        QCOMPARE(taken(future), written);
    }

    void rotateByTag()
    {
        QTemporaryDir dir;
        const QString path = Sample::picture(dir.filePath(QStringLiteral("a.jpg")), 64, 48);
        const QDateTime before = QFileInfo(path).lastModified();
        QFile source(path);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QImage pixels = QImage(path);
        source.close();

        QVERIFY(Metadata::canRotate(path));
        QVERIFY(Metadata::rotate(path, true));
        QCOMPARE(Metadata::orientation(path), 6);
        // The picture itself was not compressed again.
        QImageReader raw(path);
        raw.setAutoTransform(false);
        QCOMPARE(raw.read(), pixels);
        // Shown turned: the dark half, which was on the left, is now on top.
        const QImage shown = Thumbnailer::picture(path);
        QCOMPARE(shown.size(), QSize(48, 64));
        QVERIFY(shown.pixelColor(24, 4).lightness() < 60);
        QVERIFY(shown.pixelColor(24, 60).lightness() > 60);
        QCOMPARE(QFileInfo(path).lastModified(), before.addSecs(1));

        QVERIFY(Metadata::rotate(path, false));
        QCOMPARE(Metadata::orientation(path), 1);
    }

    void rotateByPixels()
    {
        QTemporaryDir dir;
        const QString path = Sample::picture(dir.filePath(QStringLiteral("a.png")), 64, 48);
        QVERIFY(Metadata::canRotate(path));
        QVERIFY(Metadata::rotate(path, true));
        const QImage shown(path);
        QCOMPARE(shown.size(), QSize(48, 64));
        QCOMPARE(shown.pixelColor(24, 4), QColor(Qt::black));
        QCOMPARE(shown.pixelColor(24, 60), QColor(Qt::red));
        // Nothing left behind next to it.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 1);

        const QString webp = dir.filePath(QStringLiteral("a.webp"));
        QVERIFY(!Metadata::canRotate(webp));
        QString error;
        QVERIFY(!Metadata::rotate(dir.filePath(QStringLiteral("nada.jpg")), true, &error));
        QVERIFY(!error.isEmpty());
    }

    void thumbnails()
    {
        QTemporaryDir dir;
        QTemporaryDir cache;
        Thumbnailer::setCacheDir(cache.path());
        const QString wide = Sample::picture(dir.filePath(QStringLiteral("wide.png")), 2000, 1000);
        const QImage thumb = Thumbnailer::thumbnail(wide);
        // The shortest side is what fills a square.
        QCOMPARE(thumb.height(), Thumbnailer::kSide);
        QCOMPARE(thumb.width(), Thumbnailer::kSide * 2);

        // Kept: one file, and asked again it is that file that answers.
        const auto cached = [&cache] {
            int files = 0;
            QDirIterator it(cache.path(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                ++files;
            }
            return files;
        };
        QCOMPARE(cached(), 1);
        QCOMPARE(Thumbnailer::thumbnail(wide).size(), thumb.size());
        QCOMPARE(cached(), 1);

        // A small picture is not blown up.
        const QString small = Sample::picture(dir.filePath(QStringLiteral("small.png")), 40, 30);
        QCOMPARE(Thumbnailer::thumbnail(small).size(), QSize(40, 30));
        // A panorama is not kept wider than three squares.
        const QString pano = Sample::picture(dir.filePath(QStringLiteral("pano.png")), 6000, 500);
        QVERIFY(Thumbnailer::thumbnail(pano).width() <= Thumbnailer::kSide * 3);

        // A changed file gets a new thumbnail.
        Sample::picture(wide, 1000, 2000);
        Sample::dated(wide, QDateTime::currentDateTime().addSecs(5));
        QCOMPARE(Thumbnailer::thumbnail(wide).width(), Thumbnailer::kSide);

        QVERIFY(Thumbnailer::thumbnail(dir.filePath(QStringLiteral("nada.png"))).isNull());
        QCOMPARE(Thumbnailer::prune(30), 0);
        QVERIFY(Thumbnailer::prune(-1) >= 4);
        Thumbnailer::setCacheDir(QString());
    }
};

QTEST_MAIN(TestMedia)
#include "tst_media.moc"
