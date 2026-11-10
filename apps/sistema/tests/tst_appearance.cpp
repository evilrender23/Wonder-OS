#include "appearance.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestAppearance : public QObject
{
    Q_OBJECT

private:
    static QJsonObject stored(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }

private Q_SLOTS:
    void cleansColours()
    {
        QCOMPARE(Appearance::cleanColor(QStringLiteral("#FF9F0A")), QStringLiteral("#ff9f0a"));
        QCOMPARE(Appearance::cleanColor(QStringLiteral(" ff9f0a ")), QStringLiteral("#ff9f0a"));
        QVERIFY(Appearance::cleanColor(QStringLiteral("orange")).isEmpty());
        QVERIFY(Appearance::cleanColor(QStringLiteral("#ff9f0a80")).isEmpty());
        QVERIFY(Appearance::cleanColor(QStringLiteral("#fff")).isEmpty());
        QVERIFY(Appearance::cleanColor(QStringLiteral("../../x")).isEmpty());
    }

    void startsPlainWithoutAFile()
    {
        QTemporaryDir dir;
        // A folder that isn't there yet is made, so the first save has where to land.
        Appearance appearance(dir.filePath(QStringLiteral("wonderland/appearance.json")));
        QVERIFY(appearance.folderTint().isEmpty());
        QVERIFY(appearance.setFolderTint(QStringLiteral("#32D74B")));
        QCOMPARE(appearance.folderTint(), QStringLiteral("#32d74b"));
        QCOMPARE(stored(appearance.path()).value(QLatin1String("folder_tint")).toString(), QStringLiteral("#32d74b"));
    }

    void keepsWhatElseTheFileHolds()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("appearance.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"folder_tint\": \"#ff453a\", \"other\": 7}");
        file.close();

        Appearance appearance(path);
        QCOMPARE(appearance.folderTint(), QStringLiteral("#ff453a"));
        QSignalSpy changed(&appearance, &Appearance::folderTintChanged);
        QVERIFY(appearance.setFolderTint(QString()));
        QCOMPARE(changed.count(), 1);
        QVERIFY(appearance.folderTint().isEmpty());
        QVERIFY(!stored(path).contains(QLatin1String("folder_tint")));
        QCOMPARE(stored(path).value(QLatin1String("other")).toInt(), 7);
    }

    void refusesWhatIsNotAColour()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("appearance.json"));
        Appearance appearance(path);
        QVERIFY(appearance.setFolderTint(QStringLiteral("#0a84ff")));
        QVERIFY(!appearance.setFolderTint(QStringLiteral("azul")));
        QVERIFY(!appearance.error().isEmpty());
        QCOMPARE(appearance.folderTint(), QStringLiteral("#0a84ff"));
        QVERIFY(appearance.setFolderTint(QStringLiteral("#0a84ff")));
        QVERIFY(appearance.error().isEmpty());
    }

    void aColourFromElsewhereIsCleaned()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("appearance.json"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"folder_tint\": \"red; rm -rf\"}");
        file.close();
        QVERIFY(Appearance(path).folderTint().isEmpty());
    }

    void seesAChangeMadeByAnotherProgram()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("appearance.json"));
        Appearance ours(path);
        Appearance theirs(path);
        QVERIFY(theirs.setFolderTint(QStringLiteral("#bf5af2")));
        QTRY_COMPARE_WITH_TIMEOUT(ours.folderTint(), QStringLiteral("#bf5af2"), 5000);
    }
};

QTEST_GUILESS_MAIN(TestAppearance)
#include "tst_appearance.moc"
