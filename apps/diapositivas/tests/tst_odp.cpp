#include "odp.h"
#include "zip.h"

#include <QBuffer>
#include <QDomDocument>
#include <QImage>
#include <QtTest>

using namespace Model;

class TestOdp : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void readsLibreOffice()
    {
        QFile file(QStringLiteral(TEST_DATA "/libreoffice.odp"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Presentation deck;
        QString error;
        QVERIFY2(Odp::read(file.readAll(), deck, &error), qPrintable(error));
        QCOMPARE(deck.slides.size(), 3);
        QCOMPARE(deck.height, 540.0);
        QVERIFY(qAbs(deck.width - 960) < 0.5);

        const Slide &title = deck.slides.at(0);
        QStringList texts;
        for (const Item &item : title.items) {
            texts << item.plainText();
            QVERIFY(item.w > 0 && item.h > 0);
        }
        QVERIFY(texts.contains(QStringLiteral("Informe anual")));
        QVERIFY(texts.contains(QStringLiteral("Hecho con python-pptx")));

        const Slide &points = deck.slides.at(1);
        QCOMPARE(points.notes, QStringLiteral("Notas del orador"));
        bool list = false;
        for (const Item &item : points.items) {
            if (item.plainText() == QLatin1String("Primero\nSegundo")) {
                list = true;
                QVERIFY(item.text.at(0).bullet);
                QCOMPARE(item.text.at(1).level, 1);
            }
        }
        QVERIFY(list);

        const Slide &free = deck.slides.at(2);
        QCOMPARE(free.background, QStringLiteral("#223344"));
        int pictures = 0, tables = 0, lines = 0, ellipses = 0, boxes = 0;
        for (const Item &item : free.items) {
            if (item.type == QLatin1String("image")) {
                ++pictures;
                QCOMPARE(QImage::fromData(deck.media.value(item.image)).size(), QSize(120, 60));
                QVERIFY(qAbs(item.w - 144) < 0.5 && qAbs(item.h - 72) < 0.5);
            } else if (item.type == QLatin1String("table")) {
                ++tables;
                QCOMPARE(item.rows, 2);
                QCOMPARE(item.cols, 3);
                QCOMPARE(item.cell(1, 2), QStringLiteral("f1c2"));
            } else if (item.shape == QLatin1String("line")) {
                ++lines;
                QVERIFY(qAbs(item.line().x1() - 72) < 0.5 && qAbs(item.line().y2() - 432) < 0.5);
            } else if (item.shape == QLatin1String("ellipse")) {
                ++ellipses;
                QCOMPARE(item.fill, QStringLiteral("#0080ff"));
                QCOMPARE(item.plainText(), QStringLiteral("Elipse"));
                QVERIFY(qAbs(item.rotation - 30) < 0.5);
                QVERIFY(qAbs(item.center().x() - 504) < 1 && qAbs(item.center().y() - 126) < 1);
            } else if (item.type == QLatin1String("text")) {
                ++boxes;
                QCOMPARE(item.plainText(), QStringLiteral("Negrita roja y cursiva"));
                const Run first = item.text.at(0).runs.at(0);
                QCOMPARE(first.bold, 1);
                QCOMPARE(first.size, 28.0);
                QCOMPARE(first.color, QStringLiteral("#ff0000"));
                QVERIFY(item.text.at(0).runs.last().italic);
            }
        }
        QCOMPARE(pictures, 1);
        QCOMPARE(tables, 1);
        QCOMPARE(lines, 1);
        QCOMPARE(ellipses, 1);
        QCOMPARE(boxes, 1);
    }

    void writesWhatItReads()
    {
        Presentation deck;
        deck.theme = QStringLiteral("aurora");
        deck.master.footer = QStringLiteral("Pie <de> página");
        deck.master.numbers = true;

        Slide title = newSlide(QStringLiteral("title"), deck.size());
        title.items[0].setPlainText(QStringLiteral("Título <con> & «signos»"));
        title.items[1].setPlainText(QStringLiteral("Dos  espacios\ty un tabulador"));
        title.transition = QStringLiteral("push");
        deck.slides << title;

        Slide content = newSlide(QStringLiteral("content"), deck.size());
        content.items[0].setPlainText(QStringLiteral("Contenido"));
        Run plain;
        plain.text = QStringLiteral("Normal ");
        Run strong;
        strong.text = QStringLiteral("fuerte");
        strong.bold = 1;
        strong.italic = true;
        strong.underline = true;
        strong.strike = true;
        strong.size = 40;
        strong.color = QStringLiteral("#ff0000");
        strong.font = QStringLiteral("Georgia");
        Paragraph first;
        first.bullet = true;
        first.runs << plain << strong;
        Paragraph second;
        second.bullet = true;
        second.level = 1;
        second.align = 2;
        Run more;
        more.text = QStringLiteral("Segundo nivel");
        second.runs << more;
        content.items[1].text = {first, second};
        content.notes = QStringLiteral("Primera línea\nSegunda línea");
        content.background = QStringLiteral("#102030");
        content.transition = QStringLiteral("fade");
        deck.slides << content;

        Slide blank = newSlide(QStringLiteral("blank"), deck.size());
        blank.hidden = true;
        blank.transition = QStringLiteral("dissolve");
        int n = 0;
        for (const char *shape : {"rect", "rounded", "ellipse", "triangle", "diamond", "arrow", "star"}) {
            Item item;
            item.id = newId();
            item.type = QStringLiteral("shape");
            item.shape = QLatin1String(shape);
            item.setRect(QRectF(40 + n * 120, 40, 100, 80));
            item.fill = QStringLiteral("#3366cc");
            item.valign = 1;
            ++n;
            blank.items << item;
        }
        Item &styled = blank.items[0];
        styled.rotation = 30;
        styled.stroke = QStringLiteral("#000000");
        styled.strokeWidth = 3;
        styled.shadow = true;
        styled.opacity = 0.5;
        styled.setPlainText(QStringLiteral("Dentro"));
        blank.items[5].flipH = true;
        blank.items[3].group = QStringLiteral("g");
        blank.items[4].group = QStringLiteral("g");

        Item line;
        line.id = newId();
        line.type = QStringLiteral("shape");
        line.shape = QStringLiteral("line");
        line.setRect(QRectF(100, 200, 300, 100));
        line.flipV = true;
        line.arrow = true;
        line.stroke = QStringLiteral("#00aa00");
        line.strokeWidth = 2;
        blank.items << line;

        QImage image(120, 60, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        Item picture;
        picture.id = newId();
        picture.type = QStringLiteral("image");
        picture.image = deck.addMedia(png, QStringLiteral("png"));
        picture.setRect(QRectF(500, 300, 240, 120));
        picture.shadow = true;
        blank.items << picture;

        Item table;
        table.id = newId();
        table.type = QStringLiteral("table");
        table.rows = 2;
        table.cols = 3;
        table.cells = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("1"), QString(), QStringLiteral("3 & 4")};
        table.fill = QStringLiteral("#aa0000");
        table.setRect(QRectF(60, 360, 360, 80));
        blank.items << table;
        deck.slides << blank;

        const QByteArray bytes = Odp::write(deck);
        QVERIFY(!bytes.isEmpty());
        const ZipReader zip(bytes);
        QVERIFY(zip.isValid());
        // The type comes first and unpacked: that is how the archive is told apart.
        QCOMPARE(zip.names().first(), QStringLiteral("mimetype"));
        QCOMPARE(bytes.mid(30, 8), QByteArrayLiteral("mimetype"));
        QCOMPARE(bytes.mid(38, 47), QByteArrayLiteral("application/vnd.oasis.opendocument.presentation"));
        for (const char *part : {"content.xml", "styles.xml", "meta.xml", "META-INF/manifest.xml"}) {
            QDomDocument document;
            QVERIFY2(bool(document.setContent(zip.file(QLatin1String(part)), QDomDocument::ParseOption::UseNamespaceProcessing)), part);
        }

        Presentation back;
        QString error;
        QVERIFY2(Odp::read(bytes, back, &error), qPrintable(error));
        QCOMPARE(back.theme, deck.theme);
        QCOMPARE(back.master, deck.master);
        QCOMPARE(back.width, deck.width);
        QCOMPARE(back.height, deck.height);
        QCOMPARE(back.slides.size(), 3);
        QCOMPARE(back.media.size(), 1);

        const Slide &t = back.slides.at(0);
        QCOMPARE(t.layout, QStringLiteral("content"));
        QCOMPARE(t.transition, QStringLiteral("push"));
        QCOMPARE(t.items.size(), 2);
        QCOMPARE(t.items.at(0).role, QStringLiteral("ctitle"));
        QCOMPARE(t.items.at(0).plainText(), QStringLiteral("Título <con> & «signos»"));
        QCOMPARE(t.items.at(1).role, QStringLiteral("subtitle"));
        QCOMPARE(t.items.at(1).plainText(), QStringLiteral("Dos  espacios\ty un tabulador"));
        // What the theme gives is not kept as a format of the text's own.
        QCOMPARE(t.items.at(0).text, title.items.at(0).text);
        QCOMPARE(t.items.at(0).rect(), title.items.at(0).rect());

        const Slide &c = back.slides.at(1);
        QCOMPARE(c.notes, content.notes);
        QCOMPARE(c.background, content.background);
        QCOMPARE(c.transition, QStringLiteral("fade"));
        QVERIFY(!c.hidden);
        QCOMPARE(c.items.at(1).role, QStringLiteral("body"));
        QCOMPARE(c.items.at(1).text, content.items.at(1).text);

        const Slide &b = back.slides.at(2);
        QVERIFY(b.hidden);
        QCOMPARE(b.transition, QStringLiteral("dissolve"));
        QCOMPARE(b.items.size(), blank.items.size());
        for (int i = 0; i < b.items.size(); ++i) {
            const Item &was = blank.items.at(i);
            const Item &is = b.items.at(i);
            QCOMPARE(is.type, was.type);
            QCOMPARE(is.shape, was.shape);
            QVERIFY2(qAbs(is.x - was.x) < 0.01 && qAbs(is.y - was.y) < 0.01 && qAbs(is.w - was.w) < 0.01 && qAbs(is.h - was.h) < 0.01, qPrintable(was.shape));
            QVERIFY(qAbs(is.rotation - was.rotation) < 0.01);
            QCOMPARE(is.flipH, was.flipH);
            QCOMPARE(is.flipV, was.flipV);
            QCOMPARE(is.fill, was.fill);
            QCOMPARE(is.shadow, was.shadow);
            QCOMPARE(is.arrow, was.arrow);
            QCOMPARE(is.group.isEmpty(), was.group.isEmpty());
        }
        QCOMPARE(b.items.at(3).group, b.items.at(4).group);
        QCOMPARE(b.items.at(0).stroke, QStringLiteral("#000000"));
        QCOMPARE(b.items.at(0).strokeWidth, 3.0);
        QCOMPARE(b.items.at(0).opacity, 0.5);
        QCOMPARE(b.items.at(0).valign, 1);
        QCOMPARE(b.items.at(0).plainText(), QStringLiteral("Dentro"));
        QCOMPARE(b.items.at(7).stroke, QStringLiteral("#00aa00"));
        QCOMPARE(b.items.at(7).strokeWidth, 2.0);
        QCOMPARE(b.items.at(8).image, picture.image);
        QCOMPARE(b.items.at(9).cells, table.cells);
        QCOMPARE(b.items.at(9).rows, 2);
        QCOMPARE(b.items.at(9).cols, 3);

        // And once more: what was read is written the same.
        Presentation again;
        QVERIFY(Odp::read(Odp::write(back), again));
        QCOMPARE(again.slides.size(), back.slides.size());
        for (int i = 0; i < again.slides.size(); ++i) {
            QCOMPARE(again.slides.at(i).items.size(), back.slides.at(i).items.size());
            for (int j = 0; j < again.slides.at(i).items.size(); ++j) {
                Item one = again.slides.at(i).items.at(j);
                Item other = back.slides.at(i).items.at(j);
                one.id = other.id = QString();
                one.group = other.group = QString();
                QCOMPARE(one.text, other.text);
                QCOMPARE(one.rect(), other.rect());
                QCOMPARE(one.fill, other.fill);
            }
        }
    }

    void rejectsWhatIsNotADeck()
    {
        Presentation deck;
        QString error;
        QVERIFY(!Odp::read(QByteArrayLiteral("nada"), deck, &error));
        QVERIFY(!error.isEmpty());
        ZipWriter writer;
        writer.add(QStringLiteral("content.xml"), QByteArrayLiteral("<a/>"));
        QVERIFY(!Odp::read(writer.finish(), deck, &error));
    }
};

QTEST_MAIN(TestOdp)
#include "tst_odp.moc"
