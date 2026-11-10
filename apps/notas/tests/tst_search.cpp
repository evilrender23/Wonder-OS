#include "search.h"

#include <QTest>

class TestSearch : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void fold()
    {
        QCOMPARE(Search::fold(QStringLiteral("Canción ÑANDÚ über")), QStringLiteral("cancion nandu uber"));
        // One character out for each one in: positions stay.
        const QString text = QStringLiteral("AÉÍ ñ ç 日本");
        QCOMPARE(Search::fold(text).size(), text.size());
    }

    void matches()
    {
        const QString text = QStringLiteral("Reunión de planificación del trimestre");
        QVERIFY(Search::matches(text, QStringLiteral("reunion")));
        QVERIFY(Search::matches(text, QStringLiteral("TRIMESTRE planificacion")));
        QVERIFY(Search::matches(text, QStringLiteral("  plani  ")));
        QVERIFY(!Search::matches(text, QStringLiteral("reunion semanal")));
        QVERIFY(!Search::matches(text, QString()));
        QVERIFY(!Search::matches(text, QStringLiteral("   ")));
    }

    void find()
    {
        const QList<Search::Range> found = Search::find(QStringLiteral("El café y el CAFÉ"), QStringLiteral("cafe"));
        QCOMPARE(found, QList<Search::Range>({{3, 4}, {13, 4}}));
        // Words that overlap are one stretch.
        QCOMPARE(Search::find(QStringLiteral("planificación"), QStringLiteral("plani nifica")), QList<Search::Range>({{0, 9}}));
    }

    void marked()
    {
        QCOMPARE(Search::marked(QStringLiteral("Café <solo>"), QStringLiteral("cafe")), QStringLiteral("<b>Café</b> &lt;solo&gt;"));
        QCOMPARE(Search::marked(QStringLiteral("nada"), QStringLiteral("x")), QStringLiteral("nada"));
    }

    void snippet()
    {
        const QString text = QStringLiteral("uno dos tres cuatro cinco seis siete ocho nueve diez once doce trece catorce quince\ndieciséis diecisiete AQUÍ dieciocho diecinueve veinte");
        const QString cut = Search::snippet(text, QStringLiteral("aqui"), 40);
        QVERIFY(cut.startsWith(QStringLiteral("…")));
        QVERIFY(cut.contains(QStringLiteral("<b>AQUÍ</b>")));
        QVERIFY(!cut.contains(QLatin1Char('\n')));
        QVERIFY(cut.size() < 70);
        // A match at the beginning: from the beginning.
        QVERIFY(Search::snippet(text, QStringLiteral("uno"), 40).startsWith(QStringLiteral("<b>uno</b> dos")));
    }
};

QTEST_GUILESS_MAIN(TestSearch)
#include "tst_search.moc"
