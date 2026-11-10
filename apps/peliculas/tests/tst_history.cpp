#include "history.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

static const qint64 kMinute = 60000;

class TestHistory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void whenAFilmIsFinished()
    {
        QVERIFY(!History::finished(50 * kMinute, 100 * kMinute));
        QVERIFY(History::finished(95 * kMinute, 100 * kMinute));
        QVERIFY(History::finished(100 * kMinute, 100 * kMinute));
        // A clip of two minutes: the last ten seconds.
        QVERIFY(!History::finished(105000, 120000));
        QVERIFY(History::finished(111000, 120000));
        QVERIFY(!History::finished(5000, 0));
    }

    void resumes()
    {
        History history{QString()};
        history.record(QStringLiteral("/a.mkv"), 30 * kMinute, 100 * kMinute, 1);
        QCOMPARE(history.resume(QStringLiteral("/a.mkv")), 30 * kMinute);
        QCOMPARE(history.duration(QStringLiteral("/a.mkv")), 100 * kMinute);
        QCOMPARE(history.resume(QStringLiteral("/never.mkv")), qint64(0));
        // Barely begun.
        history.record(QStringLiteral("/a.mkv"), 3000, 100 * kMinute, 2);
        QCOMPARE(history.resume(QStringLiteral("/a.mkv")), qint64(0));
        // Into the credits.
        history.record(QStringLiteral("/a.mkv"), 97 * kMinute, 100 * kMinute, 3);
        QCOMPARE(history.resume(QStringLiteral("/a.mkv")), qint64(0));
        // Too short to matter.
        history.record(QStringLiteral("/clip.mp4"), 20000, 45000, 4);
        QCOMPARE(history.resume(QStringLiteral("/clip.mp4")), qint64(0));
        // A position with the length not known yet keeps the length it had.
        history.record(QStringLiteral("/a.mkv"), 10 * kMinute, 0, 5);
        QCOMPARE(history.duration(QStringLiteral("/a.mkv")), 100 * kMinute);
        QCOMPARE(history.resume(QStringLiteral("/a.mkv")), 10 * kMinute);
        QCOMPARE(history.count(), 2);
    }

    void recentIsNewestFirstAndOnDisk()
    {
        QTemporaryDir dir;
        QStringList paths;
        for (const char *name : {"uno.mkv", "dos.mkv", "tres.mp3"}) {
            const QString path = dir.filePath(QString::fromLatin1(name));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            paths << path;
        }
        History history{QString()};
        history.record(paths.at(0), 30 * kMinute, 60 * kMinute, 10);
        history.record(paths.at(1), 59 * kMinute, 60 * kMinute, 20);
        history.record(dir.filePath(QStringLiteral("borrada.mkv")), 1000, 60 * kMinute, 30);
        history.record(paths.at(2), 0, 3 * kMinute, 40);
        // Seen again: back to the front.
        history.record(paths.at(0), 45 * kMinute, 60 * kMinute, 50);

        const QVariantList recent = history.recent(10);
        QCOMPARE(recent.size(), 3);
        QCOMPARE(recent.at(0).toMap().value(QStringLiteral("path")).toString(), paths.at(0));
        QCOMPARE(recent.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("uno"));
        QCOMPARE(recent.at(0).toMap().value(QStringLiteral("progress")).toDouble(), 0.75);
        QCOMPARE(recent.at(0).toMap().value(QStringLiteral("finished")).toBool(), false);
        QCOMPARE(recent.at(1).toMap().value(QStringLiteral("path")).toString(), paths.at(2));
        QCOMPARE(recent.at(1).toMap().value(QStringLiteral("audio")).toBool(), true);
        QCOMPARE(recent.at(2).toMap().value(QStringLiteral("finished")).toBool(), true);
        QCOMPARE(history.recent(2).size(), 2);

        const int before = history.revision();
        history.forget(paths.at(2));
        QCOMPARE(history.recent(10).size(), 2);
        QVERIFY(history.revision() > before);
        history.forget(QStringLiteral("/never.mkv"));
        history.clear();
        QCOMPARE(history.count(), 0);
        QVERIFY(history.recent(10).isEmpty());
    }

    void survivesARestart()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("deep/history.json"));
        {
            History history(file);
            history.record(QStringLiteral("/a.mkv"), 30 * kMinute, 100 * kMinute, 1);
            history.record(QStringLiteral("/b.mkv"), 40 * kMinute, 90 * kMinute, 2);
        }
        History again(file);
        QCOMPARE(again.count(), 2);
        QCOMPARE(again.resume(QStringLiteral("/a.mkv")), 30 * kMinute);
        QCOMPARE(again.resume(QStringLiteral("/b.mkv")), 40 * kMinute);

        // A file someone broke is an empty history, not a crash.
        QFile broken(file);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{\"items\": [1, {\"path\": 7}, {\"position\": 3}");
        broken.close();
        History empty(file);
        QCOMPARE(empty.count(), 0);
    }

    void forgetsTheOldest()
    {
        History history{QString()};
        for (int i = 0; i < 230; ++i) {
            history.record(QStringLiteral("/films/%1.mkv").arg(i), 10 * kMinute, 60 * kMinute, i);
        }
        QCOMPARE(history.count(), 200);
        QCOMPARE(history.resume(QStringLiteral("/films/0.mkv")), qint64(0));
        QCOMPARE(history.resume(QStringLiteral("/films/229.mkv")), 10 * kMinute);
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_history.moc"
