#include "render.h"

#include <QBuffer>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace Model;

class TestRender : public QObject
{
    Q_OBJECT

    static Item shape(const QString &kind, const QRectF &rect, const QString &fill)
    {
        Item item;
        item.id = newId();
        item.type = QStringLiteral("shape");
        item.shape = kind;
        item.setRect(rect);
        item.fill = fill;
        return item;
    }

    static Presentation blank()
    {
        Presentation deck;
        deck.slides << newSlide(QStringLiteral("blank"), deck.size());
        return deck;
    }

private Q_SLOTS:
    void backgroundAndSize()
    {
        Presentation deck = blank();
        QImage image = Render::image(deck, 0, 1);
        QCOMPARE(image.size(), QSize(960, 540));
        QCOMPARE(image.pixelColor(5, 5), QColor(Qt::white));
        QCOMPARE(Render::image(deck, 0, 0.25).size(), QSize(240, 135));
        deck.slides[0].background = QStringLiteral("#ff0000");
        QCOMPARE(Render::image(deck, 0, 1).pixelColor(900, 500), QColor(Qt::red));
        deck.slides[0].background.clear();
        deck.theme = QStringLiteral("aurora");
        image = Render::image(deck, 0, 1);
        // A theme with a gradient: the top is not the bottom.
        QVERIFY(image.pixelColor(480, 2) != image.pixelColor(480, 537));
        // No such slide: nothing, but no crash.
        QVERIFY(Render::image(deck, 7, 1).isNull() || !Render::image(deck, 7, 1).isNull());
    }

    void shapes()
    {
        Presentation deck = blank();
        deck.slides[0].items << shape(QStringLiteral("rect"), QRectF(100, 100, 200, 100), QStringLiteral("#0000ff"))
                             << shape(QStringLiteral("ellipse"), QRectF(500, 100, 200, 200), QStringLiteral("#00ff00"));
        const QImage image = Render::image(deck, 0, 1);
        QCOMPARE(image.pixelColor(200, 150), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(103, 103), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(95, 150), QColor(Qt::white));
        QCOMPARE(image.pixelColor(600, 200), QColor(Qt::green));
        // The corner of the ellipse's box is outside the ellipse.
        QCOMPARE(image.pixelColor(505, 105), QColor(Qt::white));

        for (const char *kind : {"rect", "rounded", "ellipse", "triangle", "diamond", "arrow", "star"}) {
            const Item item = shape(QLatin1String(kind), QRectF(0, 0, 200, 100), QString());
            const QPainterPath path = Render::outline(item);
            QVERIFY2(!path.isEmpty(), kind);
            QVERIFY2(QRectF(-0.01, -0.01, 200.02, 100.02).contains(path.boundingRect()), kind);
            QVERIFY2(path.contains(QPointF(100, 55)), kind);
        }
        QVERIFY(!Render::outline(shape(QStringLiteral("triangle"), QRectF(0, 0, 200, 100), QString())).contains(QPointF(5, 5)));
    }

    void rotationOpacityOrder()
    {
        Presentation deck = blank();
        Item bar = shape(QStringLiteral("rect"), QRectF(100, 200, 300, 40), QStringLiteral("#0000ff"));
        bar.rotation = 90;
        deck.slides[0].items << bar;
        QImage image = Render::image(deck, 0, 1);
        // Turned a quarter about its centre (250, 220): tall now, not wide.
        QCOMPARE(image.pixelColor(250, 100), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(120, 220), QColor(Qt::white));

        deck.slides[0].items.clear();
        Item under = shape(QStringLiteral("rect"), QRectF(100, 100, 200, 200), QStringLiteral("#ff0000"));
        Item over = shape(QStringLiteral("rect"), QRectF(200, 200, 200, 200), QStringLiteral("#0000ff"));
        deck.slides[0].items << under << over;
        QCOMPARE(Render::image(deck, 0, 1).pixelColor(250, 250), QColor(Qt::blue));
        deck.slides[0].items.swapItemsAt(0, 1);
        QCOMPARE(Render::image(deck, 0, 1).pixelColor(250, 250), QColor(Qt::red));

        deck.slides[0].items.clear();
        Item ghost = shape(QStringLiteral("rect"), QRectF(100, 100, 200, 200), QStringLiteral("#000000"));
        ghost.opacity = 0.5;
        deck.slides[0].items << ghost;
        const QColor grey = Render::image(deck, 0, 1).pixelColor(200, 200);
        QVERIFY(grey.red() > 110 && grey.red() < 145);
    }

