#include "pptx.h"
#include "xmlutil.h"
#include "zip.h"

#include <QBuffer>
#include <QImage>
#include <QtTest>

using namespace Model;

class TestPptx : public QObject
{
    Q_OBJECT

    static QByteArray png()
    {
        QImage image(64, 32, QImage::Format_RGB32);
        image.fill(Qt::darkGreen);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }

    /// A deck with one of everything.
    static Presentation sample()
    {
        Presentation deck;
        deck.theme = QStringLiteral("aurora");
        deck.master.footer = QStringLiteral("Pie de página");
        deck.master.numbers = true;

        Slide title = newSlide(QStringLiteral("title"), deck.size());
        title.items[0].setPlainText(QStringLiteral("Título <con> & «signos»"));
        title.items[1].setPlainText(QStringLiteral("Subtítulo"));
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
        Run themed;
        themed.text = QStringLiteral(" tema");
        themed.color = QStringLiteral("@accent2");
        Paragraph first;
        first.bullet = true;
        first.runs << plain << strong << themed;
        Paragraph second;
        second.bullet = true;
        second.level = 1;
        second.align = 2;
        Run more;
        more.text = QStringLiteral("Segundo nivel");
        second.runs << more;
        content.items[1].text = {first, second};
        content.items[1].anim = QStringLiteral("slide");
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
            item.fill = n % 2 ? QStringLiteral("#3366cc") : QStringLiteral("@accent1");
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
        styled.anim = QStringLiteral("appear");
        styled.setPlainText(QStringLiteral("Dentro"));
        blank.items[5].flipH = true;

        Item line;
        line.id = newId();
        line.type = QStringLiteral("shape");
        line.shape = QStringLiteral("line");
        line.setRect(QRectF(100, 200, 300, 100));
        line.flipV = true;
        line.arrow = true;
        line.stroke = QStringLiteral("@text");
        line.strokeWidth = 2;
        blank.items << line;

        Item box;
        box.id = newId();
        box.type = QStringLiteral("text");
        box.setRect(QRectF(500, 200, 300, 60));
        box.setPlainText(QStringLiteral("Un cuadro\nde dos líneas"));
        box.text[0].align = 1;
        box.valign = 2;
        box.group = QStringLiteral("g");
        blank.items << box;

        Item picture;
        picture.id = newId();
        picture.type = QStringLiteral("image");
        picture.setRect(QRectF(100, 350, 128, 64));
        picture.image = deck.addMedia(png(), QStringLiteral("png"));
        picture.group = QStringLiteral("g");
        picture.rotation = 10;
        blank.items << picture;

        Item table;
        table.id = newId();
        table.type = QStringLiteral("table");
        table.setRect(QRectF(400, 350, 400, 108));
        table.rows = 3;
        table.cols = 2;
        table.cells = {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("1"), QStringLiteral("2"), QString(), QStringLiteral("x & y")};
        blank.items << table;
        deck.slides << blank;

        Slide picturePage = newSlide(QStringLiteral("picture"), deck.size());
        deck.slides << picturePage;
        Slide columns = newSlide(QStringLiteral("columns"), deck.size());
        deck.slides << columns;
        Slide only = newSlide(QStringLiteral("titleOnly"), deck.size());
        deck.slides << only;
        return deck;
    }

