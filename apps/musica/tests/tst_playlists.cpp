#include "playlists.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestPlaylists : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void favoritesToggleAndSurviveARestart()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("deep/playlists.json"));
        {
            Playlists playlists(file);
            QVERIFY(!playlists.isFavorite(QStringLiteral("/a.mp3")));
            const int before = playlists.revision();
            playlists.toggleFavorite(QStringLiteral("/a.mp3"));
            playlists.toggleFavorite(QStringLiteral("/b.mp3"));
            playlists.toggleFavorite(QStringLiteral("/a.mp3"));
            QVERIFY(playlists.revision() > before);
            QCOMPARE(playlists.paths(Playlists::favorites()), QStringList{QStringLiteral("/b.mp3")});
            QVERIFY(playlists.list().isEmpty());
        }
        Playlists again(file);
        QVERIFY(again.isFavorite(QStringLiteral("/b.mp3")));
        QVERIFY(!again.isFavorite(QStringLiteral("/a.mp3")));
        QCOMPARE(again.name(Playlists::favorites()), QStringLiteral("Favoritas"));
    }

    void favoritesHoldEachTrackOnce()
    {
        QTemporaryDir dir;
        Playlists playlists(dir.filePath(QStringLiteral("p.json")));
        playlists.add(Playlists::favorites(), {QStringLiteral("/a"), QStringLiteral("/a"), QStringLiteral("/b")});
        QCOMPARE(playlists.paths(Playlists::favorites()).size(), 2);
    }

    void playlistsAreCreatedRenamedFilledAndRemoved()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("p.json"));
        QString id;
        {
            Playlists playlists(file);
            QSignalSpy listed(&playlists, &Playlists::listChanged);
            id = playlists.create(QStringLiteral("  Para correr "), {QStringLiteral("/a")});
            QCOMPARE(playlists.name(id), QStringLiteral("Para correr"));
            QVERIFY(listed.count() >= 1);
            // A playlist may hold a song twice.
            playlists.add(id, {QStringLiteral("/b"), QStringLiteral("/a")});
            QCOMPARE(playlists.paths(id), QStringList({"/a", "/b", "/a"}));
            playlists.removeAt(id, 0);
            QCOMPARE(playlists.paths(id), QStringList({"/b", "/a"}));
            playlists.removeAt(id, 9);
            QCOMPARE(playlists.paths(id).size(), 2);
            playlists.rename(id, QStringLiteral("   "));
            QCOMPARE(playlists.name(id), QStringLiteral("Para correr"));
            playlists.rename(id, QStringLiteral("Gimnasio"));
            const QVariantMap entry = playlists.list().first().toMap();
            QCOMPARE(entry.value(QStringLiteral("name")).toString(), QStringLiteral("Gimnasio"));
            QCOMPARE(entry.value(QStringLiteral("count")).toInt(), 2);
            QCOMPARE(entry.value(QStringLiteral("cover")).toString(), QStringLiteral("/b"));
        }
        Playlists again(file);
        QCOMPARE(again.list().size(), 1);
        QCOMPARE(again.paths(id), QStringList({"/b", "/a"}));
        again.remove(id);
        QVERIFY(again.list().isEmpty());
        QVERIFY(Playlists(file).list().isEmpty());
    }

    void anUnnamedPlaylistGetsAName()
    {
        QTemporaryDir dir;
        Playlists playlists(dir.filePath(QStringLiteral("p.json")));
        const QString id = playlists.create(QString());
        QCOMPARE(playlists.name(id), QStringLiteral("Nueva playlist"));
    }

    void unknownPlaylistsAreIgnored()
    {
        QTemporaryDir dir;
        Playlists playlists(dir.filePath(QStringLiteral("p.json")));
        playlists.add(QStringLiteral("nope"), {QStringLiteral("/a")});
        playlists.removeAt(QStringLiteral("nope"), 0);
        playlists.remove(QStringLiteral("nope"));
        QVERIFY(playlists.paths(QStringLiteral("nope")).isEmpty());
        QCOMPARE(playlists.name(QStringLiteral("nope")), QString());
    }

    void aBrokenFileIsAnEmptyStart()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("p.json"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("{ not json");
        out.close();
        Playlists playlists(file);
        QVERIFY(playlists.list().isEmpty());
        playlists.toggleFavorite(QStringLiteral("/a"));
        QVERIFY(Playlists(file).isFavorite(QStringLiteral("/a")));
    }
};

QTEST_GUILESS_MAIN(TestPlaylists)
#include "tst_playlists.moc"
