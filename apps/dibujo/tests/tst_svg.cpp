#include "exporter.h"
#include "geometry.h"
#include "svg.h"
#include "svg_samples.h"
#include "templates.h"

#include <QBuffer>
#include <QDirIterator>
#include <QDomDocument>
#include <QSvgRenderer>
#include <QtTest>

#include <cmath>

class TestSvg : public QObject
{
    Q_OBJECT

    static QList<Shape> read(const QByteArray &svg, QMap<QString, QByteArray> *resources = nullptr)
    {
        QList<Shape> shapes;
        QMap<QString, QByteArray> own;
        QString error;
        if (!Svg::read(svg, &shapes, resources ? resources : &own, &error)) {
            qWarning() << error;
        }
        return shapes;
    }
    static bool near(double a, double b, double slack = 0.6)
    {
        return qAbs(a - b) <= slack;
    }

private Q_SLOTS:
    /// LibreOffice puts the place and the look of a text on spans inside spans.
    void nestedSpans()
    {
        const QByteArray svg = Samples::NestedSpans;
        QList<Shape> shapes;
        QMap<QString, QByteArray> resources;
        QString error;
        QVERIFY2(Svg::read(svg, &shapes, &resources, &error, QString()), qPrintable(error));
        QCOMPARE(shapes.size(), 1);
        const Shape &s = shapes.first();
        QCOMPARE(s.kind, QStringLiteral("text"));
        QCOMPARE(s.text, QStringLiteral("Proceso"));
        QCOMPARE(s.style.fontSize, 24.0);
        QVERIFY(s.style.bold);
        QCOMPARE(s.style.textColor, QColor(200, 0, 0));
        // The baseline, not the top, is at y.
        QVERIFY(s.x > 30 && s.x < 40);
        QVERIFY(s.y < 80 && s.y + s.h > 80);
    }

