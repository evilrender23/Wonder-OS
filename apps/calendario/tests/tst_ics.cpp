#include "ics.h"

#include <QTest>

static QDateTime at(int y, int m, int d, int h = 0, int min = 0)
{
    return Event::at(QDate(y, m, d), QTime(h, min));
}

class TestIcs : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reads()
    {
        const QByteArray data =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "X-WR-CALNAME:Casa\\, familia\r\n"
            "BEGIN:VTIMEZONE\r\nTZID:Europe/Madrid\r\nBEGIN:STANDARD\r\nDTSTART:19701025T030000\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:uno@example.org\r\n"
            "SUMMARY:Dentista\\; revisión\r\n"
            "LOCATION:Calle Mayor\\, 3\r\n"
            "DESCRIPTION:Llevar la tarjeta\\ny las radio\r\n"
            " grafías\r\n"
            "DTSTART;TZID=Europe/Madrid:20261110T093000\r\n"
            "DTEND;TZID=Europe/Madrid:20261110T101500\r\n"
            "RRULE:FREQ=MONTHLY;INTERVAL=6;UNTIL=20271110T225959Z\r\n"
            "EXDATE;TZID=Europe/Madrid:20270510T093000\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT30M\r\nEND:VALARM\r\n"
            "BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-P1D\r\nEND:VALARM\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:dos\r\n"
            "SUMMARY:Vacaciones\r\n"
            "DTSTART;VALUE=DATE:20261224\r\n"
            "DTEND;VALUE=DATE:20270107\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:tres\r\n"
            "SUMMARY:Llamada\r\n"
            "DTSTART:20260715T160000Z\r\n"
            "DURATION:PT1H30M\r\n"
            "RRULE:FREQ=WEEKLY;COUNT=4;BYDAY=WE\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:cuatro\r\n"
            "SUMMARY:Cumpleaños\r\n"
            "DTSTART;VALUE=DATE:20260301\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\nUID:sin-fecha\r\nSUMMARY:Nada\r\nEND:VEVENT\r\n"
            "END:VCALENDAR\r\n";
        const Ics::File file = Ics::parse(data);
        QCOMPARE(file.name, QStringLiteral("Casa, familia"));
        QCOMPARE(file.events.size(), 4);

        const Event &dentist = file.events.at(0);
        QCOMPARE(dentist.id, QStringLiteral("uno@example.org"));
        QCOMPARE(dentist.title, QStringLiteral("Dentista; revisión"));
        QCOMPARE(dentist.location, QStringLiteral("Calle Mayor, 3"));
        QCOMPARE(dentist.notes, QStringLiteral("Llevar la tarjeta\ny las radiografías"));
        QCOMPARE(dentist.start, at(2026, 11, 10, 9, 30));
        QCOMPARE(dentist.end, at(2026, 11, 10, 10, 15));
        QVERIFY(!dentist.allDay);
        QCOMPARE(dentist.repeat, Event::Monthly);
        QCOMPARE(dentist.interval, 6);
        QCOMPARE(dentist.until, QDate(2027, 11, 10));
        QCOMPARE(dentist.skipped, QList<QDate>{QDate(2027, 5, 10)});
        // The nearest of its alarms.
        QCOMPARE(dentist.reminder, 30);

        const Event &holidays = file.events.at(1);
        QVERIFY(holidays.allDay);
        QCOMPARE(holidays.start, at(2026, 12, 24));
        QCOMPARE(holidays.end, at(2027, 1, 7));
        QCOMPARE(holidays.reminder, -1);

        // 16:00 UTC in July is 18:00 in Madrid (the tests run there).
        const Event &call = file.events.at(2);
        QCOMPARE(call.start, at(2026, 7, 15, 18));
        QCOMPARE(call.end, at(2026, 7, 15, 19, 30));
        QCOMPARE(call.repeat, Event::Weekly);
        QCOMPARE(call.count, 4);

        // A date with no end is that whole day.
        const Event &birthday = file.events.at(3);
        QVERIFY(birthday.allDay);
        QCOMPARE(birthday.end, at(2026, 3, 2));
    }

    void readsBareLineFeedsAndNonsense()
    {
        QCOMPARE(Ics::parse("BEGIN:VEVENT\nUID:a\nSUMMARY:Uno\nDTSTART:20260101T100000\nEND:VEVENT\n").events.size(), 1);
        QCOMPARE(Ics::parse("no es un calendario").events.size(), 0);
        QCOMPARE(Ics::parse(QByteArray()).events.size(), 0);
        QCOMPARE(Ics::parse("BEGIN:VEVENT\nDTSTART:garbage\nEND:VEVENT\nBEGIN:VEVENT\nDTSTART:20260101T100000").events.size(), 0);
    }

    void roundTrip()
    {
        Event timed;
        timed.id = QStringLiteral("a-1");
        timed.title = QStringLiteral("Comida con Ana, Luis; y «los demás» — en un sitio con un nombre largo de verdad, muy largo");
        timed.location = QStringLiteral("Bar \\ Rincón");
        timed.notes = QStringLiteral("Uno\nDos");
        timed.start = at(2026, 11, 10, 14);
        timed.end = at(2026, 11, 10, 15, 30);
        timed.repeat = Event::Weekly;
        timed.interval = 2;
        timed.until = QDate(2027, 3, 1);
        timed.skipped << QDate(2026, 11, 24);
        timed.reminder = 15;

        Event whole;
        whole.id = QStringLiteral("a-2");
        whole.title = QStringLiteral("Puente");
        whole.allDay = true;
        whole.start = at(2026, 12, 5);
        whole.end = at(2026, 12, 9);
        whole.repeat = Event::Yearly;
        whole.count = 5;
        whole.skipped << QDate(2027, 12, 5);
        whole.reminder = 0;

        const QByteArray data = Ics::write({timed, whole}, QStringLiteral("Mío, y tuyo"));
        for (const QByteArray &line : data.split('\n')) {
            QVERIFY2(line.size() <= 76, line.constData());
        }
        QVERIFY(data.startsWith("BEGIN:VCALENDAR\r\n"));
        QVERIFY(data.endsWith("END:VCALENDAR\r\n"));

        const Ics::File file = Ics::parse(data);
        QCOMPARE(file.name, QStringLiteral("Mío, y tuyo"));
        QCOMPARE(file.events.size(), 2);
        const Event &a = file.events.at(0);
        QCOMPARE(a.id, timed.id);
        QCOMPARE(a.title, timed.title);
        QCOMPARE(a.location, timed.location);
        QCOMPARE(a.notes, timed.notes);
        QCOMPARE(a.start, timed.start);
        QCOMPARE(a.end, timed.end);
        QCOMPARE(a.allDay, false);
        QCOMPARE(a.repeat, Event::Weekly);
        QCOMPARE(a.interval, 2);
        QCOMPARE(a.until, timed.until);
        QCOMPARE(a.skipped, timed.skipped);
        QCOMPARE(a.reminder, 15);
        const Event &b = file.events.at(1);
        QCOMPARE(b.allDay, true);
        QCOMPARE(b.start, whole.start);
        QCOMPARE(b.end, whole.end);
        QCOMPARE(b.repeat, Event::Yearly);
        QCOMPARE(b.count, 5);
        QCOMPARE(b.skipped, whole.skipped);
        QCOMPARE(b.reminder, 0);
    }
};

QTEST_GUILESS_MAIN(TestIcs)
#include "tst_ics.moc"
