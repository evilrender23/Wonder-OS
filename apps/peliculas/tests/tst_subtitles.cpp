#include "subtitles.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

class TestSubtitles : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void subrip()
    {
        const QString text = QStringLiteral("1\r\n00:00:01,000 --> 00:00:03,500\r\nHola, <i>mundo</i>.\r\n\r\n"
                                            "2\r\n00:01:02,250 --> 00:01:04,000\r\n{\\an8}Dos\r\nlíneas\r\n\r\n"
                                            "3\r\n01:00:00,000 --> 01:00:01,000\r\n<font color=\"#ff0000\">Rojo</font> &amp; azul\r\n");
        const QList<Cue> cues = Subtitles::parse(text, QStringLiteral("srt"));
        QCOMPARE(cues.size(), 3);
        QCOMPARE(cues.at(0), (Cue{1000, 3500, QStringLiteral("Hola, mundo.")}));
        QCOMPARE(cues.at(1), (Cue{62250, 64000, QStringLiteral("Dos\nlíneas")}));
        QCOMPARE(cues.at(2), (Cue{3600000, 3601000, QStringLiteral("Rojo & azul")}));
    }

    void subripWithoutBlankLines()
    {
        // Some files lose the empty line between blocks.
        const QString text = QStringLiteral("1\n00:00:01,000 --> 00:00:02,000\nUno\n2\n00:00:03,000 --> 00:00:04,000\nDos\n");
        const QList<Cue> cues = Subtitles::parse(text, QStringLiteral("srt"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.at(0).text, QStringLiteral("Uno"));
        QCOMPARE(cues.at(1).text, QStringLiteral("Dos"));
    }

    void brokenBlocksAreSkipped()
    {
        const QString text = QStringLiteral("1\n00:00:xx,000 --> 00:00:02,000\nMal\n\n"
                                            "2\n00:00:05,000 --> 00:00:04,000\nAl revés\n\n"
                                            "3\n00:00:06,000 --> 00:00:07,000\n\n\n"
                                            "4\n00:00:08,000 --> 00:00:09,000\nBien\n");
        const QList<Cue> cues = Subtitles::parse(text, QStringLiteral("srt"));
        QCOMPARE(cues.size(), 1);
        QCOMPARE(cues.at(0).text, QStringLiteral("Bien"));
        QVERIFY(Subtitles::parse(QString(), QStringLiteral("srt")).isEmpty());
        QVERIFY(Subtitles::parse(QStringLiteral("no es un subtítulo"), QStringLiteral("srt")).isEmpty());
    }

    void webvtt()
    {
        const QString text = QStringLiteral("WEBVTT\n\nNOTE un comentario\n\nintro\n00:01.000 --> 00:02.500 align:start position:10%\n<v Ana>Hola\n\n"
                                            "01:00:00.000 --> 01:00:01.000\nTarde\n");
        const QList<Cue> cues = Subtitles::parse(text, QStringLiteral("vtt"));
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.at(0), (Cue{1000, 2500, QStringLiteral("Hola")}));
        QCOMPARE(cues.at(1), (Cue{3600000, 3601000, QStringLiteral("Tarde")}));
    }

    void substation()
    {
        const QString text = QStringLiteral("[Script Info]\nTitle: x\n\n[V4+ Styles]\nFormat: Name, Fontname\nStyle: Default,Arial\n\n[Events]\n"
                                            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                                            "Dialogue: 0,0:00:05.50,0:00:07.00,Default,,0,0,0,,{\\i1}Después{\\i0}, con comas, sí\n"
                                            "Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,no se ve\n"
                                            "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Antes\\Nabajo\n");
        const QList<Cue> cues = Subtitles::parse(text, QStringLiteral("ass"));
        QCOMPARE(cues.size(), 2);
        // Ordered by when they start, not by where they are in the file.
        QCOMPARE(cues.at(0), (Cue{1000, 2000, QStringLiteral("Antes\nabajo")}));
        QCOMPARE(cues.at(1), (Cue{5500, 7000, QStringLiteral("Después, con comas, sí")}));
    }

    void textAtAMoment()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.srt"));
        write(path, "1\n00:00:01,000 --> 00:00:10,000\nLargo\n\n2\n00:00:02,000 --> 00:00:03,000\nCorto\n\n3\n00:00:20,000 --> 00:00:21,000\nFinal\n");
        Subtitles subtitles;
        QVERIFY(subtitles.load(path));
        QCOMPARE(subtitles.count(), 3);
        QCOMPARE(subtitles.path(), path);
        QCOMPARE(subtitles.textAt(0), QString());
        QCOMPARE(subtitles.textAt(1000), QStringLiteral("Largo"));
        QCOMPARE(subtitles.textAt(2500), QStringLiteral("Largo\nCorto"));
        // The short one has gone, the long one is still there behind it.
        QCOMPARE(subtitles.textAt(5000), QStringLiteral("Largo"));
        QCOMPARE(subtitles.textAt(10000), QString());
        QCOMPARE(subtitles.textAt(20500), QStringLiteral("Final"));
        QCOMPARE(subtitles.textAt(999999), QString());
        subtitles.clear();
        QVERIFY(subtitles.isEmpty());
        QCOMPARE(subtitles.textAt(2500), QString());
    }

    void loadFails()
    {
        QTemporaryDir dir;
        Subtitles subtitles;
        QVERIFY(!subtitles.load(dir.filePath(QStringLiteral("no-existe.srt"))));
        const QString empty = dir.filePath(QStringLiteral("vacio.srt"));
        write(empty, "nada que parezca un subtítulo\n");
        QVERIFY(!subtitles.load(empty));
        QVERIFY(subtitles.path().isEmpty());
    }

    void encodings()
    {
        QCOMPARE(Subtitles::decode("\xEF\xBB\xBF" "ni\xC3\xB1o"), QStringLiteral("niño"));
        QCOMPARE(Subtitles::decode("ni\xC3\xB1o"), QStringLiteral("niño"));
        // An old file in Latin-1: «ñ» is one byte, which is no UTF-8.
        QCOMPARE(Subtitles::decode("ni\xF1o"), QStringLiteral("niño"));
        QCOMPARE(Subtitles::decode(QByteArray("\xFF\xFE" "n\0i\0\xF1\0o\0", 10)), QStringLiteral("niño"));
    }

    void filesBeside()
    {
        QTemporaryDir dir;
        const QString root = dir.path();
        for (const char *name : {"Film.mkv", "Film.srt", "Film.es.srt", "film.EN.ass", "Film 2.srt", "Filmografia.srt", "Film.txt", "Otra.srt"}) {
            write(root + QLatin1Char('/') + QString::fromLatin1(name), "x");
        }
        const QString video = root + QStringLiteral("/Film.mkv");
        const QStringList found = Subtitles::beside(video);
        const QStringList want = {root + QStringLiteral("/Film.srt"), root + QStringLiteral("/film.EN.ass"), root + QStringLiteral("/Film.es.srt")};
        QCOMPARE(found, want);
        QCOMPARE(Subtitles::label(video, root + QStringLiteral("/Film.es.srt")), QStringLiteral("Español"));
        QCOMPARE(Subtitles::label(video, root + QStringLiteral("/film.EN.ass")), QStringLiteral("Inglés"));
        QCOMPARE(Subtitles::label(video, root + QStringLiteral("/Film.srt")), QStringLiteral("Film.srt"));
        QCOMPARE(Subtitles::label(video, root + QStringLiteral("/Film.forzados.srt")), QStringLiteral("Film.forzados.srt"));
        QCOMPARE(Subtitles::label(video, QStringLiteral("/otro/sitio/x.srt")), QStringLiteral("x.srt"));
    }
};

QTEST_GUILESS_MAIN(TestSubtitles)
#include "tst_subtitles.moc"
