#include "places.h"

#include <QDir>
#include <QStandardPaths>
#include <QTest>

class TestPlaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // The Dock, and anything else that opens folders, hands over the folder the trash is kept in.
    void theTrashFolderIsTheTrash()
    {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QUrl trash(QStringLiteral("trash:/"));
        QCOMPARE(Places::canonical(QUrl::fromLocalFile(data + QStringLiteral("/Trash/files"))), trash);
        QCOMPARE(Places::canonical(QUrl::fromLocalFile(data + QStringLiteral("/Trash/files/"))), trash);
        QCOMPARE(Places::canonical(QUrl::fromLocalFile(data + QStringLiteral("/Trash"))), trash);
        QCOMPARE(Places::canonical(QUrl::fromLocalFile(data + QStringLiteral("/./Trash//files"))), trash);
    }

    void otherFoldersStayAsTheyAre()
    {
        const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        const QList<QUrl> urls{
            QUrl::fromLocalFile(QDir::homePath()),
            QUrl::fromLocalFile(data),
            QUrl::fromLocalFile(data + QStringLiteral("/Trash/info")),
            QUrl::fromLocalFile(data + QStringLiteral("/Trash/files/a folder")),
            QUrl::fromLocalFile(data + QStringLiteral("/Trashcan")),
            QUrl(QStringLiteral("trash:/")),
            QUrl(QStringLiteral("recents:/")),
            QUrl(),
        };
        for (const QUrl &url : urls) {
            QCOMPARE(Places::canonical(url), url);
        }
    }

    void systemMountPoints()
    {
        QVERIFY(Places::isSystemMountPoint(QStringLiteral("/boot")));
        QVERIFY(Places::isSystemMountPoint(QStringLiteral("/boot/efi")));
        QVERIFY(Places::isSystemMountPoint(QStringLiteral("/var/tmp")));
        QVERIFY(Places::isSystemMountPoint(QStringLiteral("/home")));
        QVERIFY(Places::isSystemMountPoint(QStringLiteral("/efi/")));
    }

    void disksTheUserBrowses()
    {
        // Not mounted yet: it has to show, waiting to be mounted.
        QVERIFY(!Places::isSystemMountPoint(QString()));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/")));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/mnt/datos2")));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/run/media/oz/Ventoy")));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/home/oz/disk")));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/bootleg")));
        QVERIFY(!Places::isSystemMountPoint(QStringLiteral("/variety")));
    }
};

QTEST_GUILESS_MAIN(TestPlaces)
#include "tst_places.moc"
