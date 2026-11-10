#include "doc.h"

#include <QJsonDocument>
#include <QTest>

static Block block(Block::Type type, const Runs &runs, int indent = 0, bool done = false)
{
    Block b;
    b.type = type;
    b.runs = runs;
    b.indent = indent;
    b.done = done;
    return b;
}

static Doc sample()
{
    Doc doc;
    doc.blocks << block(Block::Heading1, {Run{.text = QStringLiteral("Plan")}});
    doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("Texto ")}, Run{.text = QStringLiteral("negrita"), .bold = true}, Run{.text = QStringLiteral(" y ")},
                                           Run{.text = QStringLiteral("color"), .italic = true, .color = QStringLiteral("#ff3b30"), .highlight = QStringLiteral("#ffe066")}});
    doc.blocks << block(Block::Bullet, {Run{.text = QStringLiteral("uno")}});
    doc.blocks << block(Block::Bullet, {Run{.text = QStringLiteral("dentro")}}, 1);
    doc.blocks << block(Block::Number, {Run{.text = QStringLiteral("primero")}});
    doc.blocks << block(Block::Number, {Run{.text = QStringLiteral("segundo")}});
    doc.blocks << block(Block::Task, {Run{.text = QStringLiteral("hecho")}}, 0, true);
    doc.blocks << block(Block::Task, {Run{.text = QStringLiteral("por hacer")}});
    doc.blocks << block(Block::Quote, {Run{.text = QStringLiteral("una cita")}});
    doc.blocks << block(Block::Code, {Run{.text = QStringLiteral("int x = 1 < 2;")}});
    doc.blocks << block(Block::Code, {Run{.text = QStringLiteral("return *x;")}});
    Block table;
    table.type = Block::Table;
    table.rows << QList<Runs>{{Run{.text = QStringLiteral("A")}}, {Run{.text = QStringLiteral("B|C")}}};
    table.rows << QList<Runs>{{Run{.text = QStringLiteral("1")}}, {}};
    doc.blocks << table;
    doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("web"), .href = QStringLiteral("https://example.org/a b")}, Run{.text = QStringLiteral(" ")},
                                           Run{.text = QStringLiteral("Otra página"), .href = Doc::wikiHref(QStringLiteral("Otra página"))},
                                           Run{.image = QStringLiteral("files/foto 1.png"), .width = 320, .height = 200},
                                           Run{.text = QStringLiteral("informe.pdf"), .href = QStringLiteral("files/informe.pdf")}});
    return doc;
}