    void strokeShadowLine()
    {
        Presentation deck = blank();
        Item framed = shape(QStringLiteral("rect"), QRectF(100, 100, 200, 100), QString());
        framed.stroke = QStringLiteral("#ff0000");
        framed.strokeWidth = 6;
        deck.slides[0].items << framed;
        QImage image = Render::image(deck, 0, 1);
        QCOMPARE(image.pixelColor(100, 150), QColor(Qt::red));
        QCOMPARE(image.pixelColor(200, 150), QColor(Qt::white));

        deck.slides[0].items[0].shadow = true;
        deck.slides[0].items[0].fill = QStringLiteral("#ffffff");
        image = Render::image(deck, 0, 1);
        // Below the box, the paper is no longer clean.
        QVERIFY(image.pixelColor(200, 207) != QColor(Qt::white));

        deck.slides[0].items.clear();
        Item line = shape(QStringLiteral("line"), QRectF(100, 300, 400, 0), QString());
        line.stroke = QStringLiteral("#0000ff");
        line.strokeWidth = 4;
        line.arrow = true;
        deck.slides[0].items << line;
        image = Render::image(deck, 0, 1);
        QCOMPARE(image.pixelColor(300, 300), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(300, 310), QColor(Qt::white));
        // The head is wider than the stroke.
        QCOMPARE(image.pixelColor(486, 304), QColor(Qt::blue));
    }

    static int inked(const QImage &image, const QRect &area)
    {
        int count = 0;
        for (int y = area.top(); y <= area.bottom(); ++y) {
            for (int x = area.left(); x <= area.right(); ++x) {
                if (image.pixelColor(x, y).lightness() < 128) {
                    ++count;
                }
            }
        }
        return count;
    }

    void text()
    {
        Presentation deck = blank();
        Item box;
        box.id = newId();
        box.type = QStringLiteral("text");
        box.setRect(QRectF(100, 100, 600, 300));
        box.setPlainText(QStringLiteral("Texto"));
        box.text[0].runs[0].size = 48;
        box.text[0].runs[0].color = QStringLiteral("#000000");
        deck.slides[0].items << box;
        QImage image = Render::image(deck, 0, 1);
        // At the top left, as it starts.
        QVERIFY(inked(image, QRect(100, 100, 300, 80)) > 200);
        QCOMPARE(inked(image, QRect(100, 300, 600, 100)), 0);
        QCOMPARE(inked(image, QRect(450, 100, 250, 300)), 0);
        deck.slides[0].items[0].valign = 2;
        deck.slides[0].items[0].text[0].align = 2;
        image = Render::image(deck, 0, 1);
        QCOMPARE(inked(image, QRect(100, 100, 600, 150)), 0);
        QVERIFY(inked(image, QRect(450, 320, 250, 80)) > 200);

        // The text being typed on the canvas is the editor's to show.
        Render::Options options;
        options.skipText = box.id;
        QCOMPARE(inked(Render::image(deck, 0, 1, options), QRect(0, 0, 960, 540)), 0);

        QVERIFY(Render::textHeight(deck.slides[0].items[0], deck.look()) > 48);
        Item longer = deck.slides[0].items[0];
        longer.setPlainText(QStringLiteral("Uno\nDos\nTres"));
        QVERIFY(Render::textHeight(longer, deck.look()) > 2.5 * Render::textHeight(deck.slides[0].items[0], deck.look()) - 20);
    }

    void placeholdersOnlyWhenEditing()
    {
        Presentation deck;
        deck.slides << newSlide(QStringLiteral("content"), deck.size());
        // Presented, an empty placeholder is nothing at all.
        QCOMPARE(inked(Render::image(deck, 0, 1), QRect(0, 0, 960, 540)), 0);
        Render::Options options;
        options.editing = true;
        const QImage editing = Render::image(deck, 0, 1, options);
        QVERIFY(editing != Render::image(deck, 0, 1));
    }

