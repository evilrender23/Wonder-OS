#include "editor.h"

#include <QSignalSpy>
#include <QtTest>

#include <cmath>

/// The editor as the window drives it: by its verbs and by the pointer.
class TestEditor : public QObject
{
    Q_OBJECT

    static constexpr int Shift = 0x02000000;
    static constexpr int Control = 0x04000000;

    /// A whole gesture, from one point of the canvas to another.
    static void drag(Editor &ed, const QPointF &from, const QPointF &to, int modifiers = 0)
    {
        const QPointF a = ed.toScreen(from);
        const QPointF b = ed.toScreen(to);
        ed.press(a.x(), a.y(), 1, modifiers);
        for (int i = 1; i <= 10; ++i) {
            const QPointF p = a + (b - a) * i / 10.0;
            ed.move(p.x(), p.y(), modifiers);
        }
        ed.release(b.x(), b.y(), modifiers);
    }
    static void click(Editor &ed, const QPointF &at, int modifiers = 0)
    {
        const QPointF p = ed.toScreen(at);
        ed.press(p.x(), p.y(), 1, modifiers);
        ed.release(p.x(), p.y(), modifiers);
    }
    /// A box put exactly where the test wants it.
    static QString box(Editor &ed, double x, double y, double w = 100, double h = 60, const QString &form = QStringLiteral("rect"))
    {
        const QString id = ed.addForm(form);
        ed.setProp(QStringLiteral("x"), x);
        ed.setProp(QStringLiteral("y"), y);
        ed.setProp(QStringLiteral("w"), w);
        ed.setProp(QStringLiteral("h"), h);
        return id;
    }
    static void fresh(Editor &ed)
    {
        Document doc;
        doc.uid = QStringLiteral("test");
        doc.snap = false;
        ed.setDocument(doc);
        ed.setViewport(1000, 700);
        ed.setZoom(1);
        ed.centerOn(0, 0);
    }

private Q_SLOTS:
    /// What several selected shapes disagree on is told apart from what they share.
    void mixedValues()
    {
        Editor ed;
        ed.setViewport(800, 600);
        ed.addForm(QStringLiteral("rect"));
        ed.addForm(QStringLiteral("ellipse"));
        ed.selectAll();
        QVERIFY(ed.info().value(QStringLiteral("mixed")).toStringList().isEmpty());
        const QString one = ed.selection().first();
        ed.select({one});
        ed.setProp(QStringLiteral("fill"), QStringLiteral("#ff0000"));
        ed.selectAll();
        QCOMPARE(ed.info().value(QStringLiteral("mixed")).toStringList(), QStringList{QStringLiteral("fill")});
        // Setting it for all of them settles the matter.
        ed.setProp(QStringLiteral("fill"), QStringLiteral("#00ff00"));
        QVERIFY(ed.info().value(QStringLiteral("mixed")).toStringList().isEmpty());
    }

