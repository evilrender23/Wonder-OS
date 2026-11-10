#include "history.h"
#include "model.h"

#include <QtTest>

class TestModel : public QObject
{
    Q_OBJECT

    static Shape box(Document &doc, double x, double y, double w, double h)
    {
        Shape s;
        s.id = doc.freshId();
        s.x = x;
        s.y = y;
        s.w = w;
        s.h = h;
        s.name = doc.freshName(QStringLiteral("Rectángulo"));
        doc.shapes << s;
        return s;
    }

private Q_SLOTS:
    void numbers()
    {
        QCOMPARE(Model::number(1.50), QStringLiteral("1.5"));
        QCOMPARE(Model::number(-0.0001), QStringLiteral("0"));
        QCOMPARE(Model::number(qQNaN()), QStringLiteral("0"));
        QCOMPARE(Model::number(12), QStringLiteral("12"));
    }
    void colours()
    {
        QCOMPARE(Model::color(Model::colorName(QColor(1, 2, 3))), QColor(1, 2, 3));
        const QColor clear(10, 20, 30, 128);
        QCOMPARE(Model::color(Model::colorName(clear)), clear);
        QCOMPARE(Model::color(QStringLiteral("no es un color"), QColor(Qt::red)), QColor(Qt::red));
    }
    void paths()
    {
        const QPainterPath path = Model::pathFromD(QStringLiteral("M0 0 L10 0 C 20 0 20 10 10 10 Z"));
        QVERIFY(!path.isEmpty());
        QCOMPARE(path.boundingRect().toRect(), QRect(0, 0, 18, 10));
        const QPainterPath again = Model::pathFromD(Model::pathToD(path));
        QCOMPARE(again.elementCount(), path.elementCount());
        QCOMPARE(again.boundingRect().toRect(), path.boundingRect().toRect());
        // Relative orders, arcs and the short forms are part of the grammar too.
        const QPainterPath relative = Model::pathFromD(QStringLiteral("m10 10 h20 v20 h-20 z"));
        QCOMPARE(relative.boundingRect(), QRectF(10, 10, 20, 20));
        const QPainterPath arc = Model::pathFromD(QStringLiteral("M0 50 A50 50 0 0 1 100 50"));
        QVERIFY(qAbs(arc.boundingRect().width() - 100) < 1);
        QVERIFY(qAbs(arc.boundingRect().top()) < 1);
    }
    void namesAndIds()
    {
        Document doc;
        const Shape a = box(doc, 0, 0, 10, 10);
        const Shape b = box(doc, 0, 0, 10, 10);
        QVERIFY(a.id != b.id);
        QCOMPARE(a.name, QStringLiteral("Rectángulo 1"));
        QCOMPARE(b.name, QStringLiteral("Rectángulo 2"));
        QCOMPARE(doc.indexOf(b.id), 1);
        QVERIFY(!doc.contains(QStringLiteral("nadie")));
    }
    void turnedBounds()
    {
        Shape s;
        s.x = 0;
        s.y = 0;
        s.w = 100;
        s.h = 20;
        s.rotation = 90;
        const QRectF bounds = s.bounds();
        QVERIFY(qAbs(bounds.width() - 20) < 0.01);
        QVERIFY(qAbs(bounds.height() - 100) < 0.01);
        QVERIFY(qAbs(bounds.center().x() - 50) < 0.01);
        // The transform takes the corner of the box to where it shows.
        const QPointF corner = s.transform().map(QPointF(0, 0));
        QVERIFY(qAbs(corner.x() - 60) < 0.01);
        QVERIFY(qAbs(corner.y() + 40) < 0.01);
    }
    void json()
    {
        Document doc;
        doc.uid = QStringLiteral("u1");
        doc.title = QStringLiteral("Año nuevo");
        doc.grid = true;
        doc.snap = false;
        doc.palette = {QStringLiteral("#ff0000")};
        Shape a = box(doc, 1.5, -2, 30, 40);
        doc.shapes[0].rotation = 33;
        doc.shapes[0].text = QStringLiteral("Hola\nmundo");
        doc.shapes[0].group = QStringLiteral("g1");
        doc.shapes[0].locked = true;
        doc.shapes[0].style.fillKind = QStringLiteral("linear");
        doc.shapes[0].style.fill2 = QColor(QStringLiteral("#123456"));
        doc.shapes[0].style.dash = QStringLiteral("dot");
        doc.shapes[0].style.shadow = true;
        doc.shapes[0].style.opacity = 0.5;
        doc.shapes[0].style.bold = true;
        Shape line;
        line.id = doc.freshId();
        line.kind = QStringLiteral("line");
        line.p1 = QPointF(0, 0);
        line.p2 = QPointF(50, 80);
        line.route = QStringLiteral("elbow");
        line.w = 50;
        line.h = 80;
        line.from = a.id;
        line.style.endArrow = QStringLiteral("triangle");
        doc.shapes << line;
        Shape path;
        path.id = doc.freshId();
        path.kind = QStringLiteral("path");
        path.path = Model::pathFromD(QStringLiteral("M0 0 L10 5 L0 10 Z"));
        path.ref = path.path.boundingRect();
        path.w = 20;
        path.h = 20;
        doc.shapes << path;
        doc.resources.insert(QStringLiteral("a.png"), QByteArrayLiteral("xx"));

        Document back = Document::fromJson(doc.toJson());
        QCOMPARE(back.uid, doc.uid);
        QCOMPARE(back.title, doc.title);
        QCOMPARE(back.grid, true);
        QCOMPARE(back.snap, false);
        QCOMPARE(back.palette, doc.palette);
        QCOMPARE(back.shapes.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY2(back.shapes.at(i) == doc.shapes.at(i), qPrintable(QString::number(i)));
        }
        // A new id does not collide with the ones that were read.
        const QString fresh = back.freshId();
        QVERIFY(!doc.contains(fresh));
    }
    void frames()
    {
        Document doc;
        box(doc, 0, 0, 10, 10);
        box(doc, 100, 100, 50, 50);
        doc.shapes[1].kind = QStringLiteral("frame");
        QCOMPARE(doc.frames().size(), 1);
        // The stroke counts: half of it lies outside the box.
        QCOMPARE(doc.bounds(), QRectF(-1, -1, 152, 152));
        doc.shapes[1].hidden = true;
        QCOMPARE(doc.bounds(), QRectF(-1, -1, 12, 12));
    }
    void resources()
    {
        Document doc;
        const QString name = doc.addResource(QByteArrayLiteral("abc"), QStringLiteral("png"));
        QVERIFY(name.endsWith(QStringLiteral(".png")));
        // The same bytes go by the same name.
        QCOMPARE(doc.addResource(QByteArrayLiteral("abc"), QStringLiteral("png")), name);
        QCOMPARE(doc.resources.size(), 1);
        doc.pruneResources();
        QCOMPARE(doc.resources.size(), 0);
    }
    void commands()
    {
        Document before;
        const Shape a = box(before, 0, 0, 10, 10);
        const Shape b = box(before, 20, 0, 10, 10);
        Document after = before;
        after.find(a.id)->x = 5;
        after.shapes.removeAt(1);
        const Shape c = box(after, 40, 0, 10, 10);
        after.shapes.move(1, 0);

        const Command command = Command::between(before, after, QStringLiteral("Cambiar"));
        QVERIFY(!command.isEmpty());
        QCOMPARE(command.changes.size(), 3);
        Document doc = before;
        command.redo(doc);
        QCOMPARE(doc.shapes.size(), 2);
        QCOMPARE(doc.shapes.at(0).id, c.id);
        QCOMPARE(doc.find(a.id)->x, 5.0);
        QVERIFY(!doc.contains(b.id));
        command.undo(doc);
        QCOMPARE(doc.shapes.size(), 2);
        QCOMPARE(doc.shapes.at(0).id, a.id);
        QCOMPARE(doc.shapes.at(1).id, b.id);
        QCOMPARE(doc.find(a.id)->x, 0.0);
        QVERIFY(Command::between(before, before, QString()).isEmpty());
    }
    void history()
    {
        Document doc;
        const Shape a = box(doc, 0, 0, 10, 10);
        History history;
        QVERIFY(history.atSaved());
        auto step = [&](double x, const QString &key) {
            Document next = doc;
            next.find(a.id)->x = x;
            history.push(Command::between(doc, next, QStringLiteral("Mover")), key);
            doc = next;
        };
        step(1, QStringLiteral("x"));
        step(2, QStringLiteral("x"));
        step(3, QStringLiteral("x"));
        // One gesture, one step.
        QCOMPARE(history.size(), 1);
        step(10, QString());
        QCOMPARE(history.size(), 2);
        QVERIFY(!history.atSaved());
        QVERIFY(history.undo(doc));
        QCOMPARE(doc.find(a.id)->x, 3.0);
        QVERIFY(history.undo(doc));
        QCOMPARE(doc.find(a.id)->x, 0.0);
        QVERIFY(history.atSaved());
        QVERIFY(!history.undo(doc));
        QVERIFY(history.redo(doc));
        QCOMPARE(doc.find(a.id)->x, 3.0);
        QCOMPARE(history.redoLabel(), QStringLiteral("Mover"));
        // A new step forgets what was undone.
        step(7, QString());
        QVERIFY(!history.canRedo());
        history.markSaved();
        QVERIFY(history.atSaved());
        history.undo(doc);
        QVERIFY(!history.atSaved());
    }
};

QTEST_APPLESS_MAIN(TestModel)
#include "tst_model.moc"
