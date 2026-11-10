#include "fileops.h"
#include "opsreport.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TestOpsReport : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static QJsonObject read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }

private Q_SLOTS:
    void aNewOperationIsWrittenAtOnce()
    {
        const QString path = m_dir.filePath(QStringLiteral("sub/once.json"));
        OpsReport report(path);
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(true), false);
        report.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), 0.25, QStringLiteral("1 of 4"));
        const QJsonObject state = read(path);
        QCOMPARE(state.value(QLatin1String("busy")).toBool(), true);
        QCOMPARE(state.value(QLatin1String("kind")).toString(), QStringLiteral("copy"));
        QCOMPARE(state.value(QLatin1String("status")).toString(), QStringLiteral("Copying…"));
        QCOMPARE(state.value(QLatin1String("progress")).toDouble(), 0.25);
        QCOMPARE(state.value(QLatin1String("detail")).toString(), QStringLiteral("1 of 4"));
        QVERIFY(state.value(QLatin1String("updated")).toDouble() > 0);
    }

    void progressAloneWaitsForTheInterval()
    {
        const QString path = m_dir.filePath(QStringLiteral("throttle.json"));
        OpsReport report(path);
        report.setInterval(300);
        report.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), 0.25, QString());
        report.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), 0.5, QString());
        report.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), 0.75, QString());
        QCOMPARE(read(path).value(QLatin1String("progress")).toDouble(), 0.25);
        QTRY_COMPARE_WITH_TIMEOUT(read(path).value(QLatin1String("progress")).toDouble(), 0.75, 3000);
    }

    void theEndIsWrittenAtOnce()
    {
        const QString path = m_dir.filePath(QStringLiteral("end.json"));
        OpsReport report(path);
        report.setInterval(60000);
        report.set(true, QStringLiteral("delete"), QStringLiteral("Deleting…"), 0.1, QString());
        report.set(true, QStringLiteral("delete"), QStringLiteral("Deleting…"), 0.9, QString());
        report.set(false, QString(), QString(), 0, QString());
        const QJsonObject state = read(path);
        QCOMPARE(state.value(QLatin1String("busy")).toBool(true), false);
        QVERIFY(!state.contains(QLatin1String("status")));
    }

    void whatADeadFinderLeftIsCleared()
    {
        const QString path = m_dir.filePath(QStringLiteral("stale.json"));
        {
            OpsReport crashed(path);
            crashed.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), 0.4, QString());
        }
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(true), false);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("{\"busy\":true,\"status\":\"Copying…\",\"progress\":0.4,\"updated\":1}");
        file.close();
        OpsReport report(path);
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(true), false);
    }

    void aRunningOperationKeepsSayingSo()
    {
        const QString path = m_dir.filePath(QStringLiteral("beat.json"));
        OpsReport report(path);
        report.set(true, QStringLiteral("copy"), QStringLiteral("Copying…"), -1, QString());
        const double first = read(path).value(QLatin1String("updated")).toDouble();
        QTRY_VERIFY_WITH_TIMEOUT(read(path).value(QLatin1String("updated")).toDouble() > first, 4000);
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(), true);
    }

    void aCopyIsReportedFromStartToEnd()
    {
        const QString path = m_dir.filePath(QStringLiteral("copy.json"));
        QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("from")));
        QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("to")));
        QFile source(m_dir.filePath(QStringLiteral("from/data.bin")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write(QByteArray(4 * 1024 * 1024, 'x'));
        source.close();

        FileOps ops;
        ops.reportTo(path);
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(true), false);
        ops.copyTo({QUrl::fromLocalFile(source.fileName())}, QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("to"))));
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(), true);
        QCOMPARE(read(path).value(QLatin1String("kind")).toString(), QStringLiteral("copy"));
        QTRY_VERIFY_WITH_TIMEOUT(!ops.busy(), 20000);
        QCOMPARE(read(path).value(QLatin1String("busy")).toBool(true), false);
        QVERIFY(QFile::exists(m_dir.filePath(QStringLiteral("to/data.bin"))));
    }
};

QTEST_MAIN(TestOpsReport)
#include "tst_opsreport.moc"
