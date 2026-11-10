#include "model.h"

#include <QtTest>

using namespace Model;

class TestModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void themesAndLayouts()
    {
        QVERIFY(themes().size() >= 6 && themes().size() <= 8);
        QSet<QString> ids;
        for (const Theme &t : themes()) {
            ids << t.id;
            QVERIFY(QColor::fromString(t.bg1).isValid());
            QVERIFY(QColor::fromString(t.title).isValid());
            QCOMPARE(t.accents.size(), 6);
            QVERIFY(!t.titleFont.isEmpty() && !t.bodyFont.isEmpty());
        }
        QCOMPARE(ids.size(), themes().size());
        // An unknown theme is the first.
        QCOMPARE(theme(QStringLiteral("nope")).id, themes().first().id);
        QCOMPARE(layouts(), QStringList({QStringLiteral("title"), QStringLiteral("content"), QStringLiteral("columns"),
                                         QStringLiteral("titleOnly"), QStringLiteral("blank"), QStringLiteral("picture")}));
        for (const QString &layout : layouts()) {
            QVERIFY(!layoutName(layout).isEmpty());
        }
    }

    void layoutPlaceholders()
    {
        const QSizeF size(960, 540);
        QCOMPARE(layoutItems(QStringLiteral("title"), size).size(), 2);
        QCOMPARE(layoutItems(QStringLiteral("columns"), size).size(), 3);
        QCOMPARE(layoutItems(QStringLiteral("blank"), size).size(), 0);
        const Slide slide = newSlide(QStringLiteral("content"), size);
        QCOMPARE(slide.layout, QStringLiteral("content"));
        QCOMPARE(slide.items.at(0).role, QStringLiteral("title"));
        QCOMPARE(slide.items.at(1).role, QStringLiteral("body"));
        QVERIFY(!slide.items.at(0).id.isEmpty());
        QVERIFY(slide.items.at(0).id != slide.items.at(1).id);
        for (const Item &item : slide.items) {
            QVERIFY(QRectF(0, 0, 960, 540).contains(item.rect()));
            QVERIFY(!prompt(item).isEmpty());
        }
    }

    void changingLayoutKeepsText()
    {
        const QSizeF size(960, 540);
        Slide slide = newSlide(QStringLiteral("content"), size);
        slide.items[0].setPlainText(QStringLiteral("El título"));
        slide.items[1].setPlainText(QStringLiteral("Uno\nDos"));
        Item extra;
        extra.id = newId();
        extra.type = QStringLiteral("shape");
        extra.shape = QStringLiteral("rect");
        extra.setRect(QRectF(10, 10, 50, 50));
        slide.items << extra;

        applyLayout(slide, QStringLiteral("columns"), size);
        QCOMPARE(slide.layout, QStringLiteral("columns"));
        QStringList roles;
        for (const Item &item : slide.items) {
            roles << item.role;
        }
        QCOMPARE(roles.count(QStringLiteral("title")), 1);
        QCOMPARE(roles.count(QStringLiteral("body")), 2);
        QCOMPARE(roles.count(QString()), 1);
        QString title;
        QStringList bodies;
        for (const Item &item : slide.items) {
            if (item.role == QLatin1String("title")) title = item.plainText();
            if (item.role == QLatin1String("body")) bodies << item.plainText();
        }
        QCOMPARE(title, QStringLiteral("El título"));
        QVERIFY(bodies.contains(QStringLiteral("Uno\nDos")));

        // To a layout without a body: what was written is not thrown away.
        applyLayout(slide, QStringLiteral("titleOnly"), size);
        bool kept = false;
        for (const Item &item : slide.items) {
            kept = kept || item.plainText() == QLatin1String("Uno\nDos");
        }
        QVERIFY(kept);
    }

    void jsonRoundTrip()
    {
        Item item;
        item.id = QStringLiteral("abc");
        item.type = QStringLiteral("shape");
        item.shape = QStringLiteral("star");
        item.setRect(QRectF(10.5, 20, 300, 200));
        item.rotation = 33;
        item.flipH = true;
        item.fill = QStringLiteral("@accent2");
        item.stroke = QStringLiteral("#112233");
        item.strokeWidth = 2.5;
        item.shadow = true;
        item.opacity = 0.4;
        item.valign = 1;
        item.group = QStringLiteral("g1");
        item.anim = QStringLiteral("slide");
        Run run;
        run.text = QStringLiteral("Hola «mundo»");
        run.bold = 1;
        run.italic = true;
        run.size = 31;
        run.color = QStringLiteral("#ff0000");
        run.font = QStringLiteral("Georgia");
        Paragraph paragraph;
        paragraph.runs << run << Run{QStringLiteral(" y más")};
        paragraph.align = 2;
        paragraph.bullet = true;
        paragraph.level = 1;
        item.text << paragraph;
        QCOMPARE(Item::fromJson(item.toJson()), item);

        Item table;
        table.id = QStringLiteral("t");
        table.type = QStringLiteral("table");
        table.rows = 2;
        table.cols = 2;
        table.cells = {QStringLiteral("a"), QStringLiteral("b"), QString(), QStringLiteral("d")};
        QCOMPARE(Item::fromJson(table.toJson()), table);
        QCOMPARE(table.cell(1, 1), QStringLiteral("d"));

        Slide slide = newSlide(QStringLiteral("content"), QSizeF(960, 540));
        slide.items << item << table;
        slide.notes = QStringLiteral("Recordar\nsonreír");
        slide.hidden = true;
        slide.background = QStringLiteral("#000000");
        slide.transition = QStringLiteral("push");
        QCOMPARE(Slide::fromJson(slide.toJson()), slide);

        Master master;
        master.footer = QStringLiteral("Pie");
        master.numbers = true;
        master.titleFont = QStringLiteral("Inter");
        QCOMPARE(Master::fromJson(master.toJson()), master);
    }

    void plainText()
    {
        Item item;
        item.type = QStringLiteral("text");
        QVERIFY(!item.hasText());
        item.setPlainText(QStringLiteral("Uno\nDos\n\nCuatro"));
        QCOMPARE(item.text.size(), 4);
        QCOMPARE(item.plainText(), QStringLiteral("Uno\nDos\n\nCuatro"));
        QVERIFY(item.hasText());
        // The format of what was there stays.
        item.text[0].runs[0].bold = 1;
        item.text[0].align = 1;
        item.setPlainText(QStringLiteral("Otro"));
        QCOMPARE(item.text.size(), 1);
        QCOMPARE(item.text.at(0).runs.at(0).bold, 1);
        QCOMPARE(item.text.at(0).align, 1);
    }

    void geometry()
    {
        Item item;
        item.setRect(QRectF(100, 100, 200, 100));
        QCOMPARE(item.center(), QPointF(200, 150));
        QCOMPARE(item.bounds(), QRectF(100, 100, 200, 100));
        QVERIFY(item.hit(QPointF(110, 110)));
        QVERIFY(!item.hit(QPointF(90, 110)));
        QVERIFY(item.hit(QPointF(95, 110), 6));
        item.rotation = 90;
        const QRectF turned = item.bounds();
        QVERIFY(qAbs(turned.width() - 100) < 1e-6);
        QVERIFY(qAbs(turned.height() - 200) < 1e-6);
        QVERIFY(qAbs(turned.center().x() - 200) < 1e-6);
        // The corner of the upright box is no longer on the item.
        QVERIFY(!item.hit(QPointF(110, 110)));
        QVERIFY(item.hit(QPointF(200, 60)));

        Item line;
        line.type = QStringLiteral("shape");
        line.shape = QStringLiteral("line");
        line.setRect(QRectF(0, 0, 100, 100));
        QCOMPARE(line.line(), QLineF(0, 0, 100, 100));
        line.flipV = true;
        QCOMPARE(line.line(), QLineF(0, 100, 100, 0));
        // On a line, only near the stroke counts.
        QVERIFY(line.hit(QPointF(50, 50), 4));
        QVERIFY(!line.hit(QPointF(10, 10), 4));

        Item other;
        other.setRect(QRectF(400, 0, 100, 50));
        Item first;
        first.setRect(QRectF(100, 100, 200, 100));
        QCOMPARE(bounds({first, other}), QRectF(100, 0, 400, 200));
    }

    void snapping()
    {
        const QSizeF slide(960, 540);
        // Near the centre of the slide: pulled onto it, with both guides.
        Snap s = snap(QRectF(382, 222, 200, 100), {}, slide, 6);
        QCOMPARE(s.dx, -2.0);
        QCOMPARE(s.dy, -2.0);
        QCOMPARE(s.guides.size(), 2);
        // Far from everything.
        s = snap(QRectF(133, 77, 200, 100), {}, slide, 6);
        QCOMPARE(s.dx, 0.0);
        QCOMPARE(s.dy, 0.0);
        QVERIFY(s.guides.isEmpty());
        // The left edge onto a neighbour's left edge.
        s = snap(QRectF(103, 300, 50, 50), {QRectF(100, 50, 80, 80)}, slide, 6);
        QCOMPARE(s.dx, -3.0);
        QCOMPARE(s.dy, 0.0);
        QCOMPARE(s.guides.size(), 1);
        QCOMPARE(s.guides.first().x1(), 100.0);
        QCOMPARE(s.guides.first().x2(), 100.0);
        // No threshold, no snapping.
        s = snap(QRectF(382, 222, 200, 100), {}, slide, 0);
        QCOMPARE(s.dx, 0.0);
        QVERIFY(s.guides.isEmpty());
    }

    void resizing()
    {
        const QRectF box(100, 100, 200, 100);
        QCOMPARE(resized(box, 0, QStringLiteral("se"), QPointF(50, 20), false), QRectF(100, 100, 250, 120));
        QCOMPARE(resized(box, 0, QStringLiteral("nw"), QPointF(50, 20), false), QRectF(150, 120, 150, 80));
        QCOMPARE(resized(box, 0, QStringLiteral("e"), QPointF(50, 20), false), QRectF(100, 100, 250, 100));
        QCOMPARE(resized(box, 0, QStringLiteral("n"), QPointF(50, -20), false), QRectF(100, 80, 200, 120));
        // Never thinner than the minimum.
        QCOMPARE(resized(box, 0, QStringLiteral("e"), QPointF(-500, 0), false).width(), 6.0);
        // Keeping the proportions.
        const QRectF kept = resized(box, 0, QStringLiteral("se"), QPointF(100, 0), true);
        QVERIFY(qAbs(kept.width() / kept.height() - 2) < 1e-6);
        QCOMPARE(kept.topLeft(), box.topLeft());
        // Turned a quarter: dragging the east handle down the slide widens it,
        // and the middle of the west side stays where it was.
        const QRectF turned = resized(box, 90, QStringLiteral("e"), QPointF(0, 40), false);
        QVERIFY(qAbs(turned.width() - 240) < 1e-6);
        QVERIFY(qAbs(turned.height() - 100) < 1e-6);
        const auto west = [](const QRectF &r) {
            QTransform t;
            t.translate(r.center().x(), r.center().y());
            t.rotate(90);
            return t.map(QPointF(-r.width() / 2, 0));
        };
        QVERIFY(QLineF(west(box), west(turned)).length() < 1e-6);
    }

    void lookAndColors()
    {
        Presentation deck;
        deck.theme = QStringLiteral("negro");
        Look look = deck.look();
        QCOMPARE(look.bg1, QColor::fromString(theme(QStringLiteral("negro")).bg1));
        QCOMPARE(look.accents.size(), 6);
        QCOMPARE(look.color(QStringLiteral("#123456")), QColor(0x12, 0x34, 0x56));
        QCOMPARE(look.color(QStringLiteral("@accent3")), look.accents.at(2));
        QCOMPARE(look.color(QStringLiteral("@text")), look.text);
        QCOMPARE(look.color(QStringLiteral("@title")), look.title);
        QCOMPARE(look.color(QStringLiteral("@bg")), look.bg1);
        QVERIFY(!look.color(QString()).isValid());
        // The master's changes win over the theme.
        deck.master.background = QStringLiteral("#102030");
        deck.master.titleColor = QStringLiteral("#ff0000");
        deck.master.bodyFont = QStringLiteral("Georgia");
        look = deck.look();
        QCOMPARE(look.bg1, QColor(0x10, 0x20, 0x30));
        QCOMPARE(look.bg2, QColor(0x10, 0x20, 0x30));
        QCOMPARE(look.title, QColor(Qt::red));
        QCOMPARE(look.bodyFont, QStringLiteral("Georgia"));
        // Another theme, for the picker's previews.
        QCOMPARE(deck.look(QStringLiteral("blanco")).text, QColor::fromString(theme(QStringLiteral("blanco")).text));
    }

    void defaultsByRole()
    {
        Presentation deck;
        const Look look = deck.look();
        Item title;
        title.type = QStringLiteral("text");
        title.role = QStringLiteral("ctitle");
        TextDefaults d = textDefaults(title, look);
        QCOMPARE(d.align, 1);
        QCOMPARE(d.color, look.title);
        QCOMPARE(d.font, look.titleFont);
        QVERIFY(d.size > 40);
        Item body;
        body.type = QStringLiteral("text");
        body.role = QStringLiteral("body");
        d = textDefaults(body, look);
        QCOMPARE(d.align, 0);
        QCOMPARE(d.size, 24.0);
        QVERIFY(d.spacing > 0);
        // On a dark shape the ink is white; on a light one, dark.
        Item shape;
        shape.type = QStringLiteral("shape");
        shape.fill = QStringLiteral("#101010");
        QCOMPARE(textDefaults(shape, look).color, QColor(Qt::white));
        shape.fill = QStringLiteral("#f0f0f0");
        QVERIFY(textDefaults(shape, look).color.lightness() < 60);
    }

    void buildsAndMedia()
    {
        Slide slide = newSlide(QStringLiteral("content"), QSizeF(960, 540));
        QVERIFY(slide.builds().isEmpty());
        slide.items[1].anim = QStringLiteral("appear");
        slide.items[0].anim = QStringLiteral("slide");
        QCOMPARE(slide.builds(), QStringList({slide.items.at(0).id, slide.items.at(1).id}));
        QCOMPARE(slide.indexOf(slide.items.at(1).id), 1);
        QCOMPARE(slide.indexOf(QStringLiteral("nope")), -1);

        Presentation deck;
        const QString a = deck.addMedia(QByteArrayLiteral("bytes"), QStringLiteral("png"));
        const QString b = deck.addMedia(QByteArrayLiteral("bytes"), QStringLiteral("png"));
        const QString c = deck.addMedia(QByteArrayLiteral("other"), QStringLiteral("jpg"));
        QCOMPARE(a, b);
        QVERIFY(a != c);
        QVERIFY(a.endsWith(QLatin1String(".png")));
        QCOMPARE(deck.media.size(), 2);
        QCOMPARE(deck.media.value(c), QByteArrayLiteral("other"));
    }
};

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