    /// The same deck but for the ids, which a file does not keep.
    static void compare(const Presentation &read, const Presentation &wrote)
    {
        QCOMPARE(read.width, wrote.width);
        QCOMPARE(read.height, wrote.height);
        QCOMPARE(read.theme, wrote.theme);
        QCOMPARE(read.master, wrote.master);
        QCOMPARE(read.slides.size(), wrote.slides.size());
        for (int s = 0; s < wrote.slides.size(); ++s) {
            const Slide &a = read.slides.at(s);
            const Slide &b = wrote.slides.at(s);
            QCOMPARE(a.layout, b.layout);
            QCOMPARE(a.notes, b.notes);
            QCOMPARE(a.hidden, b.hidden);
            QCOMPARE(a.background, b.background);
            QCOMPARE(a.transition, b.transition);
            QCOMPARE(a.items.size(), b.items.size());
            QHash<QString, QString> groups;
            for (int i = 0; i < b.items.size(); ++i) {
                Item x = a.items.at(i);
                Item y = b.items.at(i);
                const QByteArray where = QByteArray::number(s) + '/' + QByteArray::number(i) + ' ' + y.type.toUtf8() + ' ' + y.shape.toUtf8() + y.role.toUtf8();
                QVERIFY2(qAbs(x.x - y.x) < 0.01 && qAbs(x.y - y.y) < 0.01 && qAbs(x.w - y.w) < 0.01 && qAbs(x.h - y.h) < 0.01, where.constData());
                QVERIFY2(qAbs(x.rotation - y.rotation) < 0.01, where.constData());
                QVERIFY2(qAbs(x.opacity - y.opacity) < 0.001, where.constData());
                QVERIFY2(qAbs(x.strokeWidth - y.strokeWidth) < 0.01, where.constData());
                // Groups keep who is with whom, not their names.
                QVERIFY2(x.group.isEmpty() == y.group.isEmpty(), where.constData());
                if (!y.group.isEmpty()) {
                    if (!groups.contains(y.group)) groups.insert(y.group, x.group);
                    QVERIFY2(groups.value(y.group) == x.group, where.constData());
                }
                QVERIFY2(read.media.value(x.image) == wrote.media.value(y.image), where.constData());
                x.id = y.id;
                x.setRect(y.rect());
                x.rotation = y.rotation;
                x.opacity = y.opacity;
                x.strokeWidth = y.strokeWidth;
                x.group = y.group;
                x.image = y.image;
                if (x != y) {
                    qDebug().noquote() << QJsonDocument(x.toJson()).toJson(QJsonDocument::Compact) << "\n" << QJsonDocument(y.toJson()).toJson(QJsonDocument::Compact);
                }
                QVERIFY2(x == y, where.constData());
            }
        }
    }

private Q_SLOTS:
    void roundTrip()
    {
        const Presentation deck = sample();
        const QByteArray bytes = Pptx::write(deck);
        QVERIFY(!bytes.isEmpty());
        Presentation back;
        QString error;
        QVERIFY2(Pptx::read(bytes, back, &error), qPrintable(error));
        compare(back, deck);
        // And again: what was read writes the same file but for the ids.
        Presentation again;
        QVERIFY(Pptx::read(Pptx::write(back), again, &error));
        compare(again, deck);
    }

    void roundTripFourByThreeAndMaster()
    {
        Presentation deck;
        deck.width = 720;
        deck.theme = QStringLiteral("papel");
        deck.master.background = QStringLiteral("#ffeedd");
        deck.master.titleColor = QStringLiteral("#aa0000");
        deck.master.textColor = QStringLiteral("#001122");
        deck.master.titleFont = QStringLiteral("DejaVu Serif");
        deck.master.bodyFont = QStringLiteral("DejaVu Sans");
        deck.slides << newSlide(QStringLiteral("title"), deck.size()) << newSlide(QStringLiteral("content"), deck.size());
        Presentation back;
        QVERIFY(Pptx::read(Pptx::write(deck), back));
        compare(back, deck);
    }

    void everyThemeRoundTrips()
    {
        for (const Theme &theme : themes()) {
            Presentation deck;
            deck.theme = theme.id;
            deck.slides << newSlide(QStringLiteral("content"), deck.size());
            deck.slides[0].items[0].setPlainText(theme.name);
            Presentation back;
            QVERIFY2(Pptx::read(Pptx::write(deck), back), qPrintable(theme.id));
            QCOMPARE(back.theme, theme.id);
            QCOMPARE(back.master, deck.master);
        }
    }

