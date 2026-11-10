#include "deck.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Model;

class TestDeck : public QObject
{
    Q_OBJECT

    static QRectF rectOf(const Deck &deck, const QString &id)
    {
        const Item *item = std::as_const(deck).find(id);
        return item ? item->rect() : QRectF();
    }

private Q_SLOTS:
    void startsWithATitleSlide()
    {
        Deck deck;
        deck.reset();
        QCOMPARE(deck.count(), 1);
        QCOMPARE(deck.current(), 0);
        QCOMPARE(deck.slide(0).value(QStringLiteral("layout")).toString(), QStringLiteral("title"));
        QCOMPARE(deck.items(0).size(), 2);
        QVERIFY(!deck.canUndo());
        QVERIFY(!deck.modified());
        QCOMPARE(deck.aspect(), QStringLiteral("16:9"));
        deck.reset(QStringLiteral("negro"), QStringLiteral("4:3"));
        QCOMPARE(deck.aspect(), QStringLiteral("4:3"));
        QCOMPARE(deck.width(), 720.0);
        QCOMPARE(deck.theme(), QStringLiteral("negro"));
    }

    void slidesAddDuplicateMoveRemove()
    {
        Deck deck;
        deck.reset();
        QSignalSpy changed(&deck, &Deck::changed);
        QCOMPARE(deck.addSlide(QStringLiteral("content")), 1);
        QCOMPARE(deck.current(), 1);
        QVERIFY(changed.count() > 0);
        QVERIFY(deck.modified());
        QCOMPARE(deck.undoText(), QStringLiteral("Añadir diapositiva"));
        deck.addSlide(QStringLiteral("blank"));
        QCOMPARE(deck.count(), 3);
        const QString second = deck.slide(1).value(QStringLiteral("id")).toString();

        deck.duplicateSlide(1);
        QCOMPARE(deck.count(), 4);
        QCOMPARE(deck.current(), 2);
        QCOMPARE(deck.slide(2).value(QStringLiteral("layout")).toString(), QStringLiteral("content"));
        QVERIFY(deck.slide(2).value(QStringLiteral("id")).toString() != second);
        // The copy has items of its own.
        QVERIFY(deck.items(2).at(0).toMap().value(QStringLiteral("id")) != deck.items(1).at(0).toMap().value(QStringLiteral("id")));

        deck.moveSlide(1, 3);
        QCOMPARE(deck.slide(3).value(QStringLiteral("id")).toString(), second);
        deck.undo();
        QCOMPARE(deck.slide(1).value(QStringLiteral("id")).toString(), second);
        deck.redo();
        QCOMPARE(deck.slide(3).value(QStringLiteral("id")).toString(), second);

        deck.removeSlide(3);
        QCOMPARE(deck.count(), 3);
        deck.undo();
        QCOMPARE(deck.count(), 4);
        QCOMPARE(deck.slide(3).value(QStringLiteral("id")).toString(), second);
        QCOMPARE(deck.current(), 3);

        // The last slide cannot go.
        deck.reset();
        deck.removeSlide(0);
        QCOMPARE(deck.count(), 1);
    }

    void slideProperties()
    {
        Deck deck;
        deck.reset();
        deck.setSlide(0, QStringLiteral("hidden"), true);
        QVERIFY(deck.slide(0).value(QStringLiteral("hidden")).toBool());
        deck.addSlide(QStringLiteral("content"));
        QCOMPARE(deck.shown(), QVariantList({1}));
        deck.setSlide(1, QStringLiteral("notes"), QStringLiteral("Hablar despacio"));
        deck.setSlide(1, QStringLiteral("transition"), QStringLiteral("push"));
        deck.setSlide(1, QStringLiteral("background"), QStringLiteral("#223344"));
        QCOMPARE(deck.slide(1).value(QStringLiteral("notes")).toString(), QStringLiteral("Hablar despacio"));
        QCOMPARE(deck.slide(1).value(QStringLiteral("transition")).toString(), QStringLiteral("push"));
        QCOMPARE(deck.slide(1).value(QStringLiteral("background")).toString(), QStringLiteral("#223344"));
        deck.setSlide(1, QStringLiteral("layout"), QStringLiteral("columns"));
        QCOMPARE(deck.items(1).size(), 3);
        QCOMPARE(deck.undoText(), QStringLiteral("Cambiar diseño"));
        deck.undo();
        QCOMPARE(deck.items(1).size(), 2);
    }

