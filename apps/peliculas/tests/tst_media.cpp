#include "media.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static void touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
}

class TestMedia : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kinds()
    {
        QVERIFY(Media::isVideo(QStringLiteral("/a/Film.MKV")));
        QVERIFY(Media::isVideo(QStringLiteral("clip.mp4")));
        QVERIFY(!Media::isVideo(QStringLiteral("song.mp3")));
        QVERIFY(Media::isAudio(QStringLiteral("song.FLAC")));
        QVERIFY(Media::isPlayable(QStringLiteral("song.opus")));
        QVERIFY(!Media::isPlayable(QStringLiteral("notes.txt")));
        QVERIFY(!Media::isPlayable(QStringLiteral("film.srt")));
        QVERIFY(Media::isSubtitle(QStringLiteral("film.es.SRT")));
        QVERIFY(Media::isSubtitle(QStringLiteral("film.ass")));
        QVERIFY(!Media::isSubtitle(QStringLiteral("film.mkv")));
        QVERIFY(!Media::isVideo(QStringLiteral("mkv")));
    }

    void titles()
    {
        QCOMPARE(Media::title(QStringLiteral("/films/El viaje.2019.mkv")), QStringLiteral("El viaje.2019"));
        QCOMPARE(Media::title(QStringLiteral("/films/sin-final")), QStringLiteral("sin-final"));
        QCOMPARE(Media::title(QStringLiteral("/films/.oculto")), QStringLiteral(".oculto"));
    }

    void times()
    {
        QCOMPARE(Media::time(0), QStringLiteral("0:00"));
        QCOMPARE(Media::time(-5), QStringLiteral("0:00"));
        QCOMPARE(Media::time(187000), QStringLiteral("3:07"));
        QCOMPARE(Media::time(187999), QStringLiteral("3:07"));
        QCOMPARE(Media::time(3765000), QStringLiteral("1:02:45"));
    }

    void parsedTimes()
    {
        QCOMPARE(Media::parseTime(QStringLiteral("90")), qint64(90000));
        QCOMPARE(Media::parseTime(QStringLiteral(" 1:30 ")), qint64(90000));
        QCOMPARE(Media::parseTime(QStringLiteral("1:02:03")), qint64(3723000));
        QCOMPARE(Media::parseTime(QStringLiteral("0")), qint64(0));
        QCOMPARE(Media::parseTime(QStringLiteral("1:75")), qint64(-1));
        QCOMPARE(Media::parseTime(QStringLiteral("abc")), qint64(-1));
        QCOMPARE(Media::parseTime(QString()), qint64(-1));
        QCOMPARE(Media::parseTime(QStringLiteral("1:2:3:4")), qint64(-1));
        QCOMPARE(Media::parseTime(QStringLiteral("-3")), qint64(-1));
    }

    void expandsFolders()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        touch(root + QStringLiteral("/Serie/Cap 10.mkv"));
        touch(root + QStringLiteral("/Serie/Cap 2.mkv"));
        touch(root + QStringLiteral("/Serie/cap 1.mkv"));
        touch(root + QStringLiteral("/Serie/Cap 2.srt"));
        touch(root + QStringLiteral("/Serie/notas.txt"));
        touch(root + QStringLiteral("/Serie/Extras/detras.mp4"));
        touch(root + QStringLiteral("/suelto.bin"));

        const QStringList got = Media::expand({root + QStringLiteral("/suelto.bin"), root + QStringLiteral("/Serie"), root + QStringLiteral("/no-existe.mkv")});
        const QStringList want = {
            root + QStringLiteral("/suelto.bin"),
            root + QStringLiteral("/Serie/cap 1.mkv"),
            root + QStringLiteral("/Serie/Cap 2.mkv"),
            root + QStringLiteral("/Serie/Cap 10.mkv"),
            root + QStringLiteral("/Serie/Extras/detras.mp4"),
        };
        QCOMPARE(got, want);
        // Named twice, there once.
        QCOMPARE(Media::expand({root + QStringLiteral("/Serie"), root + QStringLiteral("/Serie/Cap 2.mkv")}).size(), 4);
    }

    void siblings()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        touch(root + QStringLiteral("/b.mkv"));
        touch(root + QStringLiteral("/a.mp4"));
        touch(root + QStringLiteral("/a.srt"));
        touch(root + QStringLiteral("/dentro/c.mkv"));
        touch(root + QStringLiteral("/raro.xyz"));
        const QStringList both = {root + QStringLiteral("/a.mp4"), root + QStringLiteral("/b.mkv")};
        QCOMPARE(Media::siblings(root + QStringLiteral("/b.mkv")), both);
        // A file with an ending the player does not know still leads its folder.
        QCOMPARE(Media::siblings(root + QStringLiteral("/raro.xyz")), QStringList{root + QStringLiteral("/raro.xyz")} + both);
    }
};

QTEST_GUILESS_MAIN(TestMedia)
#include "tst_media.moc"