    void packageIsWellFormed()
    {
        const Presentation deck = sample();
        const ZipReader zip(Pptx::write(deck));
        QVERIFY(zip.isValid());
        // The content types come first, as the packaging convention has it.
        QCOMPARE(zip.names().first(), QStringLiteral("[Content_Types].xml"));
        for (const char *part : {"_rels/.rels", "ppt/presentation.xml", "ppt/_rels/presentation.xml.rels", "ppt/theme/theme1.xml",
                                 "ppt/slideMasters/slideMaster1.xml", "ppt/slideLayouts/slideLayout1.xml", "ppt/slides/slide1.xml",
                                 "ppt/slides/_rels/slide1.xml.rels", "ppt/notesSlides/notesSlide2.xml", "docProps/app.xml", "docProps/core.xml"}) {
            QVERIFY2(zip.contains(QLatin1String(part)), part);
        }
        QCOMPARE(zip.names().filter(QRegularExpression(QStringLiteral("^ppt/slides/slide\\d+\\.xml$"))).size(), deck.slides.size());
        QCOMPARE(zip.names().filter(QStringLiteral("ppt/media/")).size(), 1);
        // Every XML part parses, and every relationship points at a part that is there.
        for (const QString &name : zip.names()) {
            if (!name.endsWith(QLatin1String(".xml")) && !name.endsWith(QLatin1String(".rels"))) {
                continue;
            }
            bool ok = false;
            const QDomDocument document = Xml::parse(zip.file(name), &ok);
            QVERIFY2(ok, qPrintable(name));
            if (!name.endsWith(QLatin1String(".rels"))) {
                continue;
            }
            const QString owner = QString(name).replace(QStringLiteral("_rels/"), QString()).chopped(5);
            for (const QDomElement &rel : Xml::kids(document.documentElement(), "Relationship")) {
                QString base = owner.left(owner.lastIndexOf(QLatin1Char('/')) + 1);
                QString target = rel.attribute(QStringLiteral("Target"));
                while (target.startsWith(QLatin1String("../"))) {
                    target = target.mid(3);
                    base.chop(1);
                    base = base.left(base.lastIndexOf(QLatin1Char('/')) + 1);
                }
                QVERIFY2(zip.contains(base + target), qPrintable(name + QStringLiteral(" → ") + base + target));
            }
        }
        // Every part has a content type, by name or by extension.
        const QDomElement types = Xml::parse(zip.file(QStringLiteral("[Content_Types].xml"))).documentElement();
        QSet<QString> overrides;
        QSet<QString> extensions;
        for (const QDomElement &e : Xml::kids(types)) {
            if (e.localName() == QLatin1String("Override")) overrides << e.attribute(QStringLiteral("PartName"));
            else extensions << e.attribute(QStringLiteral("Extension"));
        }
        for (const QString &name : zip.names()) {
            if (name == QLatin1String("[Content_Types].xml")) {
                continue;
            }
            QVERIFY2(overrides.contains(QLatin1Char('/') + name) || extensions.contains(name.section(QLatin1Char('.'), -1)), qPrintable(name));
        }
    }

