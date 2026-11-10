#include "agenda.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

static int jd(int y, int m, int d)
{
    return int(QDate(y, m, d).toJulianDay());
}

static QVariantMap fields(Agenda &agenda, const QString &title, int day, int startMin, int endMin)
{
    QVariantMap map = agenda.blank(day, startMin);
    map.insert(QStringLiteral("title"), title);
    map.insert(QStringLiteral("endDay"), day);
    map.insert(QStringLiteral("endMin"), endMin);
    return map;
}

static QStringList titles(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &item : list) {
        out << item.toMap().value(QStringLiteral("title")).toString();
    }
    return out;
}

class TestAgenda : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_file = 0;
    QString fresh() { return m_dir.filePath(QStringLiteral("agenda-%1.json").arg(++m_file)); }

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("WONDERLAND_CALENDARIO_NOW", "2026-11-10T10:30");
    }

    void startsWithTwoCalendars()
    {
        Agenda agenda(fresh());
        QCOMPARE(agenda.calendars().size(), 2);
        QCOMPARE(agenda.defaultCalendar(), agenda.calendars().first().id);
        QCOMPARE(agenda.day(jd(2026, 11, 10)).size(), 0);
    }

    void aSavedEventIsThereAfterReopening()
    {
        const QString path = fresh();
        QString id;
        {
            Agenda agenda(path);
            QVariantMap map = fields(agenda, QStringLiteral("  Dentista "), jd(2026, 11, 10), 570, 615);
            map.insert(QStringLiteral("location"), QStringLiteral("Calle Mayor"));
            map.insert(QStringLiteral("reminder"), 30);
            map.insert(QStringLiteral("calendar"), QStringLiteral("trabajo"));
            const int before = agenda.revision();
            id = agenda.save(map);
            QVERIFY(!id.isEmpty());
            QVERIFY(agenda.revision() > before);
        }
        Agenda agenda(path);
        const QVariantMap back = agenda.event(id);
        QCOMPARE(back.value(QStringLiteral("title")).toString(), QStringLiteral("Dentista"));
        QCOMPARE(back.value(QStringLiteral("calendar")).toString(), QStringLiteral("trabajo"));
        QCOMPARE(back.value(QStringLiteral("startMin")).toInt(), 570);
        QCOMPARE(back.value(QStringLiteral("endMin")).toInt(), 615);
        QCOMPARE(back.value(QStringLiteral("reminder")).toInt(), 30);
        const QVariantList day = agenda.day(jd(2026, 11, 10));
        QCOMPARE(day.size(), 1);
        QCOMPARE(day.first().toMap().value(QStringLiteral("time")).toString(), QStringLiteral("9:30"));
        QCOMPARE(day.first().toMap().value(QStringLiteral("color")).toString(), agenda.calendar(QStringLiteral("trabajo")).value(QStringLiteral("color")).toString());
    }

    void savingAgainChangesTheSameEvent()
    {
        Agenda agenda(fresh());
        const QString id = agenda.save(fields(agenda, QStringLiteral("Uno"), jd(2026, 11, 10), 540, 600));
        QVariantMap map = agenda.event(id);
        map.insert(QStringLiteral("title"), QStringLiteral("Otro"));
        map.insert(QStringLiteral("startDay"), jd(2026, 11, 12));
        map.insert(QStringLiteral("endDay"), jd(2026, 11, 12));
        QCOMPARE(agenda.save(map), id);
        QCOMPARE(agenda.events().size(), 1);
        QCOMPARE(agenda.day(jd(2026, 11, 10)).size(), 0);
        QCOMPARE(titles(agenda.day(jd(2026, 11, 12))), QStringList{QStringLiteral("Otro")});
    }

    void anEndBeforeTheStartIsPutRight()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Al revés"), jd(2026, 11, 10), 600, 540);
        const QVariantMap back = agenda.event(agenda.save(map));
        QCOMPARE(back.value(QStringLiteral("startMin")).toInt(), 600);
        QCOMPARE(back.value(QStringLiteral("endMin")).toInt(), 660);
        map.insert(QStringLiteral("endDay"), jd(2026, 11, 1));
        QCOMPARE(agenda.event(agenda.save(map)).value(QStringLiteral("endDay")).toInt(), jd(2026, 11, 10));
    }

    void allDayRunsThroughItsLastDay()
    {
        Agenda agenda(fresh());
        QVariantMap map = agenda.blank(jd(2026, 12, 24), -1);
        map.insert(QStringLiteral("title"), QStringLiteral("Vacaciones"));
        map.insert(QStringLiteral("allDay"), true);
        map.insert(QStringLiteral("endDay"), jd(2026, 12, 26));
        const QString id = agenda.save(map);
        QCOMPARE(agenda.event(id).value(QStringLiteral("endDay")).toInt(), jd(2026, 12, 26));
        QCOMPARE(agenda.day(jd(2026, 12, 23)).size(), 0);
        QCOMPARE(agenda.day(jd(2026, 12, 27)).size(), 0);
        const QVariantMap first = agenda.day(jd(2026, 12, 24)).first().toMap();
        QCOMPARE(first.value(QStringLiteral("begins")).toBool(), true);
        QCOMPARE(first.value(QStringLiteral("ends")).toBool(), false);
        const QVariantMap last = agenda.day(jd(2026, 12, 26)).first().toMap();
        QCOMPARE(last.value(QStringLiteral("begins")).toBool(), false);
        QCOMPARE(last.value(QStringLiteral("ends")).toBool(), true);
        QCOMPARE(agenda.busy(jd(2026, 12, 22), 7), QVariantList({0, 0, 1, 1, 1, 0, 0}));
    }

    void aDayListsAllDayFirstThenByHour()
    {
        Agenda agenda(fresh());
        const int day = jd(2026, 11, 10);
        agenda.save(fields(agenda, QStringLiteral("Tarde"), day, 960, 1020));
        agenda.save(fields(agenda, QStringLiteral("Mañana"), day, 480, 540));
        QVariantMap whole = agenda.blank(day, -1);
        whole.insert(QStringLiteral("title"), QStringLiteral("Fiesta"));
        whole.insert(QStringLiteral("allDay"), true);
        agenda.save(whole);
        QCOMPARE(titles(agenda.day(day)), QStringList({"Fiesta", "Mañana", "Tarde"}));
    }

    void overnightIsCutAtMidnight()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Guardia"), jd(2026, 11, 10), 1320, 120);
        map.insert(QStringLiteral("endDay"), jd(2026, 11, 11));
        agenda.save(map);
        const QVariantMap first = agenda.day(jd(2026, 11, 10)).first().toMap();
        QCOMPARE(first.value(QStringLiteral("startMin")).toInt(), 1320);
        QCOMPARE(first.value(QStringLiteral("endMin")).toInt(), 1440);
        const QVariantMap second = agenda.day(jd(2026, 11, 11)).first().toMap();
        QCOMPARE(second.value(QStringLiteral("startMin")).toInt(), 0);
        QCOMPARE(second.value(QStringLiteral("endMin")).toInt(), 120);
        QCOMPARE(second.value(QStringLiteral("from")).toInt(), jd(2026, 11, 10));
        // Until midnight sharp is not the next day's business.
        QVariantMap dinner = fields(agenda, QStringLiteral("Cena"), jd(2026, 11, 20), 1260, 0);
        dinner.insert(QStringLiteral("endDay"), jd(2026, 11, 21));
        agenda.save(dinner);
        QCOMPARE(agenda.day(jd(2026, 11, 21)).size(), 0);
        QCOMPARE(agenda.day(jd(2026, 11, 20)).first().toMap().value(QStringLiteral("endMin")).toInt(), 1440);
    }

    void overlappingEventsShareTheWidth()
    {
        Agenda agenda(fresh());
        const int day = jd(2026, 11, 10);
        agenda.save(fields(agenda, QStringLiteral("A"), day, 540, 660));
        agenda.save(fields(agenda, QStringLiteral("B"), day, 600, 720));
        agenda.save(fields(agenda, QStringLiteral("C"), day, 660, 690));
        agenda.save(fields(agenda, QStringLiteral("D"), day, 900, 960));
        QHash<QString, QVariantMap> boxes;
        for (const QVariant &item : agenda.layout(day)) {
            boxes.insert(item.toMap().value(QStringLiteral("title")).toString(), item.toMap());
        }
        QCOMPARE(boxes.size(), 4);
        // A and B overlap; C starts as A ends and takes its column; D is alone.
        QCOMPARE(boxes[QStringLiteral("A")].value(QStringLiteral("column")).toInt(), 0);
        QCOMPARE(boxes[QStringLiteral("B")].value(QStringLiteral("column")).toInt(), 1);
        QCOMPARE(boxes[QStringLiteral("C")].value(QStringLiteral("column")).toInt(), 0);
        for (const char *name : {"A", "B", "C"}) {
            QCOMPARE(boxes[QLatin1String(name)].value(QStringLiteral("columns")).toInt(), 2);
        }
        QCOMPARE(boxes[QStringLiteral("D")].value(QStringLiteral("column")).toInt(), 0);
        QCOMPARE(boxes[QStringLiteral("D")].value(QStringLiteral("columns")).toInt(), 1);
    }

    void aShortEventStillTakesRoom()
    {
        Agenda agenda(fresh());
        const int day = jd(2026, 11, 10);
        agenda.save(fields(agenda, QStringLiteral("Aviso"), day, 540, 540));
        agenda.save(fields(agenda, QStringLiteral("Otro"), day, 550, 610));
        agenda.save(fields(agenda, QStringLiteral("Tarde"), day, 1430, 1435));
        const QVariantList boxes = agenda.layout(day);
        QCOMPARE(boxes.size(), 3);
        QCOMPARE(boxes.at(0).toMap().value(QStringLiteral("columns")).toInt(), 2);
        QCOMPARE(boxes.at(0).toMap().value(QStringLiteral("bottom")).toInt(), 570);
        // And never runs off the end of the day.
        QCOMPARE(boxes.at(2).toMap().value(QStringLiteral("bottom")).toInt(), 1440);
        QCOMPARE(boxes.at(2).toMap().value(QStringLiteral("top")).toInt(), 1410);
    }

    void hiddenCalendarsShowNothing()
    {
        Agenda agenda(fresh());
        const int day = jd(2026, 11, 10);
        agenda.save(fields(agenda, QStringLiteral("Mío"), day, 540, 600));
        QVariantMap work = fields(agenda, QStringLiteral("Reunión"), day, 660, 720);
        work.insert(QStringLiteral("calendar"), QStringLiteral("trabajo"));
        agenda.save(work);
        agenda.setVisible(QStringLiteral("trabajo"), false);
        QCOMPARE(titles(agenda.day(day)), QStringList{QStringLiteral("Mío")});
        QCOMPARE(agenda.search(QStringLiteral("reunion")).size(), 0);
        QCOMPARE(agenda.busy(day, 1), QVariantList{1});
        agenda.setVisible(QStringLiteral("trabajo"), true);
        QCOMPARE(agenda.day(day).size(), 2);
    }

    void removingACalendarTakesItsEvents()
    {
        Agenda agenda(fresh());
        const QString id = agenda.addCalendar(QStringLiteral(" Deporte "), QStringLiteral("#34c759"));
        QCOMPARE(agenda.calendar(id).value(QStringLiteral("name")).toString(), QStringLiteral("Deporte"));
        QVariantMap map = fields(agenda, QStringLiteral("Correr"), jd(2026, 11, 10), 420, 480);
        map.insert(QStringLiteral("calendar"), id);
        agenda.save(map);
        agenda.save(fields(agenda, QStringLiteral("Queda"), jd(2026, 11, 10), 600, 660));
        agenda.setDefaultCalendar(id);
        QVERIFY(agenda.removeCalendar(id));
        QCOMPARE(titles(agenda.day(jd(2026, 11, 10))), QStringList{QStringLiteral("Queda")});
        QVERIFY(agenda.defaultCalendar() != id);
        // The last one cannot go.
        QVERIFY(agenda.removeCalendar(QStringLiteral("trabajo")));
        QVERIFY(!agenda.removeCalendar(QStringLiteral("personal")));
        QCOMPARE(agenda.calendars().size(), 1);
        agenda.editCalendar(QStringLiteral("personal"), QStringLiteral("Casa"), QStringLiteral("not a colour"));
        QCOMPARE(agenda.calendar(QStringLiteral("personal")).value(QStringLiteral("name")).toString(), QStringLiteral("Casa"));
        QVERIFY(QColor::isValidColorName(agenda.calendar(QStringLiteral("personal")).value(QStringLiteral("color")).toString()));
    }

    void skippingOneLeavesTheRest()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Clase"), jd(2026, 11, 10), 1080, 1140);
        map.insert(QStringLiteral("repeat"), int(Event::Weekly));
        const QString id = agenda.save(map);
        agenda.skip(id, jd(2026, 11, 17));
        QCOMPARE(agenda.day(jd(2026, 11, 10)).size(), 1);
        QCOMPARE(agenda.day(jd(2026, 11, 17)).size(), 0);
        QCOMPARE(agenda.day(jd(2026, 11, 24)).size(), 1);
        // Editing the title keeps what was left out; moving the series forgets it.
        map = agenda.event(id);
        map.insert(QStringLiteral("title"), QStringLiteral("Clase de piano"));
        agenda.save(map);
        QCOMPARE(agenda.day(jd(2026, 11, 17)).size(), 0);
        map.insert(QStringLiteral("startDay"), jd(2026, 11, 3));
        map.insert(QStringLiteral("endDay"), jd(2026, 11, 3));
        agenda.save(map);
        QCOMPARE(agenda.day(jd(2026, 11, 17)).size(), 1);
        agenda.remove(id);
        QCOMPARE(agenda.day(jd(2026, 11, 24)).size(), 0);
    }

    void untilBeforeTheStartIsNoLimit()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Diario"), jd(2026, 11, 10), 540, 600);
        map.insert(QStringLiteral("repeat"), int(Event::Daily));
        map.insert(QStringLiteral("untilDay"), jd(2026, 11, 12));
        const QString id = agenda.save(map);
        QCOMPARE(agenda.day(jd(2026, 11, 12)).size(), 1);
        QCOMPARE(agenda.day(jd(2026, 11, 13)).size(), 0);
        map = agenda.event(id);
        map.insert(QStringLiteral("untilDay"), jd(2026, 11, 1));
        agenda.save(map);
        QCOMPARE(agenda.event(id).value(QStringLiteral("untilDay")).toInt(), 0);
        QCOMPARE(agenda.day(jd(2026, 11, 13)).size(), 1);
    }

    void searchMindsNeitherCaseNorAccents()
    {
        Agenda agenda(fresh());
        agenda.save(fields(agenda, QStringLiteral("Revisión médica"), jd(2026, 12, 1), 540, 600));
        QVariantMap past = fields(agenda, QStringLiteral("Cena"), jd(2025, 3, 1), 1260, 1320);
        past.insert(QStringLiteral("location"), QStringLiteral("Casa de la MÉDICA"));
        agenda.save(past);
        QVariantMap weekly = fields(agenda, QStringLiteral("Yoga"), jd(2026, 1, 6), 1080, 1140);
        weekly.insert(QStringLiteral("repeat"), int(Event::Weekly));
        weekly.insert(QStringLiteral("notes"), QStringLiteral("Esterilla"));
        agenda.save(weekly);
        QCOMPARE(titles(agenda.search(QStringLiteral("MEDICA"))), QStringList({"Cena", "Revisión médica"}));
        // A repeating event shows once, at its next time (today is 10 November 2026, a Tuesday).
        const QVariantList yoga = agenda.search(QStringLiteral(" esterilla "));
        QCOMPARE(yoga.size(), 1);
        QCOMPARE(yoga.first().toMap().value(QStringLiteral("startDay")).toInt(), jd(2026, 11, 10));
        QCOMPARE(agenda.search(QStringLiteral("  ")).size(), 0);
        QCOMPARE(agenda.search(QStringLiteral("nada de esto")).size(), 0);
    }

    void blankStartsAtARoundHour()
    {
        Agenda agenda(fresh());
        // Today (10:30): the next hour. Another day: nine.
        QCOMPARE(agenda.blank(jd(2026, 11, 10), -1).value(QStringLiteral("startMin")).toInt(), 660);
        QCOMPARE(agenda.blank(jd(2026, 11, 11), -1).value(QStringLiteral("startMin")).toInt(), 540);
        const QVariantMap late = agenda.blank(jd(2026, 11, 11), 1410);
        QCOMPARE(late.value(QStringLiteral("endDay")).toInt(), jd(2026, 11, 12));
        QCOMPARE(late.value(QStringLiteral("endMin")).toInt(), 30);
    }

    void importsAndExports()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Comida"), jd(2026, 11, 10), 840, 930);
        map.insert(QStringLiteral("repeat"), int(Event::Monthly));
        map.insert(QStringLiteral("reminder"), 15);
        const QString id = agenda.save(map);
        QVariantMap work = fields(agenda, QStringLiteral("Reunión"), jd(2026, 11, 11), 600, 660);
        work.insert(QStringLiteral("calendar"), QStringLiteral("trabajo"));
        agenda.save(work);

        const QString one = m_dir.filePath(QStringLiteral("personal.ics"));
        const QString all = m_dir.filePath(QStringLiteral("todo.ics"));
        QVERIFY(agenda.exportFile(one, QStringLiteral("personal")));
        QVERIFY(agenda.exportFile(all));
        QVERIFY(!agenda.exportFile(m_dir.filePath(QStringLiteral("no/such/dir/x.ics"))));

        Agenda other(fresh());
        QCOMPARE(other.importFile(one, QStringLiteral("trabajo")), 1);
        QCOMPARE(other.event(id).value(QStringLiteral("calendar")).toString(), QStringLiteral("trabajo"));
        QCOMPARE(other.event(id).value(QStringLiteral("reminder")).toInt(), 15);
        QCOMPARE(other.day(jd(2026, 12, 10)).size(), 1);
        // Reading the same file again adds nothing, and the event stays where it was put.
        QCOMPARE(other.importFile(all), 2);
        QCOMPARE(other.events().size(), 2);
        QCOMPARE(other.event(id).value(QStringLiteral("calendar")).toString(), QStringLiteral("trabajo"));

        QCOMPARE(other.importFile(m_dir.filePath(QStringLiteral("no-such.ics"))), -1);
        QFile junk(m_dir.filePath(QStringLiteral("junk.ics")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("just some text");
        junk.close();
        QCOMPARE(other.importFile(junk.fileName()), -1);
    }

    void remindersComeDueOnce()
    {
        Agenda agenda(fresh());
        QVariantMap map = fields(agenda, QStringLiteral("Tren"), jd(2026, 11, 10), 720, 780);
        map.insert(QStringLiteral("reminder"), 15);
        map.insert(QStringLiteral("location"), QStringLiteral("Atocha"));
        map.insert(QStringLiteral("repeat"), int(Event::Daily));
        agenda.save(map);
        agenda.save(fields(agenda, QStringLiteral("Sin aviso"), jd(2026, 11, 10), 720, 780));
        const auto real = [](int d, int h, int m) { return QDateTime(QDate(2026, 11, d), QTime(h, m)); };

        QCOMPARE(agenda.due(real(10, 11, 30), real(10, 11, 44)).size(), 0);
        const QList<Notice> notices = agenda.due(real(10, 11, 44), real(10, 11, 45));
        QCOMPARE(notices.size(), 1);
        QCOMPARE(notices.first().title, QStringLiteral("Tren"));
        QCOMPARE(notices.first().location, QStringLiteral("Atocha"));
        QCOMPARE(notices.first().start.time(), QTime(12, 0));
        QCOMPARE(agenda.due(real(10, 11, 45), real(10, 11, 46)).size(), 0);
        // Each day's occurrence has its own.
        QCOMPARE(agenda.due(real(10, 11, 46), real(12, 11, 46)).size(), 2);
        agenda.setVisible(QStringLiteral("personal"), false);
        QCOMPARE(agenda.due(real(10, 11, 44), real(10, 11, 45)).size(), 0);
    }

    void allDayRemindersCountFromNine()
    {
        Agenda agenda(fresh());
        QVariantMap map = agenda.blank(jd(2026, 11, 12), -1);
        map.insert(QStringLiteral("title"), QStringLiteral("Cumpleaños"));
        map.insert(QStringLiteral("allDay"), true);
        map.insert(QStringLiteral("reminder"), 1440);
        agenda.save(map);
        const auto real = [](int d, int h, int m) { return QDateTime(QDate(2026, 11, d), QTime(h, m)); };
        QCOMPARE(agenda.due(real(11, 8, 0), real(11, 8, 59)).size(), 0);
        QCOMPARE(agenda.due(real(11, 8, 59), real(11, 9, 0)).size(), 1);
        QCOMPARE(agenda.due(real(11, 9, 0), real(13, 9, 0)).size(), 0);
    }

    void aBrokenFileIsAnEmptyAgenda()
    {
        const QString path = fresh();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"calendars\":[{\"id\":\"a\",\"name\":\"A\",\"color\":\"zzz\"},{\"id\":\"a\"}],"
                   "\"events\":[{\"id\":\"1\",\"calendar\":\"gone\",\"title\":\"Huérfano\",\"start\":\"2026-11-10T09:00\",\"end\":\"2026-11-10T10:00\"},"
                   "{\"id\":\"1\",\"start\":\"2026-11-10T09:00\"},{\"id\":\"2\",\"start\":\"when\"},42],\"default\":\"gone\"}");
        file.close();
        Agenda agenda(path);
        QCOMPARE(agenda.calendars().size(), 1);
        QVERIFY(QColor::isValidColorName(agenda.calendars().first().color));
        QCOMPARE(agenda.defaultCalendar(), QStringLiteral("a"));
        QCOMPARE(agenda.events().size(), 1);
        QCOMPARE(agenda.events().first().calendar, QStringLiteral("a"));

        QFile garbage(fresh());
        QVERIFY(garbage.open(QIODevice::WriteOnly));
        garbage.write("\x00\x01not json");
        garbage.close();
        QCOMPARE(Agenda(garbage.fileName()).calendars().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestAgenda)
#include "tst_agenda.moc"
