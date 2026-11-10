#include "albums.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestAlbums : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void favorites()
    {
        QTemporaryDir dir;
        Albums albums(dir.filePath(QStringLiteral("albums.json")));
        QSignalSpy spy(&albums, &Albums::changed);
        QVERIFY(!albums.isFavorite(QStringLiteral("/a.jpg")));
        albums.toggleFavorite(QStringLiteral("/a.jpg"));
        albums.toggleFavorite(QStringLiteral("/b.jpg"));
        QVERIFY(albums.isFavorite(QStringLiteral("/a.jpg")));
        QCOMPARE(albums.favoriteCount(), 2);
        // The newest favourite first.
        QCOMPARE(albums.favorites(), (QStringList{QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")}));
        QCOMPARE(spy.count(), 2);

        QVERIFY(albums.allFavorite({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")}));
        QVERIFY(!albums.allFavorite({QStringLiteral("/a.jpg"), QStringLiteral("/c.jpg")}));
        QVERIFY(!albums.allFavorite({}));

        // Already as asked: nothing said.
        albums.setFavorite({QStringLiteral("/a.jpg")}, true);
        QCOMPARE(spy.count(), 2);
        albums.setFavorite({QStringLiteral("/a.jpg"), QStringLiteral("/c.jpg"), QString()}, true);
        QCOMPARE(albums.favoriteCount(), 3);
        QCOMPARE(spy.count(), 3);
        albums.setFavorite({QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")}, false);
        QCOMPARE(albums.favorites(), QStringList{QStringLiteral("/c.jpg")});
        albums.toggleFavorite(QStringLiteral("/c.jpg"));
        QCOMPARE(albums.favoriteCount(), 0);
    }

    void albums()
    {
        QTemporaryDir dir;
        Albums albums(dir.filePath(QStringLiteral("albums.json")));
        const QString trips = albums.create(QStringLiteral("  Viajes "), {QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg"), QStringLiteral("/a.jpg")});
        const QString empty = albums.create(QStringLiteral("Vacío"));
        QVERIFY(trips != empty);
        QCOMPARE(albums.name(trips), QStringLiteral("Viajes"));
        QCOMPARE(albums.paths(trips), (QStringList{QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")}));
        QCOMPARE(albums.name(QStringLiteral("no hay")), QString());
        QVERIFY(albums.paths(QStringLiteral("no hay")).isEmpty());

        const QVariantList list = albums.list();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Viajes"));
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("cover")).toString(), QStringLiteral("/a.jpg"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("cover")).toString(), QString());

        // Only what it did not have counts, and each once.
        QCOMPARE(albums.add(trips, {QStringLiteral("/b.jpg"), QStringLiteral("/c.jpg"), QStringLiteral("/c.jpg")}), 1);
        QCOMPARE(albums.add(trips, {QStringLiteral("/b.jpg")}), 0);
        QCOMPARE(albums.add(QStringLiteral("no hay"), {QStringLiteral("/b.jpg")}), 0);
        QCOMPARE(albums.paths(trips).size(), 3);
        albums.take(trips, {QStringLiteral("/a.jpg"), QStringLiteral("/z.jpg")});
        QCOMPARE(albums.paths(trips), (QStringList{QStringLiteral("/b.jpg"), QStringLiteral("/c.jpg")}));

        albums.rename(trips, QStringLiteral("Vacaciones"));
        QCOMPARE(albums.name(trips), QStringLiteral("Vacaciones"));
        // A name of nothing is no name.
        albums.rename(trips, QStringLiteral("   "));
        QCOMPARE(albums.name(trips), QStringLiteral("Vacaciones"));

        albums.remove(empty);
        QCOMPARE(albums.list().size(), 1);
        QCOMPARE(albums.name(empty), QString());
    }

    void persists()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("albums.json"));
        QString id;
        {
            Albums albums(file);
            albums.toggleFavorite(QStringLiteral("/a.jpg"));
            id = albums.create(QStringLiteral("Viajes"), {QStringLiteral("/a.jpg"), QStringLiteral("/b.jpg")});
        }
        Albums albums(file);
        QVERIFY(albums.isFavorite(QStringLiteral("/a.jpg")));
        QCOMPARE(albums.name(id), QStringLiteral("Viajes"));
        QCOMPARE(albums.paths(id).size(), 2);

        // Gone to the bin: out of every list.
        QSignalSpy spy(&albums, &Albums::changed);
        albums.forget({QStringLiteral("/a.jpg")});
        QCOMPARE(spy.count(), 1);
        QVERIFY(!albums.isFavorite(QStringLiteral("/a.jpg")));
        QCOMPARE(albums.favoriteCount(), 0);
        QCOMPARE(albums.paths(id), QStringList{QStringLiteral("/b.jpg")});
        albums.forget({QStringLiteral("/nunca.jpg")});
        QCOMPARE(spy.count(), 1);

        // A broken file is an empty start, not a crash.
        QFile broken(file);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ esto no es json");
        broken.close();
        Albums fresh(file);
        QCOMPARE(fresh.list().size(), 0);
        QCOMPARE(fresh.favoriteCount(), 0);
    }
};

QTEST_MAIN(TestAlbums)
#include "tst_albums.moc"
