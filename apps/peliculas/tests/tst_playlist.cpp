#include "playlist.h"

#include <QAbstractItemModelTester>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

static QStringList films(int count)
{
    QStringList out;
    for (int i = 1; i <= count; ++i) {
        out << QStringLiteral("/films/%1.mkv").arg(i);
    }
    return out;
}

class TestPlaylist : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void setsAndWalks()
    {
        Playlist list;
        QAbstractItemModelTester tester(&list, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(list.current(), -1);
        QCOMPARE(list.next(true), -1);
        QCOMPARE(list.previous(), -1);

        list.set(films(3), 1);
        QCOMPARE(list.count(), 3);
        QCOMPARE(list.current(), 1);
        QCOMPARE(list.currentPath(), QStringLiteral("/films/2.mkv"));
        QCOMPARE(list.data(list.index(1), Playlist::TitleRole).toString(), QStringLiteral("2"));
        QCOMPARE(list.data(list.index(1), Playlist::FolderRole).toString(), QStringLiteral("films"));
        QCOMPARE(list.data(list.index(1), Playlist::CurrentRole).toBool(), true);
        QCOMPARE(list.data(list.index(0), Playlist::CurrentRole).toBool(), false);
        QCOMPARE(list.next(false), 2);
        QCOMPARE(list.previous(), 0);
        list.setCurrent(2);
        QCOMPARE(list.next(false), -1);
        list.setCurrent(0);
        QCOMPARE(list.previous(), -1);
        // Out of range: nothing moves.
        list.setCurrent(7);
        QCOMPARE(list.current(), 0);
    }

    void duplicatesStayOnce()
    {
        Playlist list;
        list.set({QStringLiteral("/a.mkv"), QStringLiteral("/b.mkv"), QStringLiteral("/a.mkv"), QStringLiteral("/c.mkv")}, 3);
        QCOMPARE(list.count(), 3);
        QCOMPARE(list.currentPath(), QStringLiteral("/c.mkv"));
        QCOMPARE(list.append({QStringLiteral("/b.mkv"), QStringLiteral("/d.mkv"), QStringLiteral("/d.mkv")}), 1);
        QCOMPARE(list.count(), 4);
        QCOMPARE(list.path(3), QStringLiteral("/d.mkv"));
        QCOMPARE(list.append({}), -1);
    }

    void repeats()
    {
        Playlist list;
        list.set(films(3), 2);
        list.setRepeat(1);
        QCOMPARE(list.next(false), 0);
        list.setCurrent(0);
        QCOMPARE(list.previous(), 2);
        list.setRepeat(2);
        // On its own it plays again; by hand it moves on.
        QCOMPARE(list.next(false), 0);
        QCOMPARE(list.next(true), 1);
        list.setRepeat(9);
        QCOMPARE(list.repeat(), 2);
    }

    void shufflePlaysEveryOneOnce()
    {
        Playlist list;
        list.seed(7);
        list.set(films(20), 5);
        list.setShuffle(true);
        QSet<int> seen{list.current()};
        QList<int> order{list.current()};
        while (true) {
            const int next = list.next(false);
            if (next < 0) {
                break;
            }
            QVERIFY2(!seen.contains(next), "played twice");
            seen.insert(next);
            order << next;
            list.setCurrent(next);
        }
        QCOMPARE(seen.size(), 20);
        QList<int> straight;
        for (int i = 0; i < 20; ++i) {
            straight << (5 + i) % 20;
        }
        QVERIFY(order != straight);
        // And back the way it came.
        QCOMPARE(list.previous(), order.at(18));
        // With the list on repeat the order starts over.
        list.setRepeat(1);
        QCOMPARE(list.next(false), order.first());
    }

    void shuffleKeepsWhatIsAddedForLater()
    {
        Playlist list;
        list.seed(3);
        list.set(films(4), 0);
        list.setShuffle(true);
        list.setCurrent(list.next(false));
        list.setCurrent(list.next(false));
        const int playing = list.current();
        list.append({QStringLiteral("/films/nuevo-a.mkv"), QStringLiteral("/films/nuevo-b.mkv")});
        QCOMPARE(list.current(), playing);
        QSet<QString> rest;
        while (list.next(false) >= 0) {
            list.setCurrent(list.next(false));
            rest.insert(list.currentPath());
        }
        QVERIFY(rest.contains(QStringLiteral("/films/nuevo-a.mkv")));
        QVERIFY(rest.contains(QStringLiteral("/films/nuevo-b.mkv")));
        QCOMPARE(rest.size(), 3);
    }

    void removes()
    {
        Playlist list;
        QAbstractItemModelTester tester(&list, QAbstractItemModelTester::FailureReportingMode::QtTest);
        list.set(films(4), 2);
        QSignalSpy gone(&list, &Playlist::currentRemoved);

        list.remove(0);
        QCOMPARE(list.current(), 1);
        QCOMPARE(list.currentPath(), QStringLiteral("/films/3.mkv"));
        QCOMPARE(gone.count(), 0);

        list.remove(2);
        QCOMPARE(list.currentPath(), QStringLiteral("/films/3.mkv"));
        QCOMPARE(gone.count(), 0);

        // The one playing: the next takes its place…
        list.append({QStringLiteral("/films/9.mkv")});
        list.remove(1);
        QCOMPARE(gone.count(), 1);
        QCOMPARE(list.currentPath(), QStringLiteral("/films/9.mkv"));
        // …and after the last there is none.
        list.remove(1);
        QCOMPARE(gone.count(), 2);
        QCOMPARE(list.current(), -1);
        QCOMPARE(list.count(), 1);

        list.remove(5);
        QCOMPARE(list.count(), 1);
    }

    void removedNeverComesBackInShuffle()
    {
        Playlist list;
        list.seed(11);
        list.set(films(6), 0);
        list.setShuffle(true);
        list.remove(3);
        while (list.next(false) >= 0) {
            list.setCurrent(list.next(false));
            QVERIFY(list.currentPath() != QStringLiteral("/films/4.mkv"));
            QVERIFY(!list.currentPath().isEmpty());
        }
    }

    void moves()
    {
        Playlist list;
        QAbstractItemModelTester tester(&list, QAbstractItemModelTester::FailureReportingMode::QtTest);
        list.set(films(4), 1);
        list.move(1, 3);
        QCOMPARE(list.paths(), (QStringList{QStringLiteral("/films/1.mkv"), QStringLiteral("/films/3.mkv"), QStringLiteral("/films/4.mkv"), QStringLiteral("/films/2.mkv")}));
        QCOMPARE(list.current(), 3);
        list.move(2, 0);
        QCOMPARE(list.path(0), QStringLiteral("/films/4.mkv"));
        QCOMPARE(list.currentPath(), QStringLiteral("/films/2.mkv"));
        list.move(0, 0);
        list.move(0, 9);
        QCOMPARE(list.path(0), QStringLiteral("/films/4.mkv"));
    }

    void clears()
    {
        Playlist list;
        list.set(films(2), 0);
        QSignalSpy gone(&list, &Playlist::currentRemoved);
        list.clear();
        QCOMPARE(list.count(), 0);
        QCOMPARE(list.current(), -1);
        QCOMPARE(gone.count(), 1);
        list.clear();
        QCOMPARE(gone.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestPlaylist)
#include "tst_playlist.moc"