class TestDoc : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void json()
    {
        const Doc doc = sample();
        const QByteArray bytes = QJsonDocument(doc.toJson()).toJson();
        const Doc back = Doc::fromJson(QJsonDocument::fromJson(bytes).object());
        QCOMPARE(back.blocks.size(), doc.blocks.size());
        for (int i = 0; i < doc.blocks.size(); ++i) {
            QVERIFY2(back.blocks.at(i) == doc.blocks.at(i), qPrintable(QString::number(i)));
        }
        QVERIFY(back == doc);
    }

    void jsonIsForgiving()
    {
        QVERIFY(Doc::fromJson({}).blocks.isEmpty());
        const QJsonObject odd = QJsonDocument::fromJson(R"({"blocks":[{"t":"what","indent":99,"runs":[{"s":"x"},{"img":""}]},{"t":"table","rows":[]},{"t":"table","rows":[[[{"s":"a"}]],[[],[{"s":"b"}]]]}]})").object();
        const Doc doc = Doc::fromJson(odd);
        QCOMPARE(doc.blocks.size(), 2);
        QCOMPARE(doc.blocks.at(0).type, Block::Paragraph);
        QCOMPARE(doc.blocks.at(0).indent, 8);
        QCOMPARE(doc.blocks.at(0).runs.size(), 1);
        // Every row as wide as the widest.
        QCOMPARE(doc.blocks.at(1).rows.at(0).size(), 2);
        QCOMPARE(doc.blocks.at(1).rows.at(1).size(), 2);
    }

    void text()
    {
        const QString text = sample().toText();
        const QStringList lines = text.split(QLatin1Char('\n'));
        QCOMPARE(lines.at(0), QStringLiteral("Plan"));
        QCOMPARE(lines.at(1), QStringLiteral("Texto negrita y color"));
        QCOMPARE(lines.at(2), QStringLiteral("• uno"));
        QCOMPARE(lines.at(3), QStringLiteral("  • dentro"));
        QCOMPARE(lines.at(4), QStringLiteral("1. primero"));
        QCOMPARE(lines.at(5), QStringLiteral("2. segundo"));
        QCOMPARE(lines.at(6), QStringLiteral("[x] hecho"));
        QCOMPARE(lines.at(7), QStringLiteral("[ ] por hacer"));
        QCOMPARE(lines.at(11), QStringLiteral("A\tB|C"));
        const Doc plain = Doc::fromText(QStringLiteral("uno\r\n\r\ndos"));
        QCOMPARE(plain.blocks.size(), 3);
        QCOMPARE(plain.toText(), QStringLiteral("uno\n\ndos"));
    }

    void markdown()
    {
        const QString md = sample().toMarkdown();
        const QString expected = QStringLiteral(
            "# Plan\n\n"
            "Texto **negrita** y *color*\n\n"
            "- uno\n"
            "    - dentro\n"
            "1. primero\n"
            "2. segundo\n"
            "- [x] hecho\n"
            "- [ ] por hacer\n\n"
            "> una cita\n\n"
            "```\nint x = 1 < 2;\nreturn *x;\n```\n\n"
            "| A | B\\|C |\n| --- | --- |\n| 1 |   |\n\n"
            "[web](https://example.org/a%20b) [[Otra página]]![imagen](files/foto%201.png)[informe.pdf](files/informe.pdf)\n");
        QCOMPARE(md, expected);
        QVERIFY(sample().toMarkdown(1).startsWith(QStringLiteral("## Plan\n")));
    }

    void markdownEdges()
    {
        Doc doc;
        doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("# no es un título")}});
        doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("a*b_c "), .bold = true}, Run{.text = QStringLiteral("x")}});
        doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("ls -la"), .mono = true}, Run{.text = QStringLiteral(" tachado"), .strike = true},
                                               Run{.text = QStringLiteral("sub"), .underline = true}});
        doc.blocks << block(Block::Paragraph, {});
        const QStringList lines = doc.toMarkdown().split(QStringLiteral("\n\n"));
        QCOMPARE(lines.at(0), QStringLiteral("\\# no es un título"));
        // The marks hug the words; the space stays outside.
        QCOMPARE(lines.at(1), QStringLiteral("**a\\*b\\_c** x"));
        QCOMPARE(lines.at(2), QStringLiteral("`ls -la` ~~tachado~~<u>sub</u>"));
    }

    void html()
    {
        const QString body = sample().toHtmlBody();
        QVERIFY(body.contains(QStringLiteral("<h2>Plan</h2>")));
        QVERIFY(body.contains(QStringLiteral("<strong>negrita</strong>")));
        QVERIFY(body.contains(QStringLiteral("<span style=\"color:#ff3b30;background-color:#ffe066\"><em>color</em></span>")));
        QVERIFY(body.contains(QStringLiteral("<ul>\n<li>uno<ul>\n<li>dentro</li>\n</ul>\n</li>\n</ul>\n<ol>\n<li>primero</li>\n<li>segundo</li>\n</ol>")));
        QVERIFY(body.contains(QStringLiteral("<li class=\"checked\">hecho</li>\n<li class=\"unchecked\">por hacer</li>")));
        QVERIFY(body.contains(QStringLiteral("<blockquote>\n<p>una cita</p>\n</blockquote>")));
        QVERIFY(body.contains(QStringLiteral("<pre><code>int x = 1 &lt; 2;\nreturn *x;</code></pre>")));
        QVERIFY(body.contains(QStringLiteral("<tr><td>A</td><td>B|C</td></tr>")));
        QVERIFY(body.contains(QStringLiteral("<img src=\"files/foto 1.png\" width=\"320\" height=\"200\" alt=\"\">")));
        const QString page = sample().toHtml(QStringLiteral("A & B"));
        QVERIFY(page.startsWith(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(page.contains(QStringLiteral("<title>A &amp; B</title>")));
        QVERIFY(page.contains(QStringLiteral("<h1>A &amp; B</h1>")));
    }

    void wikiLinks()
    {
        Doc doc;
        doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("ver [[Lista de la compra]] y [[ Otra ]]."), .bold = true}});
        doc.blocks << block(Block::Code, {Run{.text = QStringLiteral("a[[b]]")}});
        doc.linkWikiText();
        const Runs runs = doc.blocks.at(0).runs;
        QCOMPARE(runs.size(), 5);
        QCOMPARE(runs.at(1).text, QStringLiteral("Lista de la compra"));
        QCOMPARE(Doc::wikiTitle(runs.at(1).href), QStringLiteral("Lista de la compra"));
        QVERIFY(runs.at(1).bold);
        QCOMPARE(runs.at(3).text, QStringLiteral("Otra"));
        QCOMPARE(runs.at(4).text, QStringLiteral("."));
        // Code is code.
        QCOMPARE(doc.blocks.at(1).runs.size(), 1);
        QVERIFY(doc.blocks.at(1).runs.at(0).href.isEmpty());
        QCOMPARE(Doc::wikiTitle(QStringLiteral("https://example.org")), QString());
        QCOMPARE(Doc::wikiTitle(Doc::wikiHref(QStringLiteral("¿Qué? 100%/año"))), QStringLiteral("¿Qué? 100%/año"));
    }

    void files()
    {
        Doc doc = sample();
        QCOMPARE(doc.files(), QStringList({QStringLiteral("files/foto 1.png"), QStringLiteral("files/informe.pdf")}));
        doc.mapPaths([](const QString &path, bool picture) {
            return picture ? QString() : path.startsWith(QLatin1String("files/")) ? QStringLiteral("x/") + path.mid(6) : path;
        });
        // The picture that could not be kept is gone; the links stay.
        QCOMPARE(doc.files(), QStringList());
        QCOMPARE(doc.blocks.last().runs.size(), 4);
        QCOMPARE(doc.blocks.last().runs.last().href, QStringLiteral("x/informe.pdf"));
        QVERIFY(!doc.isEmpty());
        QVERIFY(Doc().isEmpty());
        Doc blank;
        blank.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("  ")}});
        QVERIFY(blank.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDoc)
#include "tst_doc.moc"
