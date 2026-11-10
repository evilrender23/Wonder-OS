#include "library.h"
#include "sample.h"
#include "store.h"
#include "tagreader.h"

#include <QImage>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestLibrary : public QObject
{
    Q_OBJECT

    static bool scanned(Library &library)
    {
        QSignalSpy spy(&library, &Library::scanningChanged);
        library.rescan();
        while (library.scanning()) {
            if (!spy.wait(10000)) {
                return false;
            }
        }
        return true;
    }

private Q_SLOTS:
    void tagsAreRead()
    {
        QTemporaryDir dir;
        const QString path = writeSample(dir.filePath(QStringLiteral("uno.wav")), QStringLiteral("Canción"), QStringLiteral("Björk"), QStringLiteral("Disco"), 4, 1997, QStringLiteral("Varios"));
        const Track track = TagReader::read(path);
        QCOMPARE(track.title, QStringLiteral("Canción"));
        QCOMPARE(track.artist, QStringLiteral("Björk"));
        QCOMPARE(track.album, QStringLiteral("Disco"));
        QCOMPARE(track.albumArtist, QStringLiteral("Varios"));
        QCOMPARE(track.albumOwner(), QStringLiteral("Varios"));
        QCOMPARE(track.number, 4);
        QCOMPARE(track.year, 1997);
        QVERIFY(track.durationMs >= 900 && track.durationMs <= 1100);
        QVERIFY(track.size > 0);
    }

    void aFileWithoutTagsStillHasAPlace()
    {
        QTemporaryDir dir;
        const Track track = TagReader::read(writeSample(dir.filePath(QStringLiteral("Sin nombre.wav"))));
        QCOMPARE(track.title, QStringLiteral("Sin nombre"));
        QCOMPARE(track.artist, TagReader::unknownArtist());
        QCOMPARE(track.album, TagReader::unknownAlbum());
    }

    void onlyAudioIsTakenIn()
    {
        QVERIFY(TagReader::isAudio(QStringLiteral("/x/a.MP3")));
        QVERIFY(TagReader::isAudio(QStringLiteral("a.flac")));
        QVERIFY(!TagReader::isAudio(QStringLiteral("cover.jpg")));
        QVERIFY(!TagReader::isAudio(QStringLiteral("mp3")));
    }

    void theCoverBesideTheFileIsFound()
    {
        QTemporaryDir dir;
        const QString path = writeSample(dir.filePath(QStringLiteral("a.wav")));
        QVERIFY(TagReader::cover(path).isNull());
        QImage other(4, 4, QImage::Format_RGB32);
        other.fill(Qt::blue);
        QVERIFY(other.save(dir.filePath(QStringLiteral("zzz.png"))));
        // Any picture is not a cover.
        QVERIFY(TagReader::cover(path).isNull());
        QImage cover(8, 8, QImage::Format_RGB32);
        cover.fill(Qt::red);
        QVERIFY(cover.save(dir.filePath(QStringLiteral("Cover.png"))));
        QCOMPARE(TagReader::cover(path).size(), QSize(8, 8));
    }

    void foldingIgnoresCaseAndAccents()
    {
        QCOMPARE(folded(QStringLiteral("BJÖRK Canción")), QStringLiteral("bjork cancion"));
    }

    void scanFindsAudioInSubfoldersSorted()
    {
        QTemporaryDir dir;
        writeSample(dir.filePath(QStringLiteral("z/2.wav")), QStringLiteral("Dos"), QStringLiteral("Zeta"), QStringLiteral("Uno"), 2, 2001);
        writeSample(dir.filePath(QStringLiteral("z/1.wav")), QStringLiteral("Uno"), QStringLiteral("Zeta"), QStringLiteral("Uno"), 1, 2001);
        writeSample(dir.filePath(QStringLiteral("z/viejo/1.wav")), QStringLiteral("Antes"), QStringLiteral("Zeta"), QStringLiteral("Viejo"), 1, 1990);
        writeSample(dir.filePath(QStringLiteral("a/x.wav")), QStringLiteral("Equis"), QStringLiteral("alfa"), QStringLiteral("Disco"), 1, 2010);
        QFile note(dir.filePath(QStringLiteral("a/notas.txt")));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.close();

        const Library::Scan scan = Library::scan({dir.path(), dir.filePath(QStringLiteral("no-existe"))}, {});
        QStringList titles;
        for (const Track &track : scan.tracks) {
            titles << track.title;
        }
        // Artists by name whatever their case; an artist's albums oldest first; tracks by number.
        QCOMPARE(titles, QStringList({"Equis", "Antes", "Uno", "Dos"}));
        QVERIFY(scan.folders.contains(dir.filePath(QStringLiteral("z/viejo"))));
        QVERIFY(!scan.folders.contains(dir.filePath(QStringLiteral("no-existe"))));
    }

    void aFolderListedTwiceIsScannedOnce()
    {
        QTemporaryDir dir;
        writeSample(dir.filePath(QStringLiteral("sub/a.wav")));
        const Library::Scan scan = Library::scan({dir.path(), dir.filePath(QStringLiteral("sub"))}, {});
        QCOMPARE(scan.tracks.size(), 1);
    }

    void unchangedFilesAreNotReadAgain()
    {
        QTemporaryDir dir;
        const QString path = writeSample(dir.filePath(QStringLiteral("a.wav")), QStringLiteral("Real"), QStringLiteral("A"), QStringLiteral("B"));
        Track remembered = TagReader::read(path);
        remembered.title = QStringLiteral("Recordada");
        QHash<QString, Track> known{{path, remembered}};
        QCOMPARE(Library::scan({dir.path()}, known).tracks.first().title, QStringLiteral("Recordada"));
        // A different size means a different file.
        known[path].size += 1;
        QCOMPARE(Library::scan({dir.path()}, known).tracks.first().title, QStringLiteral("Real"));
    }

    void albumsAndArtistsAreGrouped()
    {
        QTemporaryDir music;
        QTemporaryDir data;
        writeSample(music.filePath(QStringLiteral("1.wav")), QStringLiteral("Uno"), QStringLiteral("Ana"), QStringLiteral("Mar"), 1, 2003);
        writeSample(music.filePath(QStringLiteral("2.wav")), QStringLiteral("Dos"), QStringLiteral("Ana con Luis"), QStringLiteral("Mar"), 2, 2003, QStringLiteral("Ana"));
        writeSample(music.filePath(QStringLiteral("3.wav")), QStringLiteral("Tres"), QStringLiteral("Ana"), QStringLiteral("Cielo"), 1, 1999);
        writeSample(music.filePath(QStringLiteral("4.wav")), QStringLiteral("Cuatro"), QStringLiteral("Luis"), QStringLiteral("Mar"), 1);

        Store store(data.filePath(QStringLiteral("settings.json")));
        // Not the music of whoever runs the tests.
        store.set(QStringLiteral("folders"), QJsonArray());
        Library library(&store, data.filePath(QStringLiteral("library.json")), false);
        library.addFolder(QUrl::fromLocalFile(music.path()));
        QVERIFY(scanned(library));
        QCOMPARE(library.count(), 4);

        // The guest on track 2 does not split Ana's album; Luis's «Mar» is another one.
        QCOMPARE(library.albums().size(), 3);
        QCOMPARE(library.artists().size(), 2);
        const QVariantMap ana = library.artists().first().toMap();
        QCOMPARE(ana.value(QStringLiteral("name")).toString(), QStringLiteral("Ana"));
        QCOMPARE(ana.value(QStringLiteral("albums")).toInt(), 2);
        QCOMPARE(ana.value(QStringLiteral("tracks")).toInt(), 3);

        const QVariantList hers = library.albumsOf(QStringLiteral("Ana"));
        QCOMPARE(hers.size(), 2);
        QCOMPARE(hers.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Cielo"));
        const QVariantMap mar = hers.last().toMap();
        QCOMPARE(mar.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(mar.value(QStringLiteral("year")).toInt(), 2003);
        const QString key = mar.value(QStringLiteral("key")).toString();
        QCOMPARE(library.album(key).value(QStringLiteral("title")).toString(), QStringLiteral("Mar"));
        QCOMPARE(library.albumPaths(key).size(), 2);
        QCOMPARE(library.artistPaths(QStringLiteral("Ana")).size(), 3);
        QVERIFY(library.album(QStringLiteral("nope")).isEmpty());
        QCOMPARE(library.recentAlbums().size(), 3);
    }

    void theLibraryIsRememberedAndFollowsTheDisk()
    {
        QTemporaryDir music;
        QTemporaryDir data;
        const QString settings = data.filePath(QStringLiteral("settings.json"));
        const QString cache = data.filePath(QStringLiteral("library.json"));
        const QString one = writeSample(music.filePath(QStringLiteral("1.wav")), QStringLiteral("Uno"), QStringLiteral("Ana"), QStringLiteral("Mar"));
        {
            Store store(settings);
            store.set(QStringLiteral("folders"), QJsonArray());
            Library library(&store, cache, false);
            library.addFolder(QUrl::fromLocalFile(music.path()));
            // The same folder again changes nothing.
            library.addFolder(QUrl::fromLocalFile(music.path() + QLatin1Char('/')));
            QCOMPARE(library.folders().count(music.path()), 1);
            QVERIFY(scanned(library));
            QCOMPARE(library.count(), 1);
        }
        Store store(settings);
        Library library(&store, cache, false);
        // Known before any scan: the window opens with the music already there.
        QCOMPARE(library.count(), 1);
        QVERIFY(library.contains(one));
        QCOMPARE(library.track(one).title, QStringLiteral("Uno"));
        QVERIFY(library.folders().contains(music.path()));

        writeSample(music.filePath(QStringLiteral("2.wav")), QStringLiteral("Dos"), QStringLiteral("Ana"), QStringLiteral("Mar"));
        QVERIFY(QFile::remove(one));
        QVERIFY(scanned(library));
        QCOMPARE(library.count(), 1);
        QCOMPARE(library.tracks().first().title, QStringLiteral("Dos"));

        library.removeFolder(music.path());
        QVERIFY(scanned(library));
        QCOMPARE(library.count(), 0);
        QVERIFY(library.albums().isEmpty());
    }

    void aFileOutsideTheLibraryCanStillBePlayed()
    {
        QTemporaryDir dir;
        QTemporaryDir data;
        Store store(data.filePath(QStringLiteral("s.json")));
        store.set(QStringLiteral("folders"), QJsonArray());
        Library library(&store, data.filePath(QStringLiteral("l.json")), false);
        const QString path = writeSample(dir.filePath(QStringLiteral("suelta.wav")), QStringLiteral("Suelta"), QStringLiteral("X"), QStringLiteral("Y"));
        QVERIFY(!library.contains(path));
        QCOMPARE(library.track(path).title, QStringLiteral("Suelta"));
        QVERIFY(!library.track(dir.filePath(QStringLiteral("no.wav"))).isValid());
    }
};

QTEST_MAIN(TestLibrary)
#include "tst_library.moc"