    void addingItems()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString text = deck.addText();
        QCOMPARE(deck.selection(), QStringList({text}));
        QCOMPARE(deck.item(text).value(QStringLiteral("type")).toString(), QStringLiteral("text"));
        for (const char *shape : {"rect", "rounded", "ellipse", "triangle", "arrow", "star", "diamond"}) {
            const QString id = deck.addShape(QLatin1String(shape));
            QCOMPARE(std::as_const(deck).find(id)->shape, QLatin1String(shape));
            QVERIFY(std::as_const(deck).find(id)->w > 0 && std::as_const(deck).find(id)->h > 0);
            QVERIFY(deck.item(id).value(QStringLiteral("texty")).toBool());
        }
        const QString line = deck.addShape(QStringLiteral("line"));
        QVERIFY(deck.item(line).value(QStringLiteral("line")).toBool());
        QVERIFY(!std::as_const(deck).find(line)->arrow);
        const QString connector = deck.addShape(QStringLiteral("connector"));
        QCOMPARE(std::as_const(deck).find(connector)->shape, QStringLiteral("line"));
        QVERIFY(std::as_const(deck).find(connector)->arrow);
        const QString table = deck.addTable(3, 4);
        QCOMPARE(std::as_const(deck).find(table)->cells.size(), 12);
        deck.setCell(table, 1, 2, QStringLiteral("Celda"));
        QCOMPARE(std::as_const(deck).find(table)->cell(1, 2), QStringLiteral("Celda"));
        deck.setTableSize(table, 2, 5);
        QCOMPARE(std::as_const(deck).find(table)->cells.size(), 10);
        QCOMPARE(std::as_const(deck).find(table)->cell(1, 2), QStringLiteral("Celda"));
        QCOMPARE(deck.items(1).size(), 11);

        QTemporaryDir dir;
        QImage picture(80, 40, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        const QString path = dir.filePath(QStringLiteral("foto.png"));
        QVERIFY(picture.save(path));
        const QString image = deck.addImage(path);
        QVERIFY(!image.isEmpty());
        QCOMPARE(std::as_const(deck).find(image)->type, QStringLiteral("image"));
        QVERIFY(qAbs(std::as_const(deck).find(image)->w / std::as_const(deck).find(image)->h - 2) < 0.05);
        QVERIFY(deck.data().media.contains(std::as_const(deck).find(image)->image));
        QVERIFY(deck.addImage(dir.filePath(QStringLiteral("no-existe.png"))).isEmpty());
        // Undo takes the picture off the slide.
        deck.undo();
        QVERIFY(!std::as_const(deck).find(image));
    }

