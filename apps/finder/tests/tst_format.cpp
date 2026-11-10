#include "format.h"

#include <QLocale>
#include <QTest>

class TestFormat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QLocale::setDefault(QLocale::c());
    }

    void size()
    {
        QCOMPARE(Format::size(0), QStringLiteral("Zero bytes"));
        QCOMPARE(Format::size(1), QStringLiteral("1 byte"));
        QCOMPARE(Format::size(999), QStringLiteral("999 bytes"));
        QCOMPARE(Format::size(12'400), QStringLiteral("12 KB"));
        QCOMPARE(Format::size(4'500'000), QStringLiteral("4.5 MB"));
        QCOMPARE(Format::size(2'000'000), QStringLiteral("2 MB"));
        QCOMPARE(Format::size(1'250'000'000), QStringLiteral("1.25 GB"));
        QCOMPARE(Format::size(4'040'000'000'000), QStringLiteral("4.04 TB"));
        // Never "1000 KB": a size rounds up into the next unit.
        QCOMPARE(Format::size(999'600), QStringLiteral("1 MB"));
    }

    void date()
    {
        const QDateTime now(QDate(2026, 11, 10), QTime(12, 0));
        QVERIFY(Format::date(QDateTime(QDate(2026, 11, 10), QTime(9, 5)), now).startsWith(QStringLiteral("Today, ")));
        QVERIFY(Format::date(QDateTime(QDate(2026, 11, 9), QTime(23, 59)), now).startsWith(QStringLiteral("Yesterday, ")));
        QVERIFY(Format::date(QDateTime(QDate(2025, 3, 12), QTime(18, 21)), now).startsWith(QStringLiteral("12 Mar 2025, ")));
        QCOMPARE(Format::date(QDateTime(), now), QStringLiteral("--"));
    }

    void uniqueName()
    {
        const QStringList taken{QStringLiteral("notes.txt"), QStringLiteral("notes copy.txt"), QStringLiteral("untitled folder"), QStringLiteral("backup.tar.gz")};
        QCOMPARE(Format::uniqueName(QStringLiteral("new.txt"), false, QString(), taken), QStringLiteral("new.txt"));
        QCOMPARE(Format::uniqueName(QStringLiteral("notes.txt"), false, QString(), taken), QStringLiteral("notes 2.txt"));
        QCOMPARE(Format::uniqueName(QStringLiteral("notes.txt"), false, QStringLiteral("copy"), taken), QStringLiteral("notes copy 2.txt"));
        QCOMPARE(Format::uniqueName(QStringLiteral("untitled folder"), true, QString(), taken), QStringLiteral("untitled folder 2"));
        // A folder's dot is part of its name; a double extension stays whole.
        QCOMPARE(Format::uniqueName(QStringLiteral("v1.2"), true, QStringLiteral("copy"), taken), QStringLiteral("v1.2 copy"));
        QCOMPARE(Format::uniqueName(QStringLiteral("backup.tar.gz"), false, QStringLiteral("copy"), taken), QStringLiteral("backup copy.tar.gz"));
        // Names differing only in case collide on some volumes.
        QCOMPARE(Format::uniqueName(QStringLiteral("Notes.txt"), false, QString(), taken), QStringLiteral("Notes 2.txt"));
    }
};

QTEST_GUILESS_MAIN(TestFormat)
#include "tst_format.moc"
