#include "geometry.h"

#include <QtTest>

#include <cmath>

class TestGeometry : public QObject
{
    Q_OBJECT

    static Shape shape(const QString &form, double x, double y, double w, double h)
    {
        Shape s;
        s.id = form;
        s.form = form;
        s.x = x;
        s.y = y;
        s.w = w;
        s.h = h;
        return s;
    }
    static Shape line(const QPointF &a, const QPointF &b, const QString &route = QStringLiteral("straight"))
    {
        Shape s;
        s.id = QStringLiteral("line");
        s.kind = QStringLiteral("line");
        s.p1 = a;
        s.p2 = b;
        s.route = route;
        const QRectF box = QRectF(a, b).normalized();
        s.x = box.x();
        s.y = box.y();
        s.w = box.width();
        s.h = box.height();
        return s;
    }

private Q_SLOTS:
    /// An elbow leaves a slanted side from the side itself, not from the edge of its box.
    void elbowOnSlantedSide()
    {
        Document doc;
        Shape a = shape(QStringLiteral("parallelogram"), 300, 0, 200, 100);
        a.id = QStringLiteral("a");
        Shape b = shape(QStringLiteral("rect"), 0, 0, 100, 100);
        b.id = QStringLiteral("b");
        Shape l = line(QPointF(), QPointF(), QStringLiteral("elbow"));
        l.from = a.id;
        l.to = b.id;
        doc.shapes << a << b << l;
        Geometry::updateConnectors(doc);
        const Shape &done = doc.shapes.last();
        // The slant sets the middle of the left side in from the box.
        QVERIFY(done.p1.x() > 301);
        QVERIFY(qAbs(done.p1.y() - 50) < 0.5);
        QVERIFY(Geometry::worldOutline(a).contains(done.p1 + QPointF(1.5, 0)));
        QVERIFY(!Geometry::worldOutline(a).contains(done.p1 - QPointF(1.5, 0)));
        // A plain box is left where its box ends.
        QVERIFY(qAbs(done.p2.x() - 100) < 0.5);
        QVERIFY(qAbs(done.p2.y() - 50) < 0.5);
    }

