#include "ink.h"

#include <QImage>
#include <QPainter>
#include <QTest>

#include <cmath>

class TestInk : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void smooth()
    {
        // A straight line drawn with a shaky hand, with many points.
        QList<QPointF> raw;
        for (int i = 0; i <= 200; ++i) {
            raw << QPointF(10 + i, 50 + ((i % 2) ? 0.4 : -0.4));
        }
        const QList<QPointF> eased = Drawing::smooth(raw);
        QVERIFY2(eased.size() < 12, qPrintable(QString::number(eased.size())));
        QCOMPARE(eased.first(), raw.first());
        QCOMPARE(eased.last(), raw.last());
        for (const QPointF &point : eased) {
            QVERIFY(std::abs(point.y() - 50) < 0.5);
        }
        // A corner stays a corner.
        QList<QPointF> corner;
        for (int i = 0; i <= 50; ++i) corner << QPointF(i * 2, 0);
        for (int i = 1; i <= 50; ++i) corner << QPointF(100, i * 2);
        const QList<QPointF> bent = Drawing::smooth(corner);
        bool close = false;
        for (const QPointF &point : bent) {
            close = close || QLineF(point, QPointF(100, 0)).length() < 3;
        }
        QVERIFY(close);
        // A dot and a dash survive.
        QCOMPARE(Drawing::smooth({QPointF(5, 5)}).size(), 1);
        QCOMPARE(Drawing::smooth({QPointF(5, 5), QPointF(5.2, 5.1)}).size(), 1);
        QCOMPARE(Drawing::smooth({QPointF(0, 0), QPointF(30, 0)}).size(), 2);
        QVERIFY(Drawing::smooth({}).isEmpty());
    }

    void svgRoundTrip()
    {
        Drawing drawing;
        drawing.width = 400;
        drawing.height = 200;
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#ff3b30"), 2.5, {QPointF(10, 10), QPointF(50.5, 80.3), QPointF(120, 40)}};
        drawing.strokes << Stroke{QStringLiteral("marker"), QStringLiteral("#ffd60a"), 14, {QPointF(20, 150), QPointF(300, 150)}};
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#1d1d1f"), 4, {QPointF(350, 20)}};
        const QByteArray svg = drawing.toSvg();
        QVERIFY(svg.contains("<svg xmlns=\"http://www.w3.org/2000/svg\""));
        QVERIFY(svg.contains("stroke-opacity=\"0.4\""));
        QVERIFY(svg.contains("d=\"M10 10Q50.5 80.3 85.3 60.1L120 40\""));
        bool ok = false;
        const Drawing back = Drawing::fromSvg(svg, &ok);
        QVERIFY(ok);
        QVERIFY(back == drawing);
        // Someone else's SVG is not a drawing of ours.
        Drawing::fromSvg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"><path d=\"M0 0L5 5\"/></svg>", &ok);
        QVERIFY(!ok);
        Drawing::fromSvg("not xml at all <", &ok);
        QVERIFY(!ok);
    }

    void svgRenders()
    {
        // What is written is an SVG that Qt's own reader draws.
        Drawing drawing;
        drawing.width = 100;
        drawing.height = 60;
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#ff0000"), 6, {QPointF(10, 30), QPointF(90, 30)}};
        const QImage image = QImage::fromData(drawing.toSvg(), "svg");
        QVERIFY(!image.isNull());
        QCOMPARE(image.size(), QSize(100, 60));
        QCOMPARE(image.pixelColor(50, 30), QColor(255, 0, 0));
        QCOMPARE(image.pixelColor(50, 10), QColor(255, 255, 255));
        // And paint() draws the same.
        QImage painted(100, 60, QImage::Format_ARGB32_Premultiplied);
        painted.fill(Qt::white);
        QPainter painter(&painted);
        drawing.paint(&painter);
        painter.end();
        QCOMPARE(painted.pixelColor(50, 30), QColor(255, 0, 0));
        QCOMPARE(painted.pixelColor(50, 10), QColor(255, 255, 255));
    }

    void erase()
    {
        Drawing drawing;
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#000000"), 2, {QPointF(0, 0), QPointF(100, 0)}};
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#000000"), 2, {QPointF(0, 50), QPointF(100, 50)}};
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#000000"), 2, {QPointF(200, 200)}};
        QCOMPARE(drawing.eraseAt(QPointF(50, 30), 8), 0);
        QCOMPARE(drawing.eraseAt(QPointF(50, 45), 8), 1);
        QCOMPARE(drawing.strokes.size(), 2);
        QCOMPARE(drawing.eraseAt(QPointF(203, 203), 8), 1);
        // Past the end of the line is not on the line.
        QCOMPARE(drawing.eraseAt(QPointF(130, 0), 8), 0);
        QCOMPARE(drawing.strokes.size(), 1);
    }

    void crop()
    {
        Drawing drawing;
        drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#000000"), 4, {QPointF(100, 200), QPointF(300, 260)}};
        drawing.crop(10);
        QCOMPARE(drawing.strokes.first().points.first(), QPointF(12, 12));
        QCOMPARE(drawing.width, 204 + 21);
        QCOMPARE(drawing.height, 64 + 21);
        Drawing empty;
        empty.crop();
        QCOMPARE(empty.width, 720);
    }
};

QTEST_MAIN(TestInk)
#include "tst_ink.moc"