    void addAndUndo()
    {
        Editor ed;
        fresh(ed);
        QVERIFY(!ed.dirty());
        const QString id = ed.addForm(QStringLiteral("ellipse"));
        QCOMPARE(ed.count(), 1);
        QCOMPARE(ed.selection(), QStringList{id});
        QVERIFY(ed.dirty());
        QCOMPARE(ed.document().find(id)->name, QStringLiteral("Elipse 1"));
        // In the middle of what is in view.
        QVERIFY(QLineF(ed.document().find(id)->center(), QPointF(0, 0)).length() < 1);
        ed.undo();
        QCOMPARE(ed.count(), 0);
        QVERIFY(ed.selection().isEmpty());
        QVERIFY(!ed.dirty());
        ed.redo();
        QCOMPARE(ed.count(), 1);
        ed.markSaved();
        QVERIFY(!ed.dirty());
    }
    void properties()
    {
        Editor ed;
        fresh(ed);
        const QString id = box(ed, 10, 20, 100, 60);
        QCOMPARE(ed.document().find(id)->box(), QRectF(10, 20, 100, 60));
        ed.setProp(QStringLiteral("fill"), QStringLiteral("#ff0000"));
        ed.setProp(QStringLiteral("strokeWidth"), 5);
        ed.setProp(QStringLiteral("dash"), QStringLiteral("dash"));
        ed.setProp(QStringLiteral("radius"), 12);
        ed.setProp(QStringLiteral("shadow"), true);
        ed.setProp(QStringLiteral("opacity"), 0.4);
        ed.setProp(QStringLiteral("rotation"), 45);
        ed.setProp(QStringLiteral("fillKind"), QStringLiteral("linear"));
        ed.setProp(QStringLiteral("fontSize"), 30);
        ed.setProp(QStringLiteral("bold"), true);
        const Shape *s = ed.document().find(id);
        QCOMPARE(s->style.fill, QColor(QStringLiteral("#ff0000")));
        QCOMPARE(s->style.strokeWidth, 5.0);
        QCOMPARE(s->style.dash, QStringLiteral("dash"));
        QCOMPARE(s->style.radius, 12.0);
        QVERIFY(s->style.shadow);
        QCOMPARE(s->style.opacity, 0.4);
        QCOMPARE(s->rotation, 45.0);
        QCOMPARE(s->style.fillKind, QStringLiteral("linear"));
        QCOMPARE(s->style.fontSize, 30.0);
        QVERIFY(s->style.bold);
        // What the inspector reads says the same.
        const QVariantMap info = ed.info();
        QCOMPARE(info.value(QStringLiteral("count")).toInt(), 1);
        QCOMPARE(info.value(QStringLiteral("id")).toString(), id);
        QCOMPARE(info.value(QStringLiteral("w")).toDouble(), 100.0);
        QCOMPARE(info.value(QStringLiteral("strokeWidth")).toDouble(), 5.0);
        QCOMPARE(QColor(info.value(QStringLiteral("fill")).toString()), QColor(QStringLiteral("#ff0000")));
        ed.undo();
        QVERIFY(!ed.document().find(id)->style.bold);
        ed.setProp(QStringLiteral("form"), QStringLiteral("diamond"));
        QCOMPARE(ed.document().find(id)->form, QStringLiteral("diamond"));
    }
    void slidersAreOneStep()
    {
        Editor ed;
        fresh(ed);
        const QString id = box(ed, 0, 0);
        for (int i = 1; i <= 20; ++i) {
            ed.setProp(QStringLiteral("opacity"), 1 - i / 40.0);
        }
        QCOMPARE(ed.document().find(id)->style.opacity, 0.5);
        ed.undo();
        QCOMPARE(ed.document().find(id)->style.opacity, 1.0);
    }
    void clickSelects()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 200, 0);
        click(ed, QPointF(50, 30));
        QCOMPARE(ed.selection(), QStringList{a});
        click(ed, QPointF(250, 30), Shift);
        QCOMPARE(ed.selection().size(), 2);
        click(ed, QPointF(50, 30), Shift);
        QCOMPARE(ed.selection(), QStringList{b});
        click(ed, QPointF(-300, -200));
        QVERIFY(ed.selection().isEmpty());
        // The one on top is the one that is picked.
        const QString c = box(ed, 20, 10);
        click(ed, QPointF(60, 40));
        QCOMPARE(ed.selection(), QStringList{c});
        QCOMPARE(ed.shapeAt(QPointF(5, 5)), a);
        QCOMPARE(ed.shapeAt(QPointF(-50, -50)), QString());
    }
    void marquee()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 200, 0);
        const QString c = box(ed, 0, 200);
        drag(ed, QPointF(-20, -20), QPointF(320, 80));
        QCOMPARE(ed.selection().size(), 2);
        QVERIFY(ed.selection().contains(a) && ed.selection().contains(b));
        QVERIFY(ed.marquee().isNull());
        // With shift, on top of what was there.
        drag(ed, QPointF(-20, 180), QPointF(120, 280), Shift);
        QCOMPARE(ed.selection().size(), 3);
        QVERIFY(ed.selection().contains(c));
        // Nothing was changed by looking.
        QCOMPARE(ed.document().find(a)->box(), QRectF(0, 0, 100, 60));
    }
    void dragMoves()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        ed.clearSelection();
        drag(ed, QPointF(50, 30), QPointF(150, 80));
        QCOMPARE(ed.selection(), QStringList{a});
        QCOMPARE(ed.document().find(a)->box(), QRectF(100, 50, 100, 60));
        ed.undo();
        QCOMPARE(ed.document().find(a)->box(), QRectF(0, 0, 100, 60));
        // Shift keeps it on one axis.
        const QPointF from = ed.toScreen(QPointF(50, 30));
        ed.press(from.x(), from.y(), 1, 0);
        ed.move(from.x() + 60, from.y() + 7, Shift);
        ed.move(from.x() + 120, from.y() + 14, Shift);
        ed.release(from.x() + 120, from.y() + 14, Shift);
        QCOMPARE(ed.document().find(a)->box(), QRectF(120, 0, 100, 60));
    }
    void dragSnaps()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 300, 200);
        ed.setSnap(true);
        ed.clearSelection();
        // Dropped four units short of lining up with the other: it lines up.
        const QPointF from = ed.toScreen(QPointF(350, 230));
        const QPointF to = ed.toScreen(QPointF(54, 400));
        ed.press(from.x(), from.y(), 1, 0);
        ed.move((from.x() + to.x()) / 2, (from.y() + to.y()) / 2, 0);
        ed.move(to.x(), to.y(), 0);
        QVERIFY(!ed.guides().isEmpty());
        ed.release(to.x(), to.y(), 0);
        QVERIFY(ed.guides().isEmpty());
        QCOMPARE(ed.document().find(b)->x, 0.0);
        QCOMPARE(ed.document().find(a)->x, 0.0);
    }
    void resizeAndRotate()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0, 100, 60);
        QPointF corner;
        QPointF turn;
        QStringList roles;
        for (const Editor::Handle &handle : ed.handles()) {
            roles << handle.role;
            if (handle.role == QLatin1String("se")) {
                corner = handle.pos;
            } else if (handle.role == QLatin1String("rotate")) {
                turn = handle.pos;
            }
        }
        QVERIFY2(roles.contains(QStringLiteral("se")) && roles.contains(QStringLiteral("rotate")), qPrintable(roles.join(' ')));
        ed.press(corner.x(), corner.y(), 1, 0);
        ed.move(corner.x() + 20, corner.y() + 10, 0);
        ed.move(corner.x() + 50, corner.y() + 40, 0);
        ed.release(corner.x() + 50, corner.y() + 40, 0);
        QCOMPARE(ed.document().find(a)->box(), QRectF(0, 0, 150, 100));

        for (const Editor::Handle &handle : ed.handles()) {
            if (handle.role == QLatin1String("rotate")) {
                turn = handle.pos;
            }
        }
        // From above the centre to its right: a quarter turn.
        const QPointF centre = ed.toScreen(QPointF(75, 50));
        ed.press(turn.x(), turn.y(), 1, 0);
        ed.move(centre.x() + 60, centre.y() - 60, 0);
        ed.move(centre.x() + 90, centre.y(), 0);
        ed.release(centre.x() + 90, centre.y(), 0);
        QCOMPARE(ed.document().find(a)->rotation, 90.0);
        ed.undo();
        QCOMPARE(ed.document().find(a)->rotation, 0.0);
        ed.undo();
        QCOMPARE(ed.document().find(a)->box(), QRectF(0, 0, 100, 60));
    }
    void drawing()
    {
        Editor ed;
        fresh(ed);
        ed.setTool(QStringLiteral("rect"));
        drag(ed, QPointF(10, 10), QPointF(210, 110));
        QCOMPARE(ed.count(), 1);
        QCOMPARE(ed.document().shapes.first().box(), QRectF(10, 10, 200, 100));
        QCOMPARE(ed.document().shapes.first().form, QStringLiteral("rect"));
        // Back to the pointer, with the new thing in hand.
        QCOMPARE(ed.tool(), QStringLiteral("select"));
        QCOMPARE(ed.selection().size(), 1);

        ed.setTool(QStringLiteral("ellipse"));
        drag(ed, QPointF(300, 0), QPointF(380, 50), Shift);
        const Shape round = ed.document().shapes.last();
        QCOMPARE(round.form, QStringLiteral("ellipse"));
        QCOMPARE(round.w, round.h);

        ed.setTool(QStringLiteral("arrow"));
        drag(ed, QPointF(-300, -300), QPointF(-200, -250));
        const Shape arrow = ed.document().shapes.last();
        QVERIFY(arrow.isLine());
        QCOMPARE(arrow.p1, QPointF(-300, -300));
        QCOMPARE(arrow.p2, QPointF(-200, -250));
        QVERIFY(arrow.style.endArrow != QLatin1String("none"));

        ed.setTool(QStringLiteral("frame"));
        drag(ed, QPointF(-400, 200), QPointF(-100, 320));
        QVERIFY(ed.document().shapes.constLast().isFrame() || ed.document().shapes.constFirst().isFrame());
        QCOMPARE(ed.frameList().size(), 1);

        ed.setTool(QStringLiteral("pencil"));
        const QPointF start = ed.toScreen(QPointF(0, 200));
        ed.press(start.x(), start.y(), 1, 0);
        for (int i = 1; i <= 60; ++i) {
            ed.move(start.x() + i * 3, start.y() + 30 * std::sin(i / 8.0), 0);
        }
        QVERIFY(!ed.strokePreview().isEmpty());
        ed.release(start.x() + 180, start.y() + 30 * std::sin(60 / 8.0), 0);
        const Shape stroke = ed.document().shapes.last();
        QCOMPARE(stroke.kind, QStringLiteral("path"));
        QVERIFY(qAbs(stroke.w - 180) < 4);
        QVERIFY(stroke.path.elementCount() < 60);
        // The pencil stays in the hand for the next stroke.
        QCOMPARE(ed.tool(), QStringLiteral("pencil"));
        const int before = ed.count();
        ed.undo();
        QCOMPARE(ed.count(), before - 1);
    }
    void pen()
    {
        Editor ed;
        fresh(ed);
        ed.setTool(QStringLiteral("pen"));
        click(ed, QPointF(0, 0));
        click(ed, QPointF(100, 0));
        // A drag on the third point pulls a curve out of it.
        drag(ed, QPointF(100, 100), QPointF(140, 140));
        QCOMPARE(ed.penAnchors().size(), 3);
        QVERIFY(!ed.penPreview().isEmpty());
        QCOMPARE(ed.count(), 0);
        ed.finishPen(false);
        QCOMPARE(ed.count(), 1);
        const Shape path = ed.document().shapes.first();
        QCOMPARE(path.kind, QStringLiteral("path"));
        QVERIFY(path.w >= 100 && path.h >= 100);
        bool curved = false;
        for (int i = 0; i < path.path.elementCount(); ++i) {
            curved = curved || path.path.elementAt(i).type == QPainterPath::CurveToElement;
        }
        QVERIFY(curved);
        QVERIFY(ed.penAnchors().isEmpty());
        // Escape ends the path where it is; a single point is not a path.
        ed.setTool(QStringLiteral("pen"));
        click(ed, QPointF(300, 0));
        click(ed, QPointF(400, 0));
        ed.escape();
        QVERIFY(ed.penAnchors().isEmpty());
        QCOMPARE(ed.count(), 2);
        ed.setTool(QStringLiteral("pen"));
        click(ed, QPointF(300, 200));
        ed.escape();
        QVERIFY(ed.penAnchors().isEmpty());
        QCOMPARE(ed.count(), 2);
    }
    void connectorsFollow()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 300, 0);
        const QString link = ed.connect(a, b, QStringLiteral("elbow"), QStringLiteral("Sí"));
        QVERIFY(!link.isEmpty());
        const Shape *line = ed.document().find(link);
        QVERIFY(qAbs(line->p1.x() - 100) < 3);
        QVERIFY(qAbs(line->p2.x() - 300) < 3);
        QCOMPARE(line->text, QStringLiteral("Sí"));

        ed.select(b);
        ed.nudge(0, 300);
        line = ed.document().find(link);
        QVERIFY(line->p2.y() > 250);
        QCOMPARE(line->to, b);
        // One step takes both back.
        ed.undo();
        QVERIFY(qAbs(ed.document().find(link)->p2.y() - 30) < 1);
        QCOMPARE(ed.document().find(b)->y, 0.0);

        // Drawn by hand from one to the other, it sticks too.
        ed.setTool(QStringLiteral("connector"));
        const QString c = box(ed, 0, 300);
        ed.setTool(QStringLiteral("connector"));
        drag(ed, QPointF(50, 30), QPointF(50, 330));
        const Shape drawn = ed.document().shapes.last();
        QVERIFY(drawn.isLine());
        QCOMPARE(drawn.from, a);
        QCOMPARE(drawn.to, c);

        // Without its object, the end stays where it was.
        ed.select(b);
        ed.deleteSelection();
        QVERIFY(ed.document().find(link));
        QVERIFY(ed.document().find(link)->to.isEmpty());
        ed.undo();
        QCOMPARE(ed.document().find(link)->to, b);
    }
    void groups()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 200, 0);
        const QString c = box(ed, 400, 0);
        ed.select(a);
        ed.select(b, true);
        ed.group();
        QVERIFY(!ed.document().find(a)->group.isEmpty());
        QCOMPARE(ed.document().find(a)->group, ed.document().find(b)->group);
        QVERIFY(ed.document().find(c)->group.isEmpty());
        // A click on one picks them all, and they move as one.
        ed.clearSelection();
        click(ed, QPointF(50, 30));
        QCOMPARE(ed.selection().size(), 2);
        ed.nudge(10, 0);
        QCOMPARE(ed.document().find(a)->x, 10.0);
        QCOMPARE(ed.document().find(b)->x, 210.0);
        QVERIFY(ed.info().value(QStringLiteral("grouped")).toBool());
        ed.ungroup();
        QVERIFY(ed.document().find(a)->group.isEmpty());
        ed.clearSelection();
        click(ed, QPointF(60, 30));
        QCOMPARE(ed.selection(), QStringList{a});
    }
    void order()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 10, 0);
        const QString c = box(ed, 20, 0);
        auto ids = [&] {
            QStringList out;
            for (const Shape &s : ed.document().shapes) {
                out << s.id;
            }
            return out;
        };
        ed.select(a);
        ed.reorder(QStringLiteral("front"));
        QCOMPARE(ids(), QStringList({b, c, a}));
        ed.reorder(QStringLiteral("backward"));
        QCOMPARE(ids(), QStringList({b, a, c}));
        ed.reorder(QStringLiteral("back"));
        QCOMPARE(ids(), QStringList({a, b, c}));
        ed.reorder(QStringLiteral("forward"));
        QCOMPARE(ids(), QStringList({b, a, c}));
        ed.undo();
        QCOMPARE(ids(), QStringList({a, b, c}));
        // The list of layers runs from the top.
        QCOMPARE(ed.layers().first().toMap().value(QStringLiteral("id")).toString(), c);
    }
    void alignAndDistribute()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0, 50, 50);
        const QString b = box(ed, 100, 30, 50, 80);
        const QString c = box(ed, 400, 90, 50, 20);
        ed.selectAll();
        ed.align(QStringLiteral("top"));
        QCOMPARE(ed.document().find(b)->y, 0.0);
        QCOMPARE(ed.document().find(c)->y, 0.0);
        ed.align(QStringLiteral("vcenter"));
        QCOMPARE(ed.document().find(a)->center().y(), ed.document().find(c)->center().y());
        ed.distribute(QStringLiteral("h"));
        QCOMPARE(ed.document().find(a)->x, 0.0);
        QCOMPARE(ed.document().find(c)->x, 400.0);
        QCOMPARE(ed.document().find(b)->x, 200.0);
        ed.align(QStringLiteral("right"));
        QCOMPARE(ed.document().find(a)->x, 400.0);
        QCOMPARE(ed.document().find(b)->x, 400.0);
    }
    void locks()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        ed.toggleLock();
        QVERIFY(ed.document().find(a)->locked);
        ed.clearSelection();
        // A locked object is not picked up from the canvas nor moved.
        drag(ed, QPointF(50, 30), QPointF(150, 30));
        QCOMPARE(ed.document().find(a)->x, 0.0);
        ed.selectAll();
        QVERIFY(ed.selection().isEmpty());
        // From the list of layers it still is, to be unlocked.
        ed.select(a);
        ed.nudge(10, 0);
        QCOMPARE(ed.document().find(a)->x, 0.0);
        ed.deleteSelection();
        QCOMPARE(ed.count(), 1);
        ed.setLayerLocked(a, false);
        ed.nudge(10, 0);
        QCOMPARE(ed.document().find(a)->x, 10.0);
        ed.setLayerHidden(a, true);
        QVERIFY(ed.document().find(a)->hidden);
        QCOMPARE(ed.shapeAt(QPointF(50, 30)), QString());
    }
    void copies()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        const QString b = box(ed, 300, 0);
        const QString link = ed.connect(a, b);
        ed.selectAll();
        ed.duplicate();
        QCOMPARE(ed.count(), 6);
        QCOMPARE(ed.selection().size(), 3);
        // The copy of the connector holds on to the copies, not the originals.
        for (const QString &id : ed.selection()) {
            const Shape *s = ed.document().find(id);
            if (s->isLine()) {
                QVERIFY(s->from != a && s->to != b);
                QVERIFY(ed.selection().contains(s->from));
                QVERIFY(ed.selection().contains(s->to));
            } else {
                QVERIFY(s->x == 24 || s->x == 324);
            }
        }
        ed.undo();
        QCOMPARE(ed.count(), 3);

        ed.select(a);
        ed.copy();
        ed.paste();
        QCOMPARE(ed.count(), 4);
        QCOMPARE(ed.document().shapes.last().name, QStringLiteral("Rectángulo 3"));
        ed.select(b);
        ed.cut();
        QCOMPARE(ed.count(), 3);
        QVERIFY(!ed.document().contains(b));
        ed.paste();
        QCOMPARE(ed.count(), 4);
        Q_UNUSED(link)
    }
    void words()
    {
        Editor ed;
        fresh(ed);
        const QString id = ed.addText(QStringLiteral("Hola, lienzo"));
        const Shape *text = ed.document().find(id);
        QCOMPARE(text->kind, QStringLiteral("text"));
        QVERIFY(text->autoSize);
        const double narrow = text->w;
        QVERIFY(narrow > 40);
        // As wide as its words, whatever they become.
        ed.setText(id, QStringLiteral("Hola, lienzo sin fin y sin orillas"));
        QVERIFY(ed.document().find(id)->w > narrow * 1.8);
        ed.setProp(QStringLiteral("fontSize"), 40);
        QVERIFY(ed.document().find(id)->h > 40);
        // Left without words, a text is nothing.
        ed.setText(id, QString());
        QVERIFY(!ed.document().contains(id));

        const QString note = ed.addNote(QStringLiteral("Una idea"));
        QCOMPARE(ed.document().find(note)->kind, QStringLiteral("note"));
        const QVariantMap field = ed.textBox(note);
        QCOMPARE(field.value(QStringLiteral("text")).toString(), QStringLiteral("Una idea"));
        QVERIFY(field.value(QStringLiteral("w")).toDouble() > 50);

        // A double click asks for the words.
        const QString shape = box(ed, 300, 300);
        QSignalSpy asked(&ed, &Editor::editRequested);
        const QPointF p = ed.toScreen(QPointF(350, 330));
        ed.doubleClick(p.x(), p.y());
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.first().first().toString(), shape);
    }
    void frames()
    {
        Editor ed;
        fresh(ed);
        const QString a4 = ed.addFrame(QStringLiteral("a4"));
        const Shape *frame = ed.document().find(a4);
        QVERIFY(frame->isFrame());
        QCOMPARE(frame->box().size(), QSizeF(794, 1123));
        const QString wide = ed.addFrame(QStringLiteral("16:9"));
        // Beside the other, not over it.
        QVERIFY(!ed.document().find(wide)->box().intersects(ed.document().find(a4)->box()));
        // Frames lie under everything else.
        const QString shape = box(ed, 10, 10);
        const QString square = ed.addFrame(QStringLiteral("square"));
        QVERIFY(ed.document().indexOf(square) < ed.document().indexOf(shape));
        const QVariantList frames = ed.frameList();
        QCOMPARE(frames.size(), 3);
        QVERIFY(!frames.first().toMap().value(QStringLiteral("name")).toString().isEmpty());
        ed.rename(a4, QStringLiteral("Portada"));
        QCOMPARE(ed.document().find(a4)->name, QStringLiteral("Portada"));
    }
    void view()
    {
        Editor ed;
        fresh(ed);
        box(ed, 0, 0, 100, 100);
        box(ed, 3000, 2000, 100, 100);
        ed.fitAll();
        QVERIFY(ed.zoom() < 0.4);
        // Both ends are on the screen.
        const QRectF screen(0, 0, 1000, 700);
        QVERIFY(screen.contains(ed.toScreen(QPointF(0, 0))));
        QVERIFY(screen.contains(ed.toScreen(QPointF(3100, 2100))));
        ed.fitSelection();
        QVERIFY(screen.contains(ed.toScreen(QPointF(3050, 2050))));
        QVERIFY(!screen.contains(ed.toScreen(QPointF(0, 0))));
        ed.setZoom(1);
        QCOMPARE(ed.zoom(), 1.0);

        // Zooming keeps the point under the pointer where it is.
        const QPointF anchor = ed.toWorld(QPointF(200, 150));
        ed.zoomAt(2, 200, 150);
        QCOMPARE(ed.zoom(), 2.0);
        QVERIFY(QLineF(ed.toWorld(QPointF(200, 150)), anchor).length() < 1e-6);
        ed.wheel(200, 150, 0, 120, Control);
        QVERIFY(ed.zoom() > 2.0);
        QVERIFY(QLineF(ed.toWorld(QPointF(200, 150)), anchor).length() < 1e-6);
        // Without Ctrl the wheel scrolls.
        const double zoom = ed.zoom();
        const QPointF before = ed.viewCenter();
        ed.wheel(200, 150, 0, -120, 0);
        QCOMPARE(ed.zoom(), zoom);
        QVERIFY(ed.viewCenter().y() > before.y());
        ed.wheel(200, 150, 0, -120, Shift);
        QVERIFY(ed.viewCenter().x() > before.x());
        for (int i = 0; i < 40; ++i) {
            ed.zoomIn();
        }
        QCOMPARE(ed.zoom(), 32.0);
        for (int i = 0; i < 80; ++i) {
            ed.zoomOut();
        }
        QCOMPARE(ed.zoom(), 0.05);

        // The hand and the space bar drag the canvas, not what is on it.
        ed.setZoom(1);
        ed.centerOn(0, 0);
        ed.setTool(QStringLiteral("hand"));
        ed.press(500, 350, 1, 0);
        ed.move(450, 350, 0);
        ed.move(400, 330, 0);
        ed.release(400, 330, 0);
        QCOMPARE(ed.viewCenter(), QPointF(100, 20));
        ed.setTool(QStringLiteral("select"));
        ed.setSpacePan(true);
        ed.press(500, 350, 1, 0);
        ed.move(600, 350, 0);
        ed.release(600, 350, 0);
        ed.setSpacePan(false);
        QCOMPARE(ed.viewCenter(), QPointF(0, 20));
        QCOMPARE(ed.document().shapes.first().box(), QRectF(0, 0, 100, 100));
        QVERIFY(!ed.canUndo() || ed.document().shapes.size() == 2);
    }
    void colours()
    {
        Editor ed;
        fresh(ed);
        box(ed, 0, 0);
        ed.setProp(QStringLiteral("fill"), QStringLiteral("#123456"));
        box(ed, 200, 0);
        ed.setProp(QStringLiteral("fill"), QStringLiteral("#123456"));
        QVERIFY(ed.usedColors().contains(QStringLiteral("#123456")));
        ed.addPaletteColor(QStringLiteral("#abcdef"));
        ed.addPaletteColor(QStringLiteral("#abcdef"));
        QCOMPARE(ed.palette().count(QStringLiteral("#abcdef")), 1);
        ed.removePaletteColor(QStringLiteral("#abcdef"));
        QVERIFY(!ed.palette().contains(QStringLiteral("#abcdef")));
    }
    void escapes()
    {
        Editor ed;
        fresh(ed);
        const QString a = box(ed, 0, 0);
        // In the middle of a drag: the object goes back.
        const QPointF p = ed.toScreen(QPointF(50, 30));
        ed.press(p.x(), p.y(), 1, 0);
        ed.move(p.x() + 40, p.y(), 0);
        ed.move(p.x() + 80, p.y(), 0);
        QVERIFY(ed.busy());
        ed.escape();
        QVERIFY(!ed.busy());
        QCOMPARE(ed.document().find(a)->x, 0.0);
        ed.release(p.x() + 80, p.y(), 0);
        QCOMPARE(ed.document().find(a)->x, 0.0);
        // Then the tool, then the selection.
        ed.setTool(QStringLiteral("rect"));
        ed.escape();
        QCOMPARE(ed.tool(), QStringLiteral("select"));
        ed.select(a);
        ed.escape();
        QVERIFY(ed.selection().isEmpty());
    }
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