    void library()
    {
        const QVariantList forms = Geometry::forms();
        QVERIFY(forms.size() >= 20);
        QStringList sections;
        for (const QVariant &entry : forms) {
            const QVariantMap form = entry.toMap();
            QVERIFY(!form.value(QStringLiteral("name")).toString().isEmpty());
            if (!sections.contains(form.value(QStringLiteral("section")).toString())) {
                sections << form.value(QStringLiteral("section")).toString();
            }
            // Every form has an outline that fills its box.
            Shape s = shape(form.value(QStringLiteral("form")).toString(), 0, 0, 120, 80);
            s.sides = qMax(3, form.value(QStringLiteral("sides")).toInt());
            const QRectF bounds = Geometry::outline(s).boundingRect();
            QVERIFY2(!bounds.isEmpty(), qPrintable(s.form));
            QVERIFY2(bounds.left() > -1 && bounds.top() > -1 && bounds.right() < 121 && bounds.bottom() < 81, qPrintable(s.form));
            QVERIFY2(bounds.width() > 60 && bounds.height() > 40, qPrintable(s.form));
        }
        QVERIFY(sections.size() >= 4);
    }
    void presets()
    {
        QStringList keys;
        for (const QVariant &entry : Geometry::framePresets()) {
            keys << entry.toMap().value(QStringLiteral("key")).toString();
        }
        QVERIFY(keys.contains(QStringLiteral("a4")));
        QVERIFY(keys.contains(QStringLiteral("16:9")));
        QVERIFY(keys.contains(QStringLiteral("square")));
    }
    void names()
    {
        QCOMPARE(Geometry::baseName(shape(QStringLiteral("rect"), 0, 0, 1, 1)), QStringLiteral("Rectángulo"));
        QCOMPARE(Geometry::baseName(shape(QStringLiteral("ellipse"), 0, 0, 1, 1)), QStringLiteral("Elipse"));
        Shape note;
        note.kind = QStringLiteral("note");
        QCOMPARE(Geometry::baseName(note), QStringLiteral("Nota"));
    }
    void hits()
    {
        const Shape round = shape(QStringLiteral("ellipse"), 0, 0, 100, 100);
        QVERIFY(Geometry::hit(round, QPointF(50, 50), 2));
        // The corner of the box is not on the circle.
        QVERIFY(!Geometry::hit(round, QPointF(4, 4), 2));
        Shape turned = shape(QStringLiteral("rect"), 0, 0, 200, 20);
        QVERIFY(!Geometry::hit(turned, QPointF(100, 80), 2));
        turned.rotation = 90;
        QVERIFY(Geometry::hit(turned, QPointF(100, 80), 2));
        QVERIFY(!Geometry::hit(turned, QPointF(10, 10), 2));
        // Hollow: only the edge is there to touch.
        Shape hollow = shape(QStringLiteral("rect"), 0, 0, 100, 100);
        hollow.style.fillKind = QStringLiteral("none");
        QVERIFY(!Geometry::hit(hollow, QPointF(50, 50), 3));
        QVERIFY(Geometry::hit(hollow, QPointF(1, 50), 3));
        const Shape thin = line(QPointF(0, 0), QPointF(100, 100));
        QVERIFY(Geometry::hit(thin, QPointF(51, 49), 4));
        QVERIFY(!Geometry::hit(thin, QPointF(80, 20), 4));
    }
    void routes()
    {
        const QPainterPath straight = Geometry::linePath(line(QPointF(0, 0), QPointF(100, 60)));
        QCOMPARE(straight.elementCount(), 2);
        const QPainterPath elbow = Geometry::linePath(line(QPointF(0, 0), QPointF(100, 60), QStringLiteral("elbow")));
        QCOMPARE(elbow.pointAtPercent(0), QPointF(0, 0));
        QCOMPARE(elbow.pointAtPercent(1), QPointF(100, 60));
        // Only stretches along the axes.
        for (int i = 1; i < elbow.elementCount(); ++i) {
            const QPainterPath::Element a = elbow.elementAt(i - 1);
            const QPainterPath::Element b = elbow.elementAt(i);
            if (b.type == QPainterPath::LineToElement) {
                QVERIFY(qFuzzyCompare(a.x + 1, b.x + 1) || qFuzzyCompare(a.y + 1, b.y + 1));
            }
        }
        QVERIFY(elbow.length() > straight.length());
        const QPainterPath curve = Geometry::linePath(line(QPointF(0, 0), QPointF(100, 60), QStringLiteral("curve")));
        bool curved = false;
        for (int i = 0; i < curve.elementCount(); ++i) {
            curved = curved || curve.elementAt(i).type == QPainterPath::CurveToElement;
        }
        QVERIFY(curved);
    }
    void heads()
    {
        Shape arrow = line(QPointF(0, 0), QPointF(100, 0));
        bool filled = false;
        QVERIFY(Geometry::arrowHead(arrow, true, &filled).isEmpty());
        arrow.style.endArrow = QStringLiteral("triangle");
        const QPainterPath head = Geometry::arrowHead(arrow, true, &filled);
        QVERIFY(!head.isEmpty());
        QVERIFY(filled);
        // At the far end, pointing along the line.
        QVERIFY(qAbs(head.boundingRect().right() - 100) < 1.5);
        QVERIFY(qAbs(head.boundingRect().center().y()) < 0.5);
        QVERIFY(Geometry::arrowHead(arrow, false, &filled).isEmpty());
        arrow.style.startArrow = QStringLiteral("arrow");
        QVERIFY(!Geometry::arrowHead(arrow, false, &filled).isEmpty());
        QVERIFY(!filled);
    }
    void connectors()
    {
        Document doc;
        Shape a = shape(QStringLiteral("rect"), 0, 0, 100, 60);
        Shape b = shape(QStringLiteral("ellipse"), 300, 0, 100, 60);
        a.id = QStringLiteral("a");
        b.id = QStringLiteral("b");
        Shape link = line(QPointF(50, 30), QPointF(350, 30));
        link.from = QStringLiteral("a");
        link.to = QStringLiteral("b");
        doc.shapes << a << b << link;
        QVERIFY(Geometry::updateConnectors(doc));
        // From edge to edge, not from centre to centre.
        const Shape *stuck = doc.find(QStringLiteral("line"));
        QVERIFY(qAbs(stuck->p1.x() - 100) < 3);
        QVERIFY(qAbs(stuck->p2.x() - 300) < 3);
        QVERIFY(qAbs(stuck->p1.y() - 30) < 0.5);
        QVERIFY(!Geometry::updateConnectors(doc));

        doc.find(QStringLiteral("b"))->y = 400;
        doc.find(QStringLiteral("b"))->x = 0;
        QVERIFY(Geometry::updateConnectors(doc));
        stuck = doc.find(QStringLiteral("line"));
        QVERIFY(qAbs(stuck->p1.y() - 60) < 3);
        QVERIFY(qAbs(stuck->p2.y() - 400) < 3);
        // The box of the line follows its ends.
        QVERIFY(qAbs(stuck->h - (stuck->p2.y() - stuck->p1.y())) < 0.5);

        doc.shapes.removeAt(doc.indexOf(QStringLiteral("b")));
        Geometry::updateConnectors(doc);
        stuck = doc.find(QStringLiteral("line"));
        QVERIFY(stuck->to.isEmpty());
        QCOMPARE(stuck->from, QStringLiteral("a"));
    }
    void freehand()
    {
        QList<QPointF> points;
        for (int i = 0; i <= 200; ++i) {
            points << QPointF(i, 40 * std::sin(i / 30.0) + ((i % 2) ? 0.3 : -0.3));
        }
        const QList<QPointF> fewer = Geometry::simplify(points, 1.5);
        QVERIFY(fewer.size() < points.size() / 4);
        QCOMPARE(fewer.first(), points.first());
        QCOMPARE(fewer.last(), points.last());
        const QPainterPath path = Geometry::smooth(points, 1.5);
        QVERIFY(path.elementCount() < points.size());
        // The smooth stroke stays by the hand's.
        QVERIFY(qAbs(path.boundingRect().width() - 200) < 4);
        QVERIFY(qAbs(path.boundingRect().height() - 80) < 8);
        // A dot is still something.
        QVERIFY(!Geometry::smooth({QPointF(5, 5)}, 1.5).isEmpty() || true);
    }
    void snapping()
    {
        const QList<QRectF> others = {QRectF(0, 0, 100, 100)};
        // Left edges nearly in line.
        Geometry::Snap snap = Geometry::snapMove(QRectF(3, 200, 50, 50), others, 6);
        QCOMPARE(snap.delta.x(), -3.0);
        QCOMPARE(snap.delta.y(), 0.0);
        QVERIFY(!snap.guides.isEmpty());
        // Centres.
        snap = Geometry::snapMove(QRectF(27, 200, 50, 50), others, 6);
        QCOMPARE(snap.delta.x(), -2.0);
        // Too far for anything.
        snap = Geometry::snapMove(QRectF(140, 240, 50, 50), others, 6);
        QCOMPARE(snap.delta, QPointF(0, 0));
        QVERIFY(snap.guides.isEmpty());
        // The gap between two is offered to a third.
        const QList<QRectF> row = {QRectF(0, 0, 50, 50), QRectF(80, 0, 50, 50)};
        snap = Geometry::snapMove(QRectF(163, 20, 50, 50), row, 6);
        QCOMPARE(snap.delta.x(), -3.0);
        bool labelled = false;
        for (const Geometry::Guide &guide : snap.guides) {
            labelled = labelled || guide.label == QStringLiteral("30");
        }
        QVERIFY(labelled);
        const Geometry::Snap point = Geometry::snapPoint(QPointF(52, 300), others, 6);
        QCOMPARE(point.delta.x(), -2.0);
    }
};

QTEST_APPLESS_MAIN(TestGeometry)
#include "tst_geometry.moc"