    void basicShapes()
    {
        const QList<Shape> shapes = read(Samples::Basic);
        QCOMPARE(shapes.size(), 8);
        const Shape &rect = shapes.at(0);
        QCOMPARE(rect.kind, QStringLiteral("shape"));
        QCOMPARE(rect.form, QStringLiteral("rect"));
        QCOMPARE(rect.box(), QRectF(10, 20, 100, 50));
        QCOMPARE(rect.style.radius, 8.0);
        QCOMPARE(rect.style.fill, QColor(Qt::red));
        QCOMPARE(rect.style.stroke, QColor(Qt::blue));
        QCOMPARE(rect.style.strokeWidth, 3.0);
        const Shape &circle = shapes.at(1);
        QCOMPARE(circle.form, QStringLiteral("ellipse"));
        QCOMPARE(circle.box(), QRectF(160, 60, 80, 80));
        QCOMPARE(circle.style.fillKind, QStringLiteral("none"));
        const Shape &ellipse = shapes.at(2);
        QCOMPARE(ellipse.box(), QRectF(240, 180, 120, 40));
        QCOMPARE(ellipse.style.fill, QColor(0, 128, 0));
        QCOMPARE(ellipse.style.opacity, 0.5);
        // No stroke in the file, none here.
        QCOMPARE(ellipse.style.strokeWidth, 0.0);
        const Shape &line = shapes.at(3);
        QVERIFY(line.isLine());
        QCOMPARE(line.p1, QPointF(0, 0));
        QCOMPARE(line.p2, QPointF(50, 80));
        QVERIFY(line.style.dash != QLatin1String("solid"));
        QCOMPARE(shapes.at(4).kind, QStringLiteral("path"));
        QCOMPARE(shapes.at(4).box(), QRectF(0, 0, 20, 10));
        QCOMPARE(shapes.at(5).box(), QRectF(100, 100, 50, 50));
        QCOMPARE(shapes.at(5).style.fill, QColor(Qt::yellow));
        QCOMPARE(shapes.at(6).kind, QStringLiteral("path"));
        QCOMPARE(shapes.at(6).style.strokeWidth, 4.0);
        const Shape &text = shapes.at(7);
        QCOMPARE(text.kind, QStringLiteral("text"));
        QCOMPARE(text.text, QStringLiteral("Hola SVG"));
        QCOMPARE(text.style.fontSize, 18.0);
        QVERIFY(text.style.bold);
        QCOMPARE(text.style.textColor, QColor(QStringLiteral("#112233")));
        // The baseline of the file is where the words sit.
        QVERIFY(text.y < 290 && text.y + text.h >= 289);
        QVERIFY(near(text.x, 20, 5));
    }
    void groupsAndTransforms()
    {
        const QList<Shape> shapes = read(Samples::Grouped);
        QCOMPARE(shapes.size(), 4);
        // The viewBox is half the size of the picture: everything doubles.
        QCOMPARE(shapes.at(0).box(), QRectF(20, 40, 20, 20));
        QCOMPARE(shapes.at(0).style.fill, QColor(Qt::green));
        QCOMPARE(shapes.at(0).style.strokeWidth, 2.0);
        QCOMPARE(shapes.at(1).box(), QRectF(40, 60, 40, 40));
        QCOMPARE(shapes.at(1).style.fill, QColor(Qt::blue));
        // What a group held comes in as a group.
        QVERIFY(!shapes.at(0).group.isEmpty());
        // A turned rectangle is still a rectangle, turned.
        const Shape &turned = shapes.at(2);
        QCOMPARE(turned.form, QStringLiteral("rect"));
        QVERIFY(near(std::fmod(turned.rotation + 360, 180), 90));
        QVERIFY(near(turned.bounds().width(), 20) && near(turned.bounds().height(), 40));
        QVERIFY(near(turned.center().x(), 100) && near(turned.center().y(), 90));
        QCOMPARE(shapes.at(3).box(), QRectF(140, 140, 20, 20));
        // Black is what SVG fills with when nothing is said.
        QCOMPARE(shapes.at(3).style.fill, QColor(Qt::black));
    }
    void stylesAndGradients()
    {
        const QList<Shape> shapes = read(Samples::Styled);
        QCOMPARE(shapes.size(), 4);
        QCOMPARE(shapes.at(0).style.fill, QColor(QStringLiteral("#ff8800")));
        QCOMPARE(shapes.at(0).style.strokeWidth, 0.0);
        QCOMPARE(shapes.at(1).style.fill, QColor(QStringLiteral("#0088ff")));
        QCOMPARE(shapes.at(2).style.fillKind, QStringLiteral("linear"));
        QCOMPARE(shapes.at(2).style.fill, QColor(Qt::white));
        QCOMPARE(shapes.at(2).style.fill2, QColor(Qt::blue));
        QCOMPARE(shapes.at(3).style.fill.rgb(), QColor(QStringLiteral("#abcdef")).rgb());
        QVERIFY(qAbs(shapes.at(3).style.fill.alphaF() - 0.5) < 0.01);
    }
    void images()
    {
        QImage picture(4, 4, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
        const QByteArray svg = "<svg xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='50' height='50'>"
                               "<image x='5' y='6' width='20' height='10' xlink:href='data:image/png;base64," + png.toBase64() + "'/></svg>";
        QMap<QString, QByteArray> resources;
        const QList<Shape> shapes = read(svg, &resources);
        QCOMPARE(shapes.size(), 1);
        QCOMPARE(shapes.first().kind, QStringLiteral("image"));
        QCOMPARE(shapes.first().box(), QRectF(5, 6, 20, 10));
        QCOMPARE(resources.value(shapes.first().image), png);
    }
    void refusals()
    {
        QList<Shape> shapes;
        QMap<QString, QByteArray> resources;
        QString error;
        QVERIFY(!Svg::read("<html><body/></html>", &shapes, &resources, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Svg::read("<svg><rect", &shapes, &resources, &error));
        QVERIFY(shapes.isEmpty());
    }
    void writesWhatQtReads()
    {
        const Document doc = Templates::make(QStringLiteral("sample"));
        const QRectF area = Exporter::area(doc, QString());
        const QByteArray svg = Svg::write(doc, area);
        QDomDocument dom;
        QVERIFY(bool(dom.setContent(svg)));
        QCOMPARE(dom.documentElement().tagName(), QStringLiteral("svg"));
        QCOMPARE(dom.documentElement().attribute(QStringLiteral("width")).toDouble(), std::round(area.width() * 100) / 100);
        // The words are still words.
        QVERIFY(dom.elementsByTagName(QStringLiteral("text")).size() >= 10);
        QVERIFY(svg.contains("Proceso de pedido"));
        QSvgRenderer renderer(svg);
        QVERIFY(renderer.isValid());
        QVERIFY(near(renderer.defaultSize().width(), area.width(), 1));
    }
    void roundTrip()
    {
        // Out and back in, every object is where it was and looks the same.
        const Document doc = Templates::make(QStringLiteral("sample"));
        const QRectF area = Exporter::area(doc, QString());
        const QList<Shape> back = read(Svg::write(doc, area));
        QList<Shape> visible;
        for (const Shape &s : doc.shapes) {
            visible << s;
        }
        // A shape with words comes back as the shape and its words.
        int words = 0;
        int others = 0;
        for (const Shape &s : back) {
            (s.kind == QLatin1String("text") ? words : others)++;
        }
        int expectedWords = 0;
        for (const Shape &s : visible) {
            expectedWords += s.text.isEmpty() ? 0 : 1;
        }
        QVERIFY2(words >= expectedWords, qPrintable(QStringLiteral("%1 < %2").arg(words).arg(expectedWords)));
        QVERIFY2(others >= visible.size() - 4, qPrintable(QStringLiteral("%1 of %2").arg(others).arg(visible.size())));
        // The first frame, moved by the corner of the area.
        const Shape *frame = doc.frames().first();
        bool found = false;
        for (const Shape &s : back) {
            found = found || (near(s.x, frame->x - area.x()) && near(s.y, frame->y - area.y()) && near(s.w, frame->w) && near(s.h, frame->h));
        }
        QVERIFY(found);
    }
    void systemFiles()
    {
        // Pictures other programs made: each must read into something that
        // covers about the same ground as Qt's own reading of it.
        QStringList files;
        for (const QString &root : {QStringLiteral("/usr/share/icons/breeze/apps/48"), QStringLiteral("/usr/share/icons/hicolor/scalable/apps"),
                                    QStringLiteral("/usr/share/icons/Adwaita/scalable/devices")}) {
            QDirIterator it(root, {QStringLiteral("*.svg")}, QDir::Files);
            int taken = 0;
            while (it.hasNext() && taken < 25) {
                files << it.next();
                ++taken;
            }
        }
        if (files.isEmpty()) {
            QSKIP("no SVG files on this system");
        }
        int read = 0;
        for (const QString &path : std::as_const(files)) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray bytes = file.readAll();
            QSvgRenderer renderer(bytes);
            if (!renderer.isValid()) {
                continue;
            }
            QList<Shape> shapes;
            QMap<QString, QByteArray> resources;
            QString error;
            QVERIFY2(Svg::read(bytes, &shapes, &resources, &error, QFileInfo(path).path()), qPrintable(path + ": " + error));
            QVERIFY2(!shapes.isEmpty(), qPrintable(path));
            QRectF bounds;
            for (const Shape &s : std::as_const(shapes)) {
                QVERIFY2(std::isfinite(s.x) && std::isfinite(s.y) && std::isfinite(s.w) && std::isfinite(s.h), qPrintable(path));
                bounds |= s.bounds();
            }
            const QSizeF size = renderer.defaultSize();
            QVERIFY2(bounds.width() <= size.width() * 2.5 + 2 && bounds.height() <= size.height() * 2.5 + 2,
                     qPrintable(QStringLiteral("%1: %2x%3 at %4,%5").arg(path).arg(bounds.width()).arg(bounds.height()).arg(bounds.x()).arg(bounds.y())));
            QVERIFY2(bounds.width() >= size.width() * 0.2 || bounds.height() >= size.height() * 0.2, qPrintable(path));
            ++read;
        }
        QVERIFY(read >= 1);
        qInfo() << "system SVG files read:" << read << "of" << files.size();
    }
    void exports()
    {
        QTemporaryDir dir;
        const Document doc = Templates::make(QStringLiteral("flow"));
        const Shape *frame = doc.frames().first();
        QCOMPARE(Exporter::area(doc, frame->id), frame->box());
        // Everything, with air around.
        const QRectF all = Exporter::area(doc, QString());
        QVERIFY(all.contains(doc.bounds()));
        QVERIFY(all.width() > doc.bounds().width());

        QString error;
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY2(Exporter::png(doc, frame->box(), 2, false, png, &error), qPrintable(error));
        const QImage image(png);
        QCOMPARE(image.size(), QSize(int(frame->w * 2), int(frame->h * 2)));
        QCOMPARE(image.pixelColor(3, 3).alpha(), 255);
        // Something is drawn on it.
        QSet<QRgb> colours;
        for (int y = 0; y < image.height(); y += 9) {
            for (int x = 0; x < image.width(); x += 9) {
                colours << image.pixel(x, y);
            }
        }
        QVERIFY(colours.size() > 5);
        const QString clear = dir.filePath(QStringLiteral("b.png"));
        QVERIFY(Exporter::png(doc, all, 1, true, clear, &error));
        QCOMPARE(QImage(clear).pixelColor(1, 1).alpha(), 0);

        const QString svg = dir.filePath(QStringLiteral("a.svg"));
        QVERIFY(Exporter::svg(doc, frame->box(), svg, &error));
        QVERIFY(QSvgRenderer(svg).isValid());

        const QString pdf = dir.filePath(QStringLiteral("a.pdf"));
        QVERIFY2(Exporter::pdf(doc, {frame->box(), QRectF(0, 0, 400, 400)}, pdf, &error), qPrintable(error));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF-"));
        // Two pages, each of its own size.
        QVERIFY(bytes.contains("/Count 2"));
        QVERIFY(!Exporter::png(doc, QRectF(), 1, false, png, &error));
        QVERIFY(!Exporter::pdf(doc, {}, pdf, &error));
    }
};

QTEST_MAIN(TestSvg)
#include "tst_svg.moc"
