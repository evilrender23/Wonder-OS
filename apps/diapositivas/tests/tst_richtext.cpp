#include "richtext.h"

#include <QJsonDocument>
#include <QTextBlock>
#include <QTextDocument>
#include <QtTest>

using namespace Model;

class TestRichText : public QObject
{
    Q_OBJECT

    static TextDefaults defaults()
    {
        TextDefaults d;
        d.font = QStringLiteral("DejaVu Sans");
        d.size = 24;
        d.color = QColor(0x20, 0x20, 0x20);
        d.align = 0;
        return d;
    }

    static QList<Paragraph> sample()
    {
        Run plain;
        plain.text = QStringLiteral("Hola ");
        Run strong;
        strong.text = QStringLiteral("mundo");
        strong.bold = 1;
        strong.italic = true;
        strong.underline = true;
        strong.strike = true;
        strong.size = 40;
        strong.color = QStringLiteral("#ff0000");
        strong.font = QStringLiteral("DejaVu Serif");
        Paragraph first;
        first.runs << plain << strong;
        Paragraph second;
        Run point;
        point.text = QStringLiteral("Un punto");
        second.runs << point;
        second.bullet = true;
        second.align = 2;
        return {first, second};
    }

private Q_SLOTS:
    void documentRoundTrip()
    {
        const Look look = Presentation().look();
        QTextDocument document;
        RichText::fill(document, sample(), defaults(), look, 400);
        QCOMPARE(document.blockCount(), 2);
        QCOMPARE(document.toPlainText(), QStringLiteral("Hola mundo\nUn punto"));
        QCOMPARE(RichText::fromDocument(document, defaults()), sample());
    }

    void htmlRoundTrip()
    {
        const Look look = Presentation().look();
        const QString html = RichText::toHtml(sample(), defaults(), look);
        QVERIFY(html.contains(QLatin1String("mundo")));
        const QList<Paragraph> back = RichText::fromHtml(html, defaults());
        if (back != sample()) {
            for (const Paragraph &p : back) qDebug().noquote() << QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact);
            for (const Paragraph &p : sample()) qDebug().noquote() << QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact);
            qDebug().noquote() << html;
        }
        QCOMPARE(back, sample());
    }

    void whatMatchesTheDefaultsStaysUnset()
    {
        const Look look = Presentation().look();
        Run run;
        run.text = QStringLiteral("Texto");
        Paragraph paragraph;
        paragraph.runs << run;
        QTextDocument document;
        RichText::fill(document, {paragraph}, defaults(), look, 0);
        const QList<Paragraph> back = RichText::fromDocument(document, defaults());
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.at(0).align, -1);
        QCOMPARE(back.at(0).runs.size(), 1);
        QCOMPARE(back.at(0).runs.at(0).bold, -1);
        QCOMPARE(back.at(0).runs.at(0).size, 0.0);
        QVERIFY(back.at(0).runs.at(0).color.isEmpty());
        QVERIFY(back.at(0).runs.at(0).font.isEmpty());
    }

    void themeColorsResolve()
    {
        Presentation deck;
        const Look look = deck.look();
        Run run;
        run.text = QStringLiteral("x");
        run.color = QStringLiteral("@accent1");
        Paragraph paragraph;
        paragraph.runs << run;
        QTextDocument document;
        RichText::fill(document, {paragraph}, defaults(), look, 0);
        const QTextCharFormat format = document.firstBlock().begin().fragment().charFormat();
        QCOMPARE(format.foreground().color(), look.accents.at(0));
    }

    void wrapsAtTheWidth()
    {
        const Look look = Presentation().look();
        Run run;
        run.text = QStringLiteral("palabra palabra palabra palabra palabra palabra palabra palabra");
        Paragraph paragraph;
        paragraph.runs << run;
        QTextDocument wide;
        RichText::fill(wide, {paragraph}, defaults(), look, 0);
        QTextDocument narrow;
        RichText::fill(narrow, {paragraph}, defaults(), look, 200);
        QVERIFY(narrow.size().height() > wide.size().height() * 2);
        QVERIFY(narrow.size().width() <= 201);
    }

    void tidying()
    {
        Run a;
        a.text = QStringLiteral("uno ");
        Run b;
        b.text = QStringLiteral("dos");
        Run empty;
        empty.bold = 1;
        Run c;
        c.text = QStringLiteral(" tres");
        c.bold = 1;
        Paragraph paragraph;
        paragraph.runs << a << empty << b << c;
        const QList<Paragraph> tidy = RichText::tidied({paragraph});
        QCOMPARE(tidy.size(), 1);
        QCOMPARE(tidy.at(0).runs.size(), 2);
        QCOMPARE(tidy.at(0).runs.at(0).text, QStringLiteral("uno dos"));
        QCOMPARE(tidy.at(0).runs.at(1).text, QStringLiteral(" tres"));
    }

    void emptyParagraphsSurvive()
    {
        const Look look = Presentation().look();
        Run run;
        run.text = QStringLiteral("a");
        Paragraph full;
        full.runs << run;
        QTextDocument document;
        RichText::fill(document, {full, Paragraph(), full}, defaults(), look, 0);
        QCOMPARE(document.blockCount(), 3);
        const QList<Paragraph> back = RichText::fromDocument(document, defaults());
        QCOMPARE(back.size(), 3);
        // An empty line keeps one run without text: the format it would be typed in.
        QCOMPARE(back.at(1).runs.size(), 1);
        QVERIFY(back.at(1).runs.at(0).text.isEmpty());
        QCOMPARE(back.at(0).runs.at(0).text, QStringLiteral("a"));
    }
};

QTEST_MAIN(TestRichText)
#include "tst_richtext.moc"
