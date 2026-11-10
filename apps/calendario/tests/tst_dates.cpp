#include "dates.h"

#include <QTest>

class TestDates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writing()
    {
        const QDate date(2026, 11, 10);
        QCOMPARE(Dates::numeric(date), QStringLiteral("10/11/2026"));
        QCOMPARE(Dates::full(date), QStringLiteral("martes, 10 de noviembre de 2026"));
        QCOMPARE(Dates::medium(date, QDate(2026, 1, 1)), QStringLiteral("10 de noviembre"));
        QCOMPARE(Dates::medium(date, QDate(2027, 1, 1)), QStringLiteral("10 de noviembre de 2026"));
        QCOMPARE(Dates::weekday(7), QStringLiteral("domingo"));
        QCOMPARE(Dates::month(13), QString());
    }

    void readingDates_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QDate>("date");
        QTest::newRow("full") << "10/11/2026" << QDate(2026, 11, 10);
        QTest::newRow("padded") << " 05/03/2027 " << QDate(2027, 3, 5);
        QTest::newRow("short year") << "5-3-27" << QDate(2027, 3, 5);
        QTest::newRow("dots") << "5.3.2027" << QDate(2027, 3, 5);
        QTest::newRow("no year") << "24/12" << QDate(2026, 12, 24);
        QTest::newRow("iso") << "2026-02-28" << QDate(2026, 2, 28);
        QTest::newRow("30 feb") << "30/2/2026" << QDate();
        QTest::newRow("month 13") << "1/13/2026" << QDate();
        QTest::newRow("words") << "mañana" << QDate();
        QTest::newRow("empty") << "" << QDate();
    }
    void readingDates()
    {
        QFETCH(QString, text);
        QFETCH(QDate, date);
        QCOMPARE(Dates::parseDate(text, QDate(2026, 11, 10)), date);
    }

    void readingTimes_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("minute");
        QTest::newRow("hour") << "9" << 540;
        QTest::newRow("colon") << "9:30" << 570;
        QTest::newRow("padded") << " 09:05 " << 545;
        QTest::newRow("dot") << "18.45" << 1125;
        QTest::newRow("h") << "9h" << 540;
        QTest::newRow("h minutes") << "9h30" << 570;
        QTest::newRow("packed") << "0930" << 570;
        QTest::newRow("packed 3") << "930" << 570;
        QTest::newRow("pm") << "9:30 pm" << 1290;
        QTest::newRow("p.m.") << "9 p.m." << 1260;
        QTest::newRow("12 am") << "12 am" << 0;
        QTest::newRow("12 pm") << "12pm" << 720;
        QTest::newRow("midnight") << "0:00" << 0;
        QTest::newRow("end of day") << "24:00" << 1439;
        QTest::newRow("25") << "25:00" << -1;
        QTest::newRow("minute 60") << "9:60" << -1;
        QTest::newRow("words") << "tarde" << -1;
        QTest::newRow("empty") << "" << -1;
    }
    void readingTimes()
    {
        QFETCH(QString, text);
        QFETCH(int, minute);
        QCOMPARE(Dates::parseTime(text), minute);
    }
};

QTEST_GUILESS_MAIN(TestDates)
#include "tst_dates.moc"
