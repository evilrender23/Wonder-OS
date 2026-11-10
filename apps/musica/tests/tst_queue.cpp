#include "queue.h"

#include <QSignalSpy>
#include <QTest>

class TestQueue : public QObject
{
    Q_OBJECT

    static QStringList abc(int count = 5)
    {
        QStringList list;
        for (int i = 0; i < count; ++i) {
            list << QString(QChar('a' + i));
        }
        return list;
    }

private Q_SLOTS:
    void emptyQueueGoesNowhere()
    {
        Queue queue;
        QVERIFY(queue.isEmpty());
        QCOMPARE(queue.current(), QString());
        QVERIFY(!queue.next());
        QVERIFY(!queue.previous());
        QVERIFY(!queue.hasNext());
    }

    void playsInOrderAndStopsAtTheEnd()
    {
        Queue queue;
        queue.setTracks(abc(3), 1);
        QCOMPARE(queue.current(), QStringLiteral("b"));
        QVERIFY(queue.next());
        QCOMPARE(queue.current(), QStringLiteral("c"));
        QVERIFY(!queue.hasNext());
        QVERIFY(!queue.next());
        QCOMPARE(queue.current(), QStringLiteral("c"));
        QVERIFY(queue.previous());
        QVERIFY(queue.previous());
        QCOMPARE(queue.current(), QStringLiteral("a"));
        QVERIFY(!queue.previous());
    }

    void startOutOfRangeIsClamped()
    {
        Queue queue;
        queue.setTracks(abc(3), 99);
        QCOMPARE(queue.current(), QStringLiteral("c"));
        queue.setTracks(abc(3), -4);
        QCOMPARE(queue.current(), QStringLiteral("a"));
    }

    void repeatAllWrapsBothWays()
    {
        Queue queue;
        queue.setRepeat(Queue::Repeat::All);
        queue.setTracks(abc(2), 1);
        QVERIFY(queue.hasNext());
        QVERIFY(queue.next());
        QCOMPARE(queue.current(), QStringLiteral("a"));
        QVERIFY(queue.previous());
        QCOMPARE(queue.current(), QStringLiteral("b"));
    }

    void repeatOneReplaysOnlyWhenTheTrackEnds()
    {
        Queue queue;
        queue.setRepeat(Queue::Repeat::One);
        queue.setTracks(abc(3), 0);
        QVERIFY(queue.next(true));
        QCOMPARE(queue.current(), QStringLiteral("a"));
        // The button still moves on.
        QVERIFY(queue.next());
        QCOMPARE(queue.current(), QStringLiteral("b"));
    }

    void shuffleKeepsTheChosenTrackFirstAndEveryTrackOnce()
    {
        Queue queue;
        queue.seed(7);
        queue.setShuffle(true);
        queue.setTracks(abc(8), 3);
        QCOMPARE(queue.position(), 0);
        QCOMPARE(queue.current(), QStringLiteral("d"));
        QStringList items = queue.items();
        QVERIFY(items != abc(8));
        items.sort();
        QCOMPARE(items, abc(8));
    }

    void shufflingMidwayLeavesWhatPlayedAlone()
    {
        Queue queue;
        queue.seed(3);
        queue.setTracks(abc(8), 0);
        queue.next();
        queue.next();
        queue.setShuffle(true);
        QCOMPARE(queue.items().mid(0, 3), abc(3));
        QCOMPARE(queue.current(), QStringLiteral("c"));
        QStringList rest = queue.items().mid(3);
        rest.sort();
        QCOMPARE(rest, abc(8).mid(3));
    }

    void unshuffleRestoresTheOrderAndKeepsTheTrack()
    {
        Queue queue;
        queue.seed(11);
        queue.setShuffle(true);
        queue.setTracks(abc(6), 2);
        queue.next();
        const QString playing = queue.current();
        queue.setShuffle(false);
        QCOMPARE(queue.items(), abc(6));
        QCOMPARE(queue.current(), playing);
    }

    void playNextGoesRightAfterTheCurrentInOrder()
    {
        Queue queue;
        queue.setTracks(abc(3), 1);
        queue.playNext({QStringLiteral("x"), QStringLiteral("y")});
        QCOMPARE(queue.items(), QStringList({"a", "b", "x", "y", "c"}));
        QCOMPARE(queue.current(), QStringLiteral("b"));
        queue.append({QStringLiteral("z")});
        QCOMPARE(queue.items().last(), QStringLiteral("z"));
    }

    void addingToAnEmptyQueueSelectsTheFirst()
    {
        Queue queue;
        queue.append({QStringLiteral("x"), QStringLiteral("y")});
        QCOMPARE(queue.current(), QStringLiteral("x"));
        Queue other;
        other.playNext({QStringLiteral("p")});
        QCOMPARE(other.current(), QStringLiteral("p"));
    }

    void removingKeepsTheCurrentTrack()
    {
        Queue queue;
        queue.setTracks(abc(5), 2);
        queue.removeAt(0);
        QCOMPARE(queue.current(), QStringLiteral("c"));
        QCOMPARE(queue.position(), 1);
        queue.removeAt(3);
        QCOMPARE(queue.items(), QStringList({"b", "c", "d"}));
        // The playing track is not removable.
        queue.removeAt(queue.position());
        QCOMPARE(queue.size(), 3);
    }

    void removingFromAShuffledQueueKeepsTheRest()
    {
        Queue queue;
        queue.seed(5);
        queue.setShuffle(true);
        queue.setTracks(abc(6), 0);
        QStringList expected = queue.items();
        expected.removeAt(4);
        queue.removeAt(4);
        QCOMPARE(queue.items(), expected);
        queue.setShuffle(false);
        QCOMPARE(queue.size(), 5);
    }

    void clearUpcomingKeepsWhatPlayed()
    {
        Queue queue;
        queue.setTracks(abc(5), 1);
        queue.clearUpcoming();
        QCOMPARE(queue.items(), QStringList({"a", "b"}));
        QCOMPARE(queue.current(), QStringLiteral("b"));
        QVERIFY(!queue.hasNext());
    }

    void aShuffledRoundDoesNotRepeatItsLastTrackFirst()
    {
        for (quint32 seed = 0; seed < 40; ++seed) {
            Queue queue;
            queue.seed(seed);
            queue.setShuffle(true);
            queue.setRepeat(Queue::Repeat::All);
            queue.setTracks(abc(4), 0);
            queue.jumpTo(3);
            const QString last = queue.current();
            QVERIFY(queue.next());
            QCOMPARE(queue.position(), 0);
            QVERIFY(queue.current() != last);
        }
    }

    void signalsTellWhatChanged()
    {
        Queue queue;
        QSignalSpy items(&queue, &Queue::itemsChanged);
        QSignalSpy position(&queue, &Queue::positionChanged);
        QSignalSpy mode(&queue, &Queue::modeChanged);
        queue.setTracks(abc(3));
        QCOMPARE(items.count(), 1);
        QCOMPARE(position.count(), 1);
        queue.next();
        QCOMPARE(position.count(), 2);
        queue.setRepeat(Queue::Repeat::All);
        queue.setRepeat(Queue::Repeat::All);
        QCOMPARE(mode.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestQueue)
#include "tst_queue.moc"
