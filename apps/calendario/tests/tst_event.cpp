#include "event.h"

#include <QTest>

static QDateTime at(int y, int m, int d, int h = 0, int min = 0)
{
    return Event::at(QDate(y, m, d), QTime(h, min));
}

static Event make(const QDateTime &start, const QDateTime &end, Event::Repeat repeat = Event::Never)
{
    Event event;
    event.id = QStringLiteral("e");
    event.start = start;
    event.end = end;
    event.repeat = repeat;
    return event;
}

static QStringList days(const QList<QDateTime> &starts)
{
    QStringList out;
    for (const QDateTime &start : starts) {
        out << start.toString(QStringLiteral("yyyy-MM-dd"));
    }
    return out;
}

class TestEvent : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void onceTouchesOnlyItsOwnTime()
    {
        const Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10));
        QCOMPARE(event.starts(at(2026, 11, 10), at(2026, 11, 11)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 11), at(2026, 11, 12)).size(), 0);
        QCOMPARE(event.starts(at(2026, 11, 9), at(2026, 11, 10)).size(), 0);
        // Ending at ten is not being there at ten.
        QCOMPARE(event.starts(at(2026, 11, 10, 10), at(2026, 11, 10, 11)).size(), 0);
        QCOMPARE(event.starts(at(2026, 11, 10, 9, 59), at(2026, 11, 10, 11)).size(), 1);
    }

    void aMomentCountsWhereItFalls()
    {
        const Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 9));
        QCOMPARE(event.starts(at(2026, 11, 10), at(2026, 11, 11)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 10, 9), at(2026, 11, 10, 10)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 10, 8), at(2026, 11, 10, 9)).size(), 0);
    }

    void overnightTouchesBothDays()
    {
        const Event event = make(at(2026, 11, 10, 22), at(2026, 11, 11, 2));
        QCOMPARE(event.starts(at(2026, 11, 10), at(2026, 11, 11)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 11), at(2026, 11, 12)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 12), at(2026, 11, 13)).size(), 0);
    }

    void allDayEndsBeforeTheNextDay()
    {
        Event event = make(at(2026, 11, 10), at(2026, 11, 12));
        event.allDay = true;
        QCOMPARE(event.starts(at(2026, 11, 11), at(2026, 11, 12)).size(), 1);
        QCOMPARE(event.starts(at(2026, 11, 12), at(2026, 11, 13)).size(), 0);
    }

    void daily()
    {
        const Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10), Event::Daily);
        QCOMPARE(days(event.starts(at(2026, 11, 9), at(2026, 11, 13))), QStringList({"2026-11-10", "2026-11-11", "2026-11-12"}));
        // Far ahead costs no more than near.
        QCOMPARE(days(event.starts(at(2090, 1, 1), at(2090, 1, 3))), QStringList({"2090-01-01", "2090-01-02"}));
        QCOMPARE(event.starts(at(2020, 1, 1), at(2026, 11, 10)).size(), 0);
    }

    void everyOtherWeek()
    {
        Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10), Event::Weekly);
        event.interval = 2;
        QCOMPARE(days(event.starts(at(2026, 11, 1), at(2026, 12, 31))), QStringList({"2026-11-10", "2026-11-24", "2026-12-08", "2026-12-22"}));
        QCOMPARE(days(event.starts(at(2027, 1, 1), at(2027, 1, 31))), QStringList({"2027-01-05", "2027-01-19"}));
    }

    void monthlyOnThe31stSkipsShortMonths()
    {
        const Event event = make(at(2026, 1, 31, 9), at(2026, 1, 31, 10), Event::Monthly);
        QCOMPARE(days(event.starts(at(2026, 1, 1), at(2026, 9, 1))), QStringList({"2026-01-31", "2026-03-31", "2026-05-31", "2026-07-31", "2026-08-31"}));
        QCOMPARE(days(event.starts(at(2027, 2, 1), at(2027, 4, 1))), QStringList({"2027-03-31"}));
    }

    void yearlyOnLeapDay()
    {
        Event event = make(at(2024, 2, 29), at(2024, 3, 1), Event::Yearly);
        event.allDay = true;
        QCOMPARE(days(event.starts(at(2024, 1, 1), at(2033, 1, 1))), QStringList({"2024-02-29", "2028-02-29", "2032-02-29"}));
    }

    void untilIsTheLastDay()
    {
        Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10), Event::Daily);
        event.until = QDate(2026, 11, 12);
        QCOMPARE(days(event.starts(at(2026, 11, 1), at(2026, 12, 1))), QStringList({"2026-11-10", "2026-11-11", "2026-11-12"}));
        QCOMPARE(event.starts(at(2026, 11, 13), at(2027, 1, 1)).size(), 0);
    }

    void countIsCountedFromTheFirst()
    {
        Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10), Event::Weekly);
        event.count = 3;
        QCOMPARE(days(event.starts(at(2026, 11, 1), at(2027, 12, 1))), QStringList({"2026-11-10", "2026-11-17", "2026-11-24"}));
        QCOMPARE(days(event.starts(at(2026, 11, 20), at(2027, 12, 1))), QStringList({"2026-11-24"}));
        QCOMPARE(event.starts(at(2026, 11, 25), at(2027, 12, 1)).size(), 0);
        // A day taken out is still one of the three.
        event.skipped << QDate(2026, 11, 17);
        QCOMPARE(days(event.starts(at(2026, 11, 1), at(2027, 12, 1))), QStringList({"2026-11-10", "2026-11-24"}));
    }

    void skippedDaysAreLeftOut()
    {
        Event event = make(at(2026, 11, 10, 9), at(2026, 11, 10, 10), Event::Daily);
        event.skipped << QDate(2026, 11, 11);
        QCOMPARE(days(event.starts(at(2026, 11, 10), at(2026, 11, 13))), QStringList({"2026-11-10", "2026-11-12"}));
    }

    void aRepeatedOvernightReachesIntoTheRange()
    {
        const Event event = make(at(2026, 11, 10, 22), at(2026, 11, 11, 2), Event::Daily);
        // The 14th has the tail of the 13th's and the start of its own.
        QCOMPARE(days(event.starts(at(2026, 11, 14), at(2026, 11, 15))), QStringList({"2026-11-13", "2026-11-14"}));
    }

    void theClockChangeMovesNothing()
    {
        // 02:30 does not exist in Madrid on 28 March 2027; the event keeps its hour.
        const Event event = make(at(2027, 3, 27, 2, 30), at(2027, 3, 27, 3), Event::Daily);
        const QList<QDateTime> starts = event.starts(at(2027, 3, 27), at(2027, 3, 30));
        QCOMPARE(starts.size(), 3);
        for (const QDateTime &start : starts) {
            QCOMPARE(start.time(), QTime(2, 30));
        }
    }

    void jsonRoundTrip()
    {
        Event event = make(at(2026, 11, 10, 9, 30), at(2026, 11, 10, 11), Event::Monthly);
        event.calendar = QStringLiteral("trabajo");
        event.title = QStringLiteral("Revisión «trimestral»");
        event.location = QStringLiteral("Sala 2");
        event.notes = QStringLiteral("Línea 1\nLínea 2");
        event.interval = 3;
        event.until = QDate(2027, 11, 10);
        event.skipped << QDate(2027, 2, 10);
        event.reminder = 15;
        const Event back = Event::fromJson(event.toJson());
        QCOMPARE(back.id, event.id);
        QCOMPARE(back.calendar, event.calendar);
        QCOMPARE(back.title, event.title);
        QCOMPARE(back.location, event.location);
        QCOMPARE(back.notes, event.notes);
        QCOMPARE(back.start, event.start);
        QCOMPARE(back.end, event.end);
        QCOMPARE(back.allDay, false);
        QCOMPARE(back.repeat, Event::Monthly);
        QCOMPARE(back.interval, 3);
        QCOMPARE(back.until, event.until);
        QCOMPARE(back.skipped, event.skipped);
        QCOMPARE(back.reminder, 15);

        const Event plain = Event::fromJson(make(at(2026, 1, 1), at(2026, 1, 2)).toJson());
        QCOMPARE(plain.repeat, Event::Never);
        QCOMPARE(plain.reminder, -1);
    }

    void brokenJsonIsHarmless()
    {
        const Event event = Event::fromJson(QJsonObject{{QStringLiteral("id"), QStringLiteral("x")}, {QStringLiteral("start"), QStringLiteral("nonsense")}});
        QVERIFY(!event.start.isValid());
        QCOMPARE(event.starts(at(2026, 1, 1), at(2027, 1, 1)).size(), 0);
        // An end before the start is no length at all.
        const Event backwards = Event::fromJson(QJsonObject{{QStringLiteral("start"), QStringLiteral("2026-11-10T10:00")}, {QStringLiteral("end"), QStringLiteral("2026-11-10T09:00")}});
        QCOMPARE(backwards.seconds(), 0);
    }
};

QTEST_GUILESS_MAIN(TestEvent)
#include "tst_event.moc"
