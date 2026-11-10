#include "versions.h"

#include <QTemporaryDir>
#include <QtTest>

class TestVersions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void keepsACopyPerSave()
    {
        QTemporaryDir dir;
        Versions versions(dir.path() + QStringLiteral("/v"));
        const QString document = QStringLiteral("/home/nadie/Charla.pptx");
        const QDateTime start(QDate(2026, 3, 1), QTime(10, 0));
        QVERIFY(versions.list(document).isEmpty());
        const QString first = versions.add(document, "uno", start);
        QVERIFY(!first.isEmpty());
        QVERIFY(first.endsWith(QLatin1String(".pptx")));
        const QString second = versions.add(document, "dos", start.addSecs(60));
        QVERIFY(!second.isEmpty());
        const QList<Versions::Entry> list = versions.list(document);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).file, second);
        QCOMPARE(list.at(0).when, start.addSecs(60));
        QCOMPARE(list.at(1).size, 3);
        QVERIFY(versions.owns(first));
        QVERIFY(!versions.owns(QStringLiteral("/etc/passwd")));
    }

    void skipsWhatDidNotChange()
    {
        QTemporaryDir dir;
        Versions versions(dir.path());
        const QString document = QStringLiteral("/a/b.pptx");
        QVERIFY(!versions.add(document, "uno").isEmpty());
        QVERIFY(versions.add(document, "uno").isEmpty());
        QVERIFY(versions.add(document, QByteArray()).isEmpty());
        QVERIFY(versions.add(QString(), "uno").isEmpty());
        QCOMPARE(versions.list(document).size(), 1);
    }

    void keepsOnlyTheNewest()
    {
        QTemporaryDir dir;
        Versions versions(dir.path(), 3);
        const QString document = QStringLiteral("/a/b.pptx");
        const QDateTime start(QDate(2026, 3, 1), QTime(10, 0));
        for (int i = 0; i < 6; ++i) {
            QVERIFY(!versions.add(document, QByteArray::number(i), start.addSecs(i)).isEmpty());
        }
        const QList<Versions::Entry> list = versions.list(document);
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).when, start.addSecs(5));
        QCOMPARE(list.at(2).when, start.addSecs(3));
    }

    void sameInstantIsStillTwo()
    {
        QTemporaryDir dir;
        Versions versions(dir.path());
        const QDateTime when(QDate(2026, 3, 1), QTime(10, 0));
        QVERIFY(!versions.add(QStringLiteral("/a/b.pptx"), "uno", when).isEmpty());
        QVERIFY(!versions.add(QStringLiteral("/a/b.pptx"), "dos", when).isEmpty());
        QCOMPARE(versions.list(QStringLiteral("/a/b.pptx")).size(), 2);
    }

    void eachDocumentHasItsOwn()
    {
        QTemporaryDir dir;
        Versions versions(dir.path());
        versions.add(QStringLiteral("/a/b.pptx"), "uno");
        versions.add(QStringLiteral("/a/c.odp"), "dos");
        QCOMPARE(versions.list(QStringLiteral("/a/b.pptx")).size(), 1);
        QCOMPARE(versions.list(QStringLiteral("/a/c.odp")).size(), 1);
        QVERIFY(versions.list(QStringLiteral("/a/c.odp")).at(0).file.endsWith(QLatin1String(".odp")));
        QVERIFY(versions.folder(QStringLiteral("/a/b.pptx")) != versions.folder(QStringLiteral("/a/c.odp")));
    }
};

QTEST_MAIN(TestVersions)
#include "tst_versions.moc"