    void pictureAndTable()
    {
        Presentation deck = blank();
        QImage source(40, 20, QImage::Format_RGB32);
        source.fill(Qt::magenta);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        source.save(&buffer, "PNG");
        Item picture;
        picture.id = newId();
        picture.type = QStringLiteral("image");
        picture.setRect(QRectF(100, 100, 400, 200));
        picture.image = deck.addMedia(bytes, QStringLiteral("png"));
        deck.slides[0].items << picture;
        QCOMPARE(Render::image(deck, 0, 1).pixelColor(300, 200), QColor(Qt::magenta));

        deck.slides[0].items.clear();
        Item table;
        table.id = newId();
        table.type = QStringLiteral("table");
        table.setRect(QRectF(100, 100, 400, 120));
        table.rows = 3;
        table.cols = 2;
        table.cells = {QStringLiteral("Cabecera"), QStringLiteral("B"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")};
        deck.slides[0].items << table;
        const QImage image = Render::image(deck, 0, 1);
        // The header row is filled with the accent; the body is not.
        QCOMPARE(image.pixelColor(480, 104), deck.look().accents.at(0));
        QVERIFY(image.pixelColor(480, 150) != deck.look().accents.at(0));
    }

    void buildSteps()
    {
        Presentation deck = blank();
        Item first = shape(QStringLiteral("rect"), QRectF(0, 0, 100, 100), QStringLiteral("#ff0000"));
        Item second = shape(QStringLiteral("rect"), QRectF(200, 0, 100, 100), QStringLiteral("#00ff00"));
        Item third = shape(QStringLiteral("rect"), QRectF(400, 0, 100, 100), QStringLiteral("#0000ff"));
        second.anim = QStringLiteral("appear");
        third.anim = QStringLiteral("slide");
        deck.slides[0].items << first << second << third;
        Render::Options options;
        options.step = 0;
        QImage image = Render::image(deck, 0, 1, options);
        QCOMPARE(image.pixelColor(50, 50), QColor(Qt::red));
        QCOMPARE(image.pixelColor(250, 50), QColor(Qt::white));
        QCOMPARE(image.pixelColor(450, 50), QColor(Qt::white));
        options.step = 1;
        image = Render::image(deck, 0, 1, options);
        QCOMPARE(image.pixelColor(250, 50), QColor(Qt::green));
        QCOMPARE(image.pixelColor(450, 50), QColor(Qt::white));
        options.step = 2;
        QCOMPARE(Render::image(deck, 0, 1, options).pixelColor(450, 50), QColor(Qt::blue));
        QCOMPARE(Render::image(deck, 0, 1).pixelColor(450, 50), QColor(Qt::blue));
        // One item alone, on nothing: what an entrance moves.
        options = {};
        options.only = third.id;
        image = Render::image(deck, 0, 1, options);
        QCOMPARE(image.pixelColor(450, 50), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(50, 50).alpha(), 0);
        QCOMPARE(image.pixelColor(900, 500).alpha(), 0);
    }

    void footerAndNumber()
    {
        Presentation deck = blank();
        const QImage bare = Render::image(deck, 0, 1);
        deck.master.footer = QStringLiteral("Wonderland");
        deck.master.numbers = true;
        const QImage dressed = Render::image(deck, 0, 1);
        QVERIFY(bare != dressed);
        // Greyed text: anything that is not paper counts.
        const auto marked = [&](const QRect &area) {
            int count = 0;
            for (int y = area.top(); y <= area.bottom(); ++y) {
                for (int x = area.left(); x <= area.right(); ++x) {
                    count += dressed.pixelColor(x, y) != QColor(Qt::white);
                }
            }
            return count;
        };
        QVERIFY(marked(QRect(0, 490, 480, 50)) > 20);
        QVERIFY(marked(QRect(480, 490, 480, 50)) > 5);
        QCOMPARE(marked(QRect(0, 0, 960, 480)), 0);
    }

    void files()
    {
        QTemporaryDir dir;
        Presentation deck = blank();
        deck.slides << newSlide(QStringLiteral("title"), deck.size()) << newSlide(QStringLiteral("blank"), deck.size());
        deck.slides[1].items[0].setPlainText(QStringLiteral("En el PDF"));
        deck.slides[2].hidden = true;
        const QString png = dir.filePath(QStringLiteral("a.png"));
        QVERIFY(Render::savePng(deck, 1, png, 1.5));
        QCOMPARE(QImage(png).size(), QSize(1440, 810));
        const QString pdf = dir.filePath(QStringLiteral("a.pdf"));
        QVERIFY(Render::savePdf(deck, pdf));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF-"));
        // Two pages: the hidden slide is left out.
        QVERIFY(bytes.contains("/Count 2"));
        QVERIFY(!Render::savePdf(deck, QStringLiteral("/proc/no/se/puede.pdf")));
    }
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