    void readsPythonPptx()
    {
        QFile file(QStringLiteral(TEST_DATA "/python-pptx.pptx"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Presentation deck;
        QString error;
        QVERIFY2(Pptx::read(file.readAll(), deck, &error), qPrintable(error));
        // python-pptx was told 13.333 inches: a hair under 960 points.
        QVERIFY(qAbs(deck.width - 960) < 0.05);
        QCOMPARE(deck.height, 540.0);
        QCOMPARE(deck.slides.size(), 3);
        // A file of somebody else's becomes the plain theme nearest its background.
        QCOMPARE(deck.theme, QStringLiteral("blanco"));

        const Slide &title = deck.slides.at(0);
        QCOMPARE(title.layout, QStringLiteral("title"));
        QCOMPARE(title.items.size(), 2);
        QCOMPARE(title.items.at(0).role, QStringLiteral("ctitle"));
        QCOMPARE(title.items.at(0).plainText(), QStringLiteral("Informe anual"));
        QCOMPARE(title.items.at(1).role, QStringLiteral("subtitle"));
        QCOMPARE(title.items.at(1).plainText(), QStringLiteral("Hecho con python-pptx"));
        // The placeholders say nothing of where they are: the layout does.
        for (const Item &item : title.items) {
            QVERIFY(item.w > 300 && item.h > 30);
            QVERIFY(QRectF(0, 0, 960, 540).contains(item.rect().adjusted(1, 1, -1, -1)));
        }

        const Slide &points = deck.slides.at(1);
        QCOMPARE(points.layout, QStringLiteral("content"));
        QCOMPARE(points.notes, QStringLiteral("Notas del orador"));
        QCOMPARE(points.items.at(1).role, QStringLiteral("body"));
        QCOMPARE(points.items.at(1).text.size(), 2);
        QVERIFY(points.items.at(1).text.at(0).bullet);
        QCOMPARE(points.items.at(1).text.at(1).level, 1);
        QCOMPARE(points.items.at(1).plainText(), QStringLiteral("Primero\nSegundo"));

        const Slide &free = deck.slides.at(2);
        QCOMPARE(free.background, QStringLiteral("#223344"));
        QCOMPARE(free.items.size(), 9);
        const Item &box = free.items.at(0);
        QCOMPARE(box.type, QStringLiteral("text"));
        QCOMPARE(box.rect(), QRectF(72, 72, 288, 72));
        QCOMPARE(box.text.at(0).align, 1);
        QCOMPARE(box.text.at(0).runs.size(), 2);
        QCOMPARE(box.text.at(0).runs.at(0).text, QStringLiteral("Negrita roja"));
        QCOMPARE(box.text.at(0).runs.at(0).bold, 1);
        QCOMPARE(box.text.at(0).runs.at(0).size, 28.0);
        QCOMPARE(box.text.at(0).runs.at(0).color, QStringLiteral("#ff0000"));
        QVERIFY(box.text.at(0).runs.at(1).italic);
        QVERIFY(box.text.at(0).runs.at(1).underline);
        QCOMPARE(box.text.at(0).runs.at(1).font, QStringLiteral("Georgia"));
        const Item &oval = free.items.at(1);
        QCOMPARE(oval.shape, QStringLiteral("ellipse"));
        QCOMPARE(oval.fill, QStringLiteral("#0080ff"));
        QCOMPARE(oval.stroke, QStringLiteral("#000000"));
        QCOMPARE(oval.strokeWidth, 3.0);
        QCOMPARE(oval.rotation, 30.0);
        QCOMPARE(oval.plainText(), QStringLiteral("Elipse"));
        QCOMPARE(free.items.at(2).shape, QStringLiteral("rounded"));
        QVERIFY(free.items.at(2).fill.isEmpty());
        QCOMPARE(free.items.at(3).shape, QStringLiteral("arrow"));
        // A shape that follows its style takes the accent of the theme.
        QVERIFY(QColor::fromString(free.items.at(3).fill).isValid());
        QCOMPARE(free.items.at(4).shape, QStringLiteral("star"));
        QCOMPARE(free.items.at(5).shape, QStringLiteral("triangle"));
        QCOMPARE(free.items.at(6).shape, QStringLiteral("line"));
        QCOMPARE(free.items.at(6).line(), QLineF(72, 360, 288, 432));
        QVERIFY(!free.items.at(6).stroke.isEmpty());
        const Item &picture = free.items.at(7);
        QCOMPARE(picture.type, QStringLiteral("image"));
        QCOMPARE(QImage::fromData(deck.media.value(picture.image)).size(), QSize(120, 60));
        const Item &table = free.items.at(8);
        QCOMPARE(table.type, QStringLiteral("table"));
        QCOMPARE(table.rows, 2);
        QCOMPARE(table.cols, 3);
        QCOMPARE(table.cell(1, 2), QStringLiteral("f1c2"));
    }

    void rejectsWhatIsNotADeck()
    {
        Presentation deck;
        deck.theme = QStringLiteral("negro");
        QString error;
        QVERIFY(!Pptx::read(QByteArrayLiteral("nada"), deck, &error));
        QVERIFY(!error.isEmpty());
        ZipWriter writer;
        writer.add(QStringLiteral("hola.txt"), QByteArrayLiteral("un zip cualquiera"));
        QVERIFY(!Pptx::read(writer.finish(), deck, &error));
        // What was there is left alone.
        QCOMPARE(deck.theme, QStringLiteral("negro"));
        // A file cut short.
        const QByteArray bytes = Pptx::write(sample());
        QVERIFY(!Pptx::read(bytes.left(bytes.size() / 3), deck, &error));
    }
};

QTEST_MAIN(TestPptx)
#include "tst_pptx.moc"
