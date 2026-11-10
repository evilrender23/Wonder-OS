#include "dragnotice.h"
#include "look.h"

#include <QTest>

class TestLook : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyTypeHasItsPreview_data()
    {
        QTest::addColumn<QString>("mime");
        QTest::addColumn<bool>("isDir");
        QTest::addColumn<QString>("kind");

        QTest::newRow("folder") << QStringLiteral("inode/directory") << true << QStringLiteral("folder");
        QTest::newRow("png") << QStringLiteral("image/png") << false << QStringLiteral("image");
        QTest::newRow("svg") << QStringLiteral("image/svg+xml") << false << QStringLiteral("image");
        QTest::newRow("gif") << QStringLiteral("image/gif") << false << QStringLiteral("animation");
        // No reader in Qt: its thumbnail is all there is to show.
        QTest::newRow("photoshop") << QStringLiteral("image/vnd.adobe.photoshop") << false << QStringLiteral("other");
        QTest::newRow("pdf") << QStringLiteral("application/pdf") << false << QStringLiteral("pdf");
        QTest::newRow("mp3") << QStringLiteral("audio/mpeg") << false << QStringLiteral("audio");
        QTest::newRow("mp4") << QStringLiteral("video/mp4") << false << QStringLiteral("video");
        QTest::newRow("plain") << QStringLiteral("text/plain") << false << QStringLiteral("text");
        QTest::newRow("markdown") << QStringLiteral("text/markdown") << false << QStringLiteral("text");
        QTest::newRow("json") << QStringLiteral("application/json") << false << QStringLiteral("text");
        // Text underneath, though filed under application/.
        QTest::newRow("shell") << QStringLiteral("application/x-shellscript") << false << QStringLiteral("text");
        QTest::newRow("php") << QStringLiteral("application/x-php") << false << QStringLiteral("text");
        QTest::newRow("zip") << QStringLiteral("application/zip") << false << QStringLiteral("other");
        QTest::newRow("binary") << QStringLiteral("application/octet-stream") << false << QStringLiteral("other");
        QTest::newRow("unknown") << QStringLiteral("application/x-never-heard-of") << false << QStringLiteral("other");
    }

    void everyTypeHasItsPreview()
    {
        QFETCH(QString, mime);
        QFETCH(bool, isDir);
        QFETCH(QString, kind);
        QCOMPARE(Look::kindFor(mime, isDir), kind);
    }

    void theDesktopIsToldByItsOwnCall()
    {
        const QStringList drag = DragNotice::shellCommand();
        const QStringList look = DragNotice::shellCommand(QStringLiteral("looking"));
        if (drag.isEmpty()) {
            QSKIP("no desktop shell here");
        }
        QCOMPARE(drag.last(), QStringLiteral("appDrag"));
        QCOMPARE(look.last(), QStringLiteral("looking"));
        QCOMPARE(look.mid(0, look.size() - 1), drag.mid(0, drag.size() - 1));
    }
};

QTEST_GUILESS_MAIN(TestLook)
#include "tst_look.moc"
