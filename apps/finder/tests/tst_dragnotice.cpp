#include "dragnotice.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestDragNotice : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    // A shell stand-in that is slow to answer and logs what it was told.
    QStringList slowShell(const QString &log)
    {
        const QString script = m_dir.filePath(QStringLiteral("shell.sh"));
        QFile file(script);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write("#!/bin/sh\nsleep 0.2\necho \"$2\" >> \"$1\"\n");
        }
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return {script, log};
    }

    static QByteArray said(const QString &log)
    {
        QFile file(log);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private Q_SLOTS:
    void startAndEndAreSaidInOrder()
    {
        const QString log = m_dir.filePath(QStringLiteral("order.log"));
        DragNotice notice;
        notice.setCommand(slowShell(log));
        notice.set(true);
        notice.set(false); // the drag ended before the shell heard it had started
        QTRY_VERIFY_WITH_TIMEOUT(notice.idle(), 5000);
        QCOMPARE(said(log), QByteArray("true\nfalse\n"));
    }

    void aStateTheShellAlreadyHasIsNotRepeated()
    {
        const QString log = m_dir.filePath(QStringLiteral("repeat.log"));
        DragNotice notice;
        notice.setCommand(slowShell(log));
        notice.set(false);
        QVERIFY(notice.idle());
        notice.set(true);
        notice.set(false);
        notice.set(true);
        QTRY_VERIFY_WITH_TIMEOUT(notice.idle(), 5000);
        QCOMPARE(said(log), QByteArray("true\n"));
    }

    void noShellIsNoError()
    {
        DragNotice notice;
        notice.setCommand({});
        notice.set(true);
        QVERIFY(notice.idle());
        notice.setCommand({m_dir.filePath(QStringLiteral("missing"))});
        notice.set(true);
        QTRY_VERIFY_WITH_TIMEOUT(notice.idle(), 5000);
    }
};

QTEST_MAIN(TestDragNotice)
#include "tst_dragnotice.moc"
