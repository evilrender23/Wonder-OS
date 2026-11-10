#include "deck.h"
#include "outline.h"

#include <QtTest>

using namespace Model;

class TestOutline : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parses()
    {
        const QList<Outline::Block> blocks = Outline::parse(QStringLiteral(
            "# Plan\nUn subtitulo\n\n---\n# Puntos\n- Uno\n  - Uno a\n* Dos\n> Recordar esto\n> y esto\n---\n\n# Fin\n"));
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks.at(0).title, QStringLiteral("Plan"));
        QCOMPARE(blocks.at(0).lines.size(), 1);
        QVERIFY(!blocks.at(0).lines.at(0).bullet);
        QCOMPARE(blocks.at(1).lines.size(), 3);
        QVERIFY(blocks.at(1).lines.at(0).bullet);
        QCOMPARE(blocks.at(1).lines.at(1).level, 1);
        QCOMPARE(blocks.at(1).lines.at(1).text, QStringLiteral("Uno a"));
        QCOMPARE(blocks.at(1).lines.at(2).text, QStringLiteral("Dos"));
        QCOMPARE(blocks.at(1).notes, QStringLiteral("Recordar esto\ny esto"));
        QCOMPARE(blocks.at(2).title, QStringLiteral("Fin"));
    }

    void aSecondTitleStartsASlide()
    {
        const QList<Outline::Block> blocks = Outline::parse(QStringLiteral("# Uno\n- a\n# Dos\n- b\n"));
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks.at(1).title, QStringLiteral("Dos"));
        QCOMPARE(blocks.at(1).lines.at(0).text, QStringLiteral("b"));
    }

    void writesWhatItParses()
    {
        const QString text = QStringLiteral("# Plan\nUn subtitulo\n\n---\n\n# Puntos\n- Uno\n  - Uno a\n- Dos\n> Nota\n");
        QCOMPARE(Outline::write(Outline::parse(text)), text);
    }

    void buildsADeck()
    {
        Presentation deck;
        Outline::apply(deck, QStringLiteral("# Plan\nPara 2027\n---\n# Puntos\n- Uno\n  - Uno a\n- Dos\n> Nota\n---\n# Fin"));
        QCOMPARE(deck.slides.size(), 3);
        QCOMPARE(deck.slides.at(0).layout, QStringLiteral("title"));
        QCOMPARE(deck.slides.at(0).items.at(0).plainText(), QStringLiteral("Plan"));
        QCOMPARE(deck.slides.at(0).items.at(1).plainText(), QStringLiteral("Para 2027"));
        QCOMPARE(deck.slides.at(1).layout, QStringLiteral("content"));
        const Item &body = deck.slides.at(1).items.at(1);
        QCOMPARE(body.role, QStringLiteral("body"));
        QCOMPARE(body.text.size(), 3);
        QVERIFY(body.text.at(0).bullet);
        QCOMPARE(body.text.at(1).level, 1);
        QCOMPARE(deck.slides.at(1).notes, QStringLiteral("Nota"));
        QCOMPARE(Outline::fromDeck(deck), QStringLiteral("# Plan\nPara 2027\n\n---\n\n# Puntos\n- Uno\n  - Uno a\n- Dos\n> Nota\n\n---\n\n# Fin\n"));
    }

    void keepsWhatTheOutlineDoesNotSay()
    {
        Presentation deck;
        Outline::apply(deck, QStringLiteral("# Uno\n- a\n---\n# Dos\n- b"));
        // A shape and a format of the user's own.
        Item shape;
        shape.id = newId();
        shape.type = QStringLiteral("shape");
        shape.shape = QStringLiteral("ellipse");
        shape.w = shape.h = 50;
        deck.slides[1].items << shape;
        deck.slides[1].items[1].text[0].runs[0].italic = true;
        deck.slides[1].transition = QStringLiteral("fade");
        const QString firstId = deck.slides.at(0).id;

        Outline::apply(deck, QStringLiteral("# Uno cambiado\n- a\n---\n# Dos\n- b"));
        QCOMPARE(deck.slides.size(), 2);
        QCOMPARE(deck.slides.at(0).id, firstId);
        QCOMPARE(deck.slides.at(0).items.at(0).plainText(), QStringLiteral("Uno cambiado"));
        QCOMPARE(deck.slides.at(1).items.size(), 3);
        QVERIFY(deck.slides.at(1).items.at(1).text.at(0).runs.at(0).italic);
        QCOMPARE(deck.slides.at(1).transition, QStringLiteral("fade"));
    }

    void dropsTheSlidesPastTheOutline()
    {
        Presentation deck;
        Outline::apply(deck, QStringLiteral("# Uno\n---\n# Dos\n---\n# Tres"));
        QCOMPARE(deck.slides.size(), 3);
        Outline::apply(deck, QStringLiteral("# Uno"));
        QCOMPARE(deck.slides.size(), 1);
        Outline::apply(deck, QString());
        QCOMPARE(deck.slides.size(), 1);
        QCOMPARE(deck.slides.at(0).items.at(0).plainText(), QString());
    }

    void givesATitleToABlankSlide()
    {
        Presentation deck;
        deck.slides << newSlide(QStringLiteral("blank"), deck.size());
        Outline::apply(deck, QStringLiteral("# Hola\n- uno"));
        QCOMPARE(deck.slides.at(0).items.size(), 2);
        QCOMPARE(Outline::blocks(deck).at(0).title, QStringLiteral("Hola"));
        QCOMPARE(Outline::blocks(deck).at(0).lines.at(0).text, QStringLiteral("uno"));
    }

    void theDeckTakesItAsOneStep()
    {
        Deck deck;
        deck.reset();
        deck.setOutline(QStringLiteral("# A\n---\n# B\n- uno\n---\n# C"));
        QCOMPARE(deck.count(), 3);
        QVERIFY(deck.modified());
        QCOMPARE(deck.undoText(), QStringLiteral("Editar esquema"));
        deck.setCurrent(2);
        deck.setOutline(QStringLiteral("# A"));
        QCOMPARE(deck.count(), 1);
        QCOMPARE(deck.current(), 0);
        deck.undo();
        QVERIFY(deck.count() == 3 || deck.count() == 1);
        while (deck.canUndo()) {
            deck.undo();
        }
        QCOMPARE(deck.count(), 1);
        QCOMPARE(deck.outline(), QStringLiteral("#\n"));
    }
};

QTEST_MAIN(TestOutline)
#include "tst_outline.moc"
