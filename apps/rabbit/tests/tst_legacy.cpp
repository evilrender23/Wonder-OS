#include "legacy.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestLegacy : public QObject
{
    Q_OBJECT

    static void write(const QString &path, const QByteArray &text)
    {
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private Q_SLOTS:
    void whatFinderKeptBecomesRabbits()
    {
        QTemporaryDir home;
        const QString config = home.path() + QStringLiteral("/config");
        const QString data = home.path() + QStringLiteral("/data");
        write(config + QStringLiteral("/wonderland/wonderland-finder.conf"), "favorites");
        write(data + QStringLiteral("/wonderland/wonderland-finder/tags.json"), "tags");

        Legacy::adopt(config, data);

        QCOMPARE(read(config + QStringLiteral("/wonderland/wonderland-rabbit.conf")), QByteArray("favorites"));
        QCOMPARE(read(data + QStringLiteral("/wonderland/wonderland-rabbit/tags.json")), QByteArray("tags"));
        QVERIFY(!QFileInfo::exists(config + QStringLiteral("/wonderland/wonderland-finder.conf")));
        QVERIFY(!QFileInfo::exists(data + QStringLiteral("/wonderland/wonderland-finder")));
    }

    void whatRabbitAlreadyHasStays()
    {
        QTemporaryDir home;
        const QString config = home.path() + QStringLiteral("/config");
        const QString data = home.path() + QStringLiteral("/data");
        write(config + QStringLiteral("/wonderland/wonderland-finder.conf"), "old");
        write(config + QStringLiteral("/wonderland/wonderland-rabbit.conf"), "new");
        write(data + QStringLiteral("/wonderland/wonderland-finder/tags.json"), "old");
        write(data + QStringLiteral("/wonderland/wonderland-rabbit/tags.json"), "new");

        Legacy::adopt(config, data);

        QCOMPARE(read(config + QStringLiteral("/wonderland/wonderland-rabbit.conf")), QByteArray("new"));
        QCOMPARE(read(data + QStringLiteral("/wonderland/wonderland-rabbit/tags.json")), QByteArray("new"));
        QCOMPARE(read(config + QStringLiteral("/wonderland/wonderland-finder.conf")), QByteArray("old"));
    }

    void nothingToAdoptIsFine()
    {
        QTemporaryDir home;
        Legacy::adopt(home.path() + QStringLiteral("/config"), home.path() + QStringLiteral("/data"));
        QVERIFY(QDir(home.path()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLegacy)
#include "tst_legacy.moc"