    void pictureIntoPlaceholder()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("picture"));
        QString holder;
        for (const QVariant &v : deck.items(1)) {
            if (v.toMap().value(QStringLiteral("role")).toString() == QLatin1String("picture")) {
                holder = v.toMap().value(QStringLiteral("id")).toString();
            }
        }
        QVERIFY(!holder.isEmpty());
        const QRectF box = rectOf(deck, holder);
        QTemporaryDir dir;
        QImage picture(50, 100, QImage::Format_RGB32);
        picture.fill(Qt::red);
        const QString path = dir.filePath(QStringLiteral("alta.jpg"));
        QVERIFY(picture.save(path));
        deck.select(holder);
        QCOMPARE(deck.addImage(path), holder);
        QCOMPARE(deck.items(1).size(), 2);
        const QRectF now = rectOf(deck, holder);
        QVERIFY(box.adjusted(-0.01, -0.01, 0.01, 0.01).contains(now));
        QVERIFY(qAbs(now.height() - box.height()) < 0.01);
        QVERIFY(qAbs(now.width() / now.height() - 0.5) < 0.01);
        QVERIFY(std::as_const(deck).find(holder)->image.endsWith(QLatin1String(".jpg")));
    }

    void propertiesUndoRedo()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString id = deck.addShape(QStringLiteral("rect"));
        deck.setProp(QStringLiteral("fill"), QStringLiteral("#ff0000"));
        deck.setProp(QStringLiteral("stroke"), QStringLiteral("#000000"));
        deck.setProp(QStringLiteral("shadow"), true);
        deck.setProp(QStringLiteral("rotation"), 400);
        QCOMPARE(std::as_const(deck).find(id)->fill, QStringLiteral("#ff0000"));
        QVERIFY(std::as_const(deck).find(id)->strokeWidth > 0);
        QVERIFY(std::as_const(deck).find(id)->shadow);
        QCOMPARE(std::as_const(deck).find(id)->rotation, 40.0);
        QCOMPARE(deck.item(id).value(QStringLiteral("fillColor")).toString(), QStringLiteral("#ff0000"));
        deck.setProp(QStringLiteral("opacity"), 7);
        QCOMPARE(std::as_const(deck).find(id)->opacity, 1.0);
        deck.setProp(QStringLiteral("shape"), QStringLiteral("ellipse"));
        QCOMPARE(std::as_const(deck).find(id)->shape, QStringLiteral("ellipse"));
        deck.undo();
        QCOMPARE(std::as_const(deck).find(id)->shape, QStringLiteral("rect"));
        deck.undo(); // opacity: nothing changed, so this is the rotation.
        QCOMPARE(std::as_const(deck).find(id)->rotation, 0.0);
        deck.redo();
        QCOMPARE(std::as_const(deck).find(id)->rotation, 40.0);
        while (deck.canUndo()) {
            deck.undo();
        }
        QCOMPARE(deck.count(), 1);
        QVERIFY(!deck.modified());
        while (deck.canRedo()) {
            deck.redo();
        }
        QCOMPARE(std::as_const(deck).find(id)->shape, QStringLiteral("ellipse"));
        // A new step forgets what could be redone.
        deck.undo();
        QVERIFY(deck.canRedo());
        deck.select(id);
        deck.setProp(QStringLiteral("fill"), QStringLiteral("#00ff00"));
        QCOMPARE(std::as_const(deck).find(id)->fill, QStringLiteral("#00ff00"));
        QVERIFY(!deck.canRedo());
    }

    void slidersMergeIntoOneStep()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString id = deck.addShape(QStringLiteral("rect"));
        for (int i = 9; i >= 3; --i) {
            deck.setProp(QStringLiteral("opacity"), i / 10.0);
        }
        QCOMPARE(std::as_const(deck).find(id)->opacity, 0.3);
        deck.undo();
        QCOMPARE(std::as_const(deck).find(id)->opacity, 1.0);
        deck.redo();
        QCOMPARE(std::as_const(deck).find(id)->opacity, 0.3);
    }

    void textFormat()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString id = deck.addText();
        deck.setPlain(id, QStringLiteral("Uno\nDos"));
        QCOMPARE(deck.item(id).value(QStringLiteral("plain")).toString(), QStringLiteral("Uno\nDos"));
        deck.setFormat(QStringLiteral("bold"), true);
        deck.setFormat(QStringLiteral("italic"), true);
        deck.setFormat(QStringLiteral("size"), 44);
        deck.setFormat(QStringLiteral("color"), QStringLiteral("#00aa00"));
        deck.setFormat(QStringLiteral("font"), QStringLiteral("DejaVu Serif"));
        deck.setFormat(QStringLiteral("align"), 2);
        deck.setFormat(QStringLiteral("bullet"), true);
        const QVariantMap item = deck.item(id);
        QVERIFY(item.value(QStringLiteral("bold")).toBool());
        QVERIFY(item.value(QStringLiteral("italic")).toBool());
        QCOMPARE(item.value(QStringLiteral("size")).toDouble(), 44.0);
        QCOMPARE(item.value(QStringLiteral("color")).toString(), QStringLiteral("#00aa00"));
        QCOMPARE(item.value(QStringLiteral("font")).toString(), QStringLiteral("DejaVu Serif"));
        QCOMPARE(item.value(QStringLiteral("align")).toInt(), 2);
        QVERIFY(item.value(QStringLiteral("bullet")).toBool());
        for (const Paragraph &paragraph : std::as_const(deck).find(id)->text) {
            QCOMPARE(paragraph.align, 2);
            QVERIFY(paragraph.bullet);
            for (const Run &run : paragraph.runs) {
                QCOMPARE(run.bold, 1);
                QCOMPARE(run.size, 44.0);
            }
        }
        // Back to what the theme says: unset again.
        const QVariantMap d = deck.textDefaults(id);
        deck.setFormat(QStringLiteral("size"), d.value(QStringLiteral("size")));
        QCOMPARE(std::as_const(deck).find(id)->text.at(0).runs.at(0).size, 0.0);

        // The editor's HTML comes back as the same paragraphs.
        const QList<Paragraph> before = std::as_const(deck).find(id)->text;
        deck.setHtml(id, deck.html(id));
        QCOMPARE(std::as_const(deck).find(id)->text, before);
    }

    void selection()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString a = deck.addShape(QStringLiteral("rect"));
        deck.setProp(QStringLiteral("x"), 100);
        deck.setProp(QStringLiteral("y"), 100);
        const QString b = deck.addShape(QStringLiteral("ellipse"));
        deck.setProp(QStringLiteral("x"), 500);
        deck.setProp(QStringLiteral("y"), 300);
        QSignalSpy spy(&deck, &Deck::selectionChanged);
        deck.select(a);
        QCOMPARE(deck.selection(), QStringList({a}));
        deck.select(b, true);
        QCOMPARE(deck.selection().size(), 2);
        deck.select(a, true);
        QCOMPARE(deck.selection(), QStringList({b}));
        QVERIFY(spy.count() >= 3);
        deck.clearSelection();
        QVERIFY(deck.selection().isEmpty());
        deck.selectAll();
        QCOMPARE(deck.selection().size(), 2);
        deck.selectRect(90, 90, 300, 300);
        QCOMPARE(deck.selection(), QStringList({a}));
        QCOMPARE(deck.hitTest(110, 110), a);
        QCOMPARE(deck.hitTest(5, 5), QString());
        // The one on top wins.
        deck.select(b);
        deck.setProp(QStringLiteral("x"), 150);
        deck.setProp(QStringLiteral("y"), 150);
        QCOMPARE(deck.hitTest(225, 225), b);
        deck.selectOnly({a, b});
        const QVariantMap bounds = deck.selectionBounds();
        QCOMPARE(bounds.value(QStringLiteral("x")).toDouble(), 100.0);
        QCOMPARE(bounds.value(QStringLiteral("w")).toDouble(), 220.0);
        // Changing slide drops it.
        deck.setCurrent(0);
        QVERIFY(deck.selection().isEmpty());
    }

    void dragging()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString id = deck.addShape(QStringLiteral("rect"));
        deck.setProp(QStringLiteral("x"), 100);
        deck.setProp(QStringLiteral("y"), 100);
        deck.setProp(QStringLiteral("w"), 200);
        deck.setProp(QStringLiteral("h"), 100);

        deck.startDrag();
        deck.moveBy(10, 0);
        deck.moveBy(30, 0);
        deck.moveBy(51, 17);
        QCOMPARE(rectOf(deck, id), QRectF(151, 117, 200, 100));
        deck.finishDrag(QStringLiteral("Mover"));
        QCOMPARE(deck.undoText(), QStringLiteral("Mover"));
        deck.undo();
        QCOMPARE(rectOf(deck, id), QRectF(100, 100, 200, 100));

        // With the magnet, near the middle of the slide it lands on it, and the guides show.
        deck.select(id);
        deck.startDrag();
        deck.moveBy(278, 118, 6);
        QCOMPARE(rectOf(deck, id).center(), QPointF(480, 270));
        QVERIFY(!deck.guides().isEmpty());
        deck.finishDrag(QStringLiteral("Mover"));
        QVERIFY(deck.guides().isEmpty());
        deck.undo();

        deck.select(id);
        deck.startDrag();
        deck.resizeBy(QStringLiteral("se"), 50, 50);
        QCOMPARE(rectOf(deck, id), QRectF(100, 100, 250, 150));
        deck.finishDrag(QStringLiteral("Cambiar tamaño"));
        deck.startDrag();
        deck.rotateBy(45);
        QCOMPARE(std::as_const(deck).find(id)->rotation, 45.0);
        deck.finishDrag(QStringLiteral("Girar"));
        deck.undo();
        QCOMPARE(std::as_const(deck).find(id)->rotation, 0.0);
        deck.undo();
        QCOMPARE(rectOf(deck, id), QRectF(100, 100, 200, 100));

        // A drag that goes nowhere is no step.
        const QString before = deck.undoText();
        deck.startDrag();
        deck.moveBy(0, 0);
        deck.finishDrag(QStringLiteral("Mover"));
        QCOMPARE(deck.undoText(), before);

        const QString line = deck.addShape(QStringLiteral("line"));
        const QLineF was = std::as_const(deck).find(line)->line();
        deck.startDrag();
        deck.moveEnd(1, 40, 90);
        deck.finishDrag(QStringLiteral("Mover extremo"));
        QCOMPARE(std::as_const(deck).find(line)->line().p1(), was.p1());
        QCOMPARE(std::as_const(deck).find(line)->line().p2(), was.p2() + QPointF(40, 90));
        deck.startDrag();
        deck.moveEnd(0, 300, 200);
        deck.finishDrag(QStringLiteral("Mover extremo"));
        QCOMPARE(std::as_const(deck).find(line)->line().p1(), was.p1() + QPointF(300, 200));
        QCOMPARE(std::as_const(deck).find(line)->line().p2(), was.p2() + QPointF(40, 90));
    }

    void arranging()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        QStringList ids;
        const QList<QRectF> boxes = {QRectF(100, 100, 100, 50), QRectF(300, 200, 60, 60), QRectF(700, 400, 100, 100)};
        for (const QRectF &box : boxes) {
            ids << deck.addShape(QStringLiteral("rect"));
            deck.setProp(QStringLiteral("x"), box.x());
            deck.setProp(QStringLiteral("y"), box.y());
            deck.setProp(QStringLiteral("w"), box.width());
            deck.setProp(QStringLiteral("h"), box.height());
        }
        // One item aligns against the slide.
        deck.select(ids.at(0));
        deck.align(QStringLiteral("center"));
        QCOMPARE(rectOf(deck, ids.at(0)).center().x(), 480.0);
        deck.align(QStringLiteral("bottom"));
        QCOMPARE(rectOf(deck, ids.at(0)).bottom(), 540.0);
        deck.undo();
        deck.undo();
        // Several, against each other.
        deck.selectOnly(ids);
        deck.align(QStringLiteral("left"));
        for (const QString &id : ids) {
            QCOMPARE(rectOf(deck, id).x(), 100.0);
        }
        deck.undo();
        deck.align(QStringLiteral("middle"));
        for (const QString &id : ids) {
            QCOMPARE(rectOf(deck, id).center().y(), 300.0);
        }
        deck.undo();
        deck.distribute(QStringLiteral("h"));
        // 800 − 100 wide, 260 of boxes: two gaps of 220.
        QCOMPARE(rectOf(deck, ids.at(0)).x(), 100.0);
        QCOMPARE(rectOf(deck, ids.at(1)).x(), 420.0);
        QCOMPARE(rectOf(deck, ids.at(2)).x(), 700.0);
        deck.undo();
        deck.distribute(QStringLiteral("v"));
        QCOMPARE(rectOf(deck, ids.at(1)).y(), 245.0);
        deck.undo();

        deck.select(ids.at(0));
        deck.nudge(1, -10);
        QCOMPARE(rectOf(deck, ids.at(0)).topLeft(), QPointF(101, 90));
    }

    void groupingAndOrder()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString a = deck.addShape(QStringLiteral("rect"));
        const QString b = deck.addShape(QStringLiteral("ellipse"));
        const QString c = deck.addShape(QStringLiteral("star"));
        deck.selectOnly({a, b});
        deck.group();
        QVERIFY(!std::as_const(deck).find(a)->group.isEmpty());
        QCOMPARE(std::as_const(deck).find(a)->group, std::as_const(deck).find(b)->group);
        QVERIFY(std::as_const(deck).find(c)->group.isEmpty());
        // Selecting one of a group selects all of it; they move as one.
        deck.select(a);
        QCOMPARE(deck.selection().size(), 2);
        const QPointF before = rectOf(deck, b).topLeft();
        deck.nudge(10, 0);
        QCOMPARE(rectOf(deck, b).topLeft(), before + QPointF(10, 0));
        deck.ungroup();
        QVERIFY(std::as_const(deck).find(a)->group.isEmpty());
        deck.select(a);
        QCOMPARE(deck.selection(), QStringList({a}));

        const auto order = [&] {
            QStringList ids;
            for (const QVariant &v : deck.items(1)) {
                ids << v.toMap().value(QStringLiteral("id")).toString();
            }
            return ids;
        };
        QCOMPARE(order(), QStringList({a, b, c}));
        deck.order(QStringLiteral("front"));
        QCOMPARE(order(), QStringList({b, c, a}));
        deck.order(QStringLiteral("back"));
        QCOMPARE(order(), QStringList({a, b, c}));
        deck.order(QStringLiteral("forward"));
        QCOMPARE(order(), QStringList({b, a, c}));
        deck.order(QStringLiteral("backward"));
        QCOMPARE(order(), QStringList({a, b, c}));
        deck.undo();
        QCOMPARE(order(), QStringList({b, a, c}));
    }

    void clipboard()
    {
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("blank"));
        const QString a = deck.addShape(QStringLiteral("rect"));
        deck.setProp(QStringLiteral("fill"), QStringLiteral("#123456"));
        deck.copy();
        deck.paste();
        QCOMPARE(deck.items(1).size(), 2);
        QCOMPARE(deck.selection().size(), 1);
        const QString pasted = deck.selection().first();
        QVERIFY(pasted != a);
        QCOMPARE(std::as_const(deck).find(pasted)->fill, QStringLiteral("#123456"));
        // Not right on top of the original.
        QVERIFY(rectOf(deck, pasted).topLeft() != rectOf(deck, a).topLeft());
        // Onto another slide, in the same place.
        deck.addSlide(QStringLiteral("blank"));
        deck.paste();
        QCOMPARE(deck.items(2).size(), 1);
        deck.setCurrent(1);
        deck.select(a);
        deck.cut();
        QCOMPARE(deck.items(1).size(), 1);
        QVERIFY(!std::as_const(deck).find(a));
        deck.undo();
        QVERIFY(std::as_const(deck).find(a));
        deck.select(a);
        deck.duplicate();
        QCOMPARE(deck.items(1).size(), 3);
        deck.remove();
        QCOMPARE(deck.items(1).size(), 2);
        deck.selectAll();
        deck.remove();
        QCOMPARE(deck.items(1).size(), 0);
        deck.undo();
        QCOMPARE(deck.items(1).size(), 2);

        // Plain text on the clipboard becomes a text box.
        QGuiApplication::clipboard()->setText(QStringLiteral("Texto de fuera"));
        deck.paste();
        QCOMPARE(deck.items(1).size(), 3);
        QCOMPARE(std::as_const(deck).find(deck.selection().first())->plainText(), QStringLiteral("Texto de fuera"));
    }

    void themeMasterAspect()
    {
        Deck deck;
        deck.reset();
        QVERIFY(deck.themes().size() >= 6);
        deck.setTheme(QStringLiteral("aurora"));
        QCOMPARE(deck.theme(), QStringLiteral("aurora"));
        QVERIFY(!deck.swatches().isEmpty());
        deck.undo();
        QCOMPARE(deck.theme(), QStringLiteral("blanco"));

        QVariantMap master = deck.master();
        master.insert(QStringLiteral("footer"), QStringLiteral("Wonderland"));
        master.insert(QStringLiteral("numbers"), true);
        master.insert(QStringLiteral("background"), QStringLiteral("#eeeeee"));
        deck.setMaster(master);
        QCOMPARE(deck.master().value(QStringLiteral("footer")).toString(), QStringLiteral("Wonderland"));
        QVERIFY(deck.master().value(QStringLiteral("numbers")).toBool());
        QCOMPARE(deck.data().look().bg1, QColor(0xee, 0xee, 0xee));
        deck.undo();
        QVERIFY(deck.master().value(QStringLiteral("footer")).toString().isEmpty());

        // To 4:3 and back: everything is where it was.
        const QString shape = deck.addShape(QStringLiteral("rect"));
        const QRectF before = rectOf(deck, shape);
        const QVariantList layout = deck.items(0);
        deck.setAspect(QStringLiteral("4:3"));
        QCOMPARE(deck.width(), 720.0);
        QCOMPARE(deck.height(), 540.0);
        for (const QVariant &v : deck.items(0)) {
            const QVariantMap item = v.toMap();
            QVERIFY(item.value(QStringLiteral("x")).toDouble() >= 0);
            QVERIFY(item.value(QStringLiteral("x")).toDouble() + item.value(QStringLiteral("w")).toDouble() <= 720.01);
        }
        deck.undo();
        QCOMPARE(deck.width(), 960.0);
        QCOMPARE(rectOf(deck, shape), before);
        QCOMPARE(deck.items(0), layout);
        QVERIFY(deck.fonts().size() > 0);
    }

    void saveAndLoad()
    {
        QTemporaryDir dir;
        Deck deck;
        deck.reset(QStringLiteral("oceano"));
        deck.addSlide(QStringLiteral("content"));
        deck.setPlain(deck.items(1).at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("Guardado"));
        deck.setSlide(1, QStringLiteral("notes"), QStringLiteral("Una nota"));
        deck.addShape(QStringLiteral("star"));
        QVERIFY(deck.modified());
        QSignalSpy file(&deck, &Deck::fileChanged);
        QVERIFY(deck.save(dir.filePath(QStringLiteral("prueba"))));
        QCOMPARE(deck.path(), dir.filePath(QStringLiteral("prueba.pptx")));
        QCOMPARE(deck.name(), QStringLiteral("prueba"));
        QVERIFY(!deck.modified());
        QVERIFY(file.count() > 0);
        deck.addShape(QStringLiteral("rect"));
        QVERIFY(deck.modified());
        deck.undo();
        QVERIFY(!deck.modified());

        Deck other;
        QVERIFY(other.load(deck.path()));
        QCOMPARE(other.count(), 2);
        QCOMPARE(other.theme(), QStringLiteral("oceano"));
        QCOMPARE(other.slide(1).value(QStringLiteral("notes")).toString(), QStringLiteral("Una nota"));
        QCOMPARE(other.items(1).size(), 3);
        QVERIFY(!other.modified());
        QVERIFY(!other.canUndo());

        // The same deck as an .odp, by the ending of its name alone.
        QVERIFY(deck.save(dir.filePath(QStringLiteral("abierta.odp"))));
        QCOMPARE(deck.path(), dir.filePath(QStringLiteral("abierta.odp")));
        Deck open;
        QVERIFY(open.load(deck.path()));
        QCOMPARE(open.path(), deck.path());
        QCOMPARE(open.count(), 2);
        QCOMPARE(open.theme(), QStringLiteral("oceano"));
        QCOMPARE(open.slide(1).value(QStringLiteral("notes")).toString(), QStringLiteral("Una nota"));
        QCOMPARE(open.items(1).size(), 3);

        QVERIFY(!other.load(dir.filePath(QStringLiteral("no-existe.pptx"))));
        QVERIFY(!other.error().isEmpty());
        QCOMPARE(other.count(), 2);
        QFile junk(dir.filePath(QStringLiteral("basura.pptx")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write("esto no es un zip");
        junk.close();
        QVERIFY(!other.load(junk.fileName()));
        QVERIFY(!other.error().isEmpty());
        QCOMPARE(other.count(), 2);
        QVERIFY(!deck.save(QStringLiteral("/proc/no/se/puede/aqui")));
        QVERIFY(!deck.error().isEmpty());
    }

    void exporting()
    {
        QTemporaryDir dir;
        Deck deck;
        deck.reset();
        deck.addSlide(QStringLiteral("content"));
        deck.addSlide(QStringLiteral("blank"));
        deck.setSlide(1, QStringLiteral("hidden"), true);
        QCOMPARE(deck.exportPng(dir.filePath(QStringLiteral("salida.png"))), 2);
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("salida-01.png"))));
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("salida-02.png"))));
        const QImage third(dir.filePath(QStringLiteral("salida-03.png")));
        QCOMPARE(third.size(), QSize(1920, 1080));
        QVERIFY(deck.exportPdf(dir.filePath(QStringLiteral("salida.pdf"))));
        QFile pdf(dir.filePath(QStringLiteral("salida.pdf")));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray bytes = pdf.readAll();
        QVERIFY(bytes.startsWith("%PDF-"));
        QVERIFY(bytes.size() > 1000);
    }
};

QTEST_MAIN(TestDeck)
#include "tst_deck.moc"
