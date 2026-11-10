#include "app.h"

#include <KApplicationTrader>
#include <KService>
#include <KSycoca>

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>

class TestOpenWith : public QObject
{
    Q_OBJECT

    static void writeApp(const QString &id)
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + QLatin1Char('/') + id);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[Desktop Entry]\nType=Application\nName=" + id.toUtf8() + "\nExec=true %f\nMimeType=text/x-csrc;\n");
    }

    static QString mimeApps()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
    }

    // The service cache notices a rewritten mimeapps.list on its next check, not at once.
    static QString preferred(const QString &mime)
    {
        const KService::Ptr service = KApplicationTrader::preferredService(mime);
        return service ? service->storageId() : QString();
    }

private Q_SLOTS:
    void initTestCase()
    {
        // Keeps the test out of the user's own mimeapps.list.
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(mimeApps());
        writeApp(QStringLiteral("rabbit-test-a.desktop"));
        writeApp(QStringLiteral("rabbit-test-b.desktop"));
        KSycoca::self()->ensureCacheValid();
    }

    void cleanupTestCase()
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        QFile::remove(dir + QStringLiteral("/rabbit-test-a.desktop"));
        QFile::remove(dir + QStringLiteral("/rabbit-test-b.desktop"));
        QFile::remove(mimeApps());
    }

    void pickedAppBecomesTheDefault()
    {
        const QString mime = QStringLiteral("text/x-csrc");
        QVERIFY(App::setDefaultApp(mime, QStringLiteral("rabbit-test-b.desktop")));
        QTRY_COMPARE_WITH_TIMEOUT(preferred(mime), QStringLiteral("rabbit-test-b.desktop"), 10000);

        QVERIFY(App::setDefaultApp(mime, QStringLiteral("rabbit-test-a.desktop")));
        QTRY_COMPARE_WITH_TIMEOUT(preferred(mime), QStringLiteral("rabbit-test-a.desktop"), 10000);

        QFile file(mimeApps());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("text/x-csrc=rabbit-test-a.desktop"));
    }

    void unknownAppOrTypeChangesNothing()
    {
        const QString mime = QStringLiteral("text/x-csrc");
        QVERIFY(App::setDefaultApp(mime, QStringLiteral("rabbit-test-a.desktop")));
        QVERIFY(!App::setDefaultApp(mime, QStringLiteral("rabbit-test-missing.desktop")));
        QVERIFY(!App::setDefaultApp(QString(), QStringLiteral("rabbit-test-b.desktop")));
        QTRY_COMPARE_WITH_TIMEOUT(preferred(mime), QStringLiteral("rabbit-test-a.desktop"), 10000);
    }
};

QTEST_GUILESS_MAIN(TestOpenWith)
#include "tst_openwith.moc"
