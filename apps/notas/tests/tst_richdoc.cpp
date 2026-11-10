#include "richdoc.h"

#include <QTest>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextTable>

static Block block(Block::Type type, const Runs &runs, int indent = 0, bool done = false)
{
    Block b;
    b.type = type;
    b.runs = runs;
    b.indent = indent;
    b.done = done;
    return b;
}

static Run text(const QString &text)
{
    return Run{.text = text};
}

static Doc sample()
{
    Doc doc;
    doc.blocks << block(Block::Heading1, {text(QStringLiteral("Plan"))});
    doc.blocks << block(Block::Paragraph, {text(QStringLiteral("Texto ")), Run{.text = QStringLiteral("negrita"), .bold = true}, text(QStringLiteral(" y ")),
                                           Run{.text = QStringLiteral("color"), .italic = true, .underline = true, .strike = true, .color = QStringLiteral("#ff3b30"), .highlight = QStringLiteral("#ffe066")},
                                           Run{.text = QStringLiteral("mono"), .mono = true}});
    doc.blocks << block(Block::Heading2, {text(QStringLiteral("Dos"))});
    doc.blocks << block(Block::Heading3, {text(QStringLiteral("Tres")), Run{.text = QStringLiteral(" cursiva"), .italic = true}});
    doc.blocks << block(Block::Bullet, {text(QStringLiteral("uno"))});
    doc.blocks << block(Block::Bullet, {text(QStringLiteral("dentro"))}, 1);
    doc.blocks << block(Block::Bullet, {text(QStringLiteral("dos"))});
    doc.blocks << block(Block::Number, {text(QStringLiteral("primero"))});
    doc.blocks << block(Block::Number, {text(QStringLiteral("segundo"))});
    doc.blocks << block(Block::Task, {text(QStringLiteral("hecho"))}, 0, true);
    doc.blocks << block(Block::Task, {text(QStringLiteral("por hacer"))});
    doc.blocks << block(Block::Task, {text(QStringLiteral("subtarea"))}, 1, true);
    doc.blocks << block(Block::Quote, {text(QStringLiteral("una cita\ncon dos líneas"))});
    doc.blocks << block(Block::Code, {text(QStringLiteral("int x = 1;"))});
    doc.blocks << block(Block::Code, {});
    doc.blocks << block(Block::Code, {text(QStringLiteral("  return x;"))});
    Block table;
    table.type = Block::Table;
    table.rows << QList<Runs>{{Run{.text = QStringLiteral("A"), .bold = true}}, {text(QStringLiteral("B"))}, {}};
    table.rows << QList<Runs>{{text(QStringLiteral("1\n2"))}, {}, {Run{.text = QStringLiteral("enlace"), .href = QStringLiteral("https://example.org")}}};
    doc.blocks << table;
    doc.blocks << block(Block::Paragraph, {Run{.text = QStringLiteral("web"), .bold = true, .href = QStringLiteral("https://example.org/")}, text(QStringLiteral(" ")),
                                           Run{.text = QStringLiteral("Otra"), .href = Doc::wikiHref(QStringLiteral("Otra"))},
                                           Run{.image = QStringLiteral("files/foto.png"), .width = 320, .height = 200}});
    doc.blocks << block(Block::Paragraph, {});
    return doc;
}

class TestRichDoc : public QObject
{
    Q_OBJECT

    Style style = Style::of(false);

    static QString types(const QTextDocument &document)
    {
        QStringList names;
        for (const Block &block : RichDoc::read(&document).blocks) {
            names << Block::typeName(block.type) + (block.indent ? QString::number(block.indent) : QString()) + (block.done ? QStringLiteral("!") : QString());
        }
        return names.join(QLatin1Char(' '));
    }

private Q_SLOTS:
    void roundTrip()
    {
        const Doc doc = sample();
        for (const bool dark : {false, true}) {
            QTextDocument document;
            RichDoc::write(doc, &document, Style::of(dark));
            const Doc back = RichDoc::read(&document);
            QCOMPARE(back.blocks.size(), doc.blocks.size());
            for (int i = 0; i < doc.blocks.size(); ++i) {
                QVERIFY2(back.blocks.at(i) == doc.blocks.at(i), qPrintable(QStringLiteral("block %1: %2").arg(i).arg(Block::typeName(doc.blocks.at(i).type))));
            }
            QVERIFY(!document.isModified());
            QVERIFY(!document.isUndoAvailable());
        }
    }

    void tableFirstAndLast()
    {
        Doc doc;
        Block table;
        table.type = Block::Table;
        table.rows << QList<Runs>{{text(QStringLiteral("a"))}, {text(QStringLiteral("b"))}};
        doc.blocks << table << table;
        QTextDocument document;
        RichDoc::write(doc, &document, style);
        const Doc back = RichDoc::read(&document);
        // Qt keeps a paragraph before, between and after tables; the tables are whole.
        QList<Block> tables;
        for (const Block &block : back.blocks) {
            if (block.type == Block::Table) {
                tables << block;
            } else {
                QVERIFY(block.runs.isEmpty());
            }
        }
        QCOMPARE(tables.size(), 2);
        QVERIFY(tables.at(0) == table);
        QVERIFY(tables.at(1) == table);
    }

    void toggles()
    {
        QTextDocument document;
        Doc doc;
        doc.blocks << block(Block::Paragraph, {text(QStringLiteral("uno dos tres"))});
        RichDoc::write(doc, &document, style);
        RichText rich(&document, style);
        rich.toggle(4, 7, QStringLiteral("bold"));
        Runs runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 3);
        QVERIFY(runs.at(1).bold);
        QCOMPARE(runs.at(1).text, QStringLiteral("dos"));
        // Part bold, part not: all of it becomes bold; then none.
        rich.toggle(0, 7, QStringLiteral("bold"));
        runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 2);
        QVERIFY(runs.at(0).bold);
        rich.toggle(0, 7, QStringLiteral("bold"));
        QCOMPARE(RichDoc::read(&document).blocks.first().runs.size(), 1);
        // No selection: the word at the cursor.
        rich.toggle(9, 9, QStringLiteral("italic"));
        runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.last().text, QStringLiteral("tres"));
        QVERIFY(runs.last().italic);
        rich.toggle(8, 12, QStringLiteral("underline"));
        rich.toggle(8, 12, QStringLiteral("strike"));
        rich.toggle(8, 12, QStringLiteral("mono"));
        rich.setColor(8, 12, QStringLiteral("#0a84ff"));
        rich.setHighlight(0, 3, QStringLiteral("#ffe066"));
        runs = RichDoc::read(&document).blocks.first().runs;
        QVERIFY(runs.last().underline && runs.last().strike && runs.last().mono && runs.last().italic);
        QCOMPARE(runs.last().color, QStringLiteral("#0a84ff"));
        QCOMPARE(runs.first().highlight, QStringLiteral("#ffe066"));
        const QVariantMap look = rich.lookAt(10, 10);
        QCOMPARE(look.value(QStringLiteral("italic")).toBool(), true);
        QCOMPARE(look.value(QStringLiteral("bold")).toBool(), false);
        QCOMPARE(look.value(QStringLiteral("color")).toString(), QStringLiteral("#0a84ff"));
        QCOMPARE(look.value(QStringLiteral("type")).toString(), QStringLiteral("p"));
        rich.clearLook(0, 12);
        runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 1);
        QVERIFY(runs.first() == text(QStringLiteral("uno dos tres")));
        // All of it undoes.
        while (document.isUndoAvailable()) {
            document.undo();
        }
        QVERIFY(RichDoc::read(&document) == doc);
    }

    void typesOfBlocks()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("uno\ndos\ntres\ncuatro")), &document, style);
        RichText rich(&document, style);
        rich.setType(0, 0, Block::Heading1);
        QCOMPARE(types(document), QStringLiteral("h1 p p p"));
        rich.setType(4, 9, Block::Bullet);
        QCOMPARE(types(document), QStringLiteral("h1 bullet bullet p"));
        // A second list beside the first joins it: one list, not two.
        rich.setType(13, 13, Block::Bullet);
        QCOMPARE(document.findBlock(13).textList(), document.findBlock(5).textList());
        rich.setType(4, 4, Block::Task);
        QCOMPARE(types(document), QStringLiteral("h1 task bullet bullet"));
        QVERIFY(rich.toggleDone(5));
        QCOMPARE(types(document), QStringLiteral("h1 task! bullet bullet"));
        QVERIFY(rich.toggleDone(5));
        QVERIFY(!rich.toggleDone(0));
        // The same type again takes it away.
        rich.setType(4, 4, Block::Task);
        QCOMPARE(types(document), QStringLiteral("h1 p bullet bullet"));
        rich.setType(8, 14, Block::Number);
        QCOMPARE(types(document), QStringLiteral("h1 p number number"));
        rich.setType(0, 14, Block::Quote);
        QCOMPARE(types(document), QStringLiteral("quote quote quote quote"));
        rich.setType(0, 14, Block::Code);
        QCOMPARE(types(document), QStringLiteral("code code code code"));
        rich.setType(0, 14, Block::Paragraph, false);
        QCOMPARE(types(document), QStringLiteral("p p p p"));
        QVERIFY(RichDoc::read(&document) == Doc::fromText(QStringLiteral("uno\ndos\ntres\ncuatro")));
    }

    void headingKeepsItsLook()
    {
        QTextDocument document;
        Doc doc;
        doc.blocks << block(Block::Paragraph, {text(QStringLiteral("un ")), Run{.text = QStringLiteral("título"), .italic = true, .color = QStringLiteral("#ff3b30")}});
        RichDoc::write(doc, &document, style);
        RichText rich(&document, style);
        rich.setType(0, 0, Block::Heading2);
        Doc back = RichDoc::read(&document);
        QCOMPARE(back.blocks.first().type, Block::Heading2);
        // Headings are bold by nature: that is not «bold» in the page.
        QVERIFY(!back.blocks.first().runs.first().bold);
        QVERIFY(back.blocks.first().runs.last().italic);
        QCOMPARE(back.blocks.first().runs.last().color, QStringLiteral("#ff3b30"));
        rich.setType(0, 0, Block::Paragraph, false);
        QVERIFY(RichDoc::read(&document) == doc);
    }

    void indent()
    {
        QTextDocument document;
        Doc doc;
        doc.blocks << block(Block::Bullet, {text(QStringLiteral("a"))}) << block(Block::Bullet, {text(QStringLiteral("b"))}) << block(Block::Bullet, {text(QStringLiteral("c"))})
                   << block(Block::Paragraph, {text(QStringLiteral("d"))});
        RichDoc::write(doc, &document, style);
        RichText rich(&document, style);
        rich.indent(2, 4, 1);
        QCOMPARE(types(document), QStringLiteral("bullet bullet1 bullet1 p"));
        QCOMPARE(document.findBlock(2).textList(), document.findBlock(4).textList());
        rich.indent(4, 4, 1);
        QCOMPARE(types(document), QStringLiteral("bullet bullet1 bullet2 p"));
        rich.indent(0, 6, -1);
        QCOMPARE(types(document), QStringLiteral("bullet bullet bullet1 p"));
        // Nothing to take from a paragraph, nor below zero.
        rich.indent(0, 6, -1);
        rich.indent(0, 6, -1);
        QCOMPARE(types(document), QStringLiteral("bullet bullet bullet p"));
        QVERIFY(RichDoc::read(&document) == doc);
    }

    void tables()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("antes\ndespués")), &document, style);
        RichText rich(&document, style);
        int at = rich.insertTable(5, 2, 3);
        QVERIFY(at > 0);
        Doc doc = RichDoc::read(&document);
        QCOMPARE(types(document), QStringLiteral("p table p p"));
        QCOMPARE(doc.blocks.at(1).rows.size(), 2);
        QCOMPARE(doc.blocks.at(1).rows.at(0).size(), 3);
        rich.insertText(at, at, QStringLiteral("x"));
        // Tab walks the cells and adds a row after the last.
        int cell = at;
        for (int i = 0; i < 6; ++i) {
            cell = rich.nextCell(cell, false);
        }
        rich.insertText(cell, cell, QStringLiteral("z"));
        doc = RichDoc::read(&document);
        QCOMPARE(doc.blocks.at(1).rows.size(), 3);
        QCOMPARE(doc.blocks.at(1).rows.at(0).at(0), Runs{text(QStringLiteral("x"))});
        QCOMPARE(doc.blocks.at(1).rows.at(2).at(0), Runs{text(QStringLiteral("z"))});
        QCOMPARE(rich.nextCell(0, false), -1);
        QVERIFY(rich.lookAt(at, at).value(QStringLiteral("table")).toBool());
        QVERIFY(!rich.lookAt(0, 0).value(QStringLiteral("table")).toBool());

        QVERIFY(rich.editTable(at, QStringLiteral("column-right")));
        QVERIFY(rich.editTable(at, QStringLiteral("row-above")));
        doc = RichDoc::read(&document);
        QCOMPARE(doc.blocks.at(1).rows.size(), 4);
        QCOMPARE(doc.blocks.at(1).rows.at(0).size(), 4);
        QCOMPARE(doc.blocks.at(1).rows.at(1).at(0), Runs{text(QStringLiteral("x"))});
        QVERIFY(doc.blocks.at(1).rows.at(1).at(1).isEmpty());
        const int first = document.rootFrame()->childFrames().first()->firstPosition();
        QVERIFY(rich.editTable(first, QStringLiteral("delete-row")));
        QVERIFY(rich.editTable(first, QStringLiteral("delete-column")));
        doc = RichDoc::read(&document);
        QCOMPARE(doc.blocks.at(1).rows.size(), 3);
        QCOMPARE(doc.blocks.at(1).rows.at(0).size(), 3);
        QVERIFY(!rich.editTable(0, QStringLiteral("delete-row")));
        QVERIFY(rich.editTable(first, QStringLiteral("delete-table")));
        QCOMPARE(RichDoc::read(&document).toText().simplified(), QStringLiteral("antes después"));
        // No table inside a table.
        at = rich.insertTable(0, 1, 1);
        QCOMPARE(rich.insertTable(at, 2, 2), -1);
    }

    void links()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("ver la web ya")), &document, style);
        RichText rich(&document, style);
        QCOMPARE(rich.setLink(7, 10, QString(), QStringLiteral("example.org/x")), 10);
        Runs runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 3);
        QCOMPARE(runs.at(1).href, QStringLiteral("https://example.org/x"));
        QVERIFY(!runs.at(1).underline);
        // Bold over half of it: still one link.
        rich.toggle(7, 8, QStringLiteral("bold"));
        QVariantMap link = rich.linkAt(9);
        QCOMPARE(link.value(QStringLiteral("text")).toString(), QStringLiteral("web"));
        QCOMPARE(link.value(QStringLiteral("start")).toInt(), 7);
        QCOMPARE(link.value(QStringLiteral("end")).toInt(), 10);
        QVERIFY(rich.linkAt(2).isEmpty());
        QCOMPARE(rich.lookAt(8, 8).value(QStringLiteral("link")).toString(), QStringLiteral("https://example.org/x"));
        // New text for the link.
        rich.setLink(7, 10, QStringLiteral("página"), QStringLiteral("https://example.org/y"));
        QCOMPARE(RichDoc::read(&document).toText(), QStringLiteral("ver la página ya"));
        rich.removeLink(8);
        runs = RichDoc::read(&document).blocks.first().runs;
        for (const Run &run : runs) {
            QVERIFY(run.href.isEmpty());
        }
        // With nothing selected the link is written out; mail gets its scheme.
        const int after = rich.setLink(0, 0, QString(), QStringLiteral("ana@example.org"));
        QCOMPARE(after, 22);
        QCOMPARE(rich.linkAt(3).value(QStringLiteral("href")).toString(), QStringLiteral("mailto:ana@example.org"));
        // What is typed after a link is not link.
        const int typed = rich.insertText(after, after, QStringLiteral("!"));
        QVERIFY(rich.linkAt(typed).isEmpty() || rich.linkAt(typed).value(QStringLiteral("end")).toInt() < typed);
    }

    void images()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("ab")), &document, style);
        RichText rich(&document, style);
        QCOMPARE(rich.insertImage(1, QStringLiteral("files/a.png"), 100, 50), 2);
        Runs runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 3);
        QVERIFY(runs.at(1) == (Run{.image = QStringLiteral("files/a.png"), .width = 100, .height = 50}));
        QVariantMap image = rich.imageAt(1);
        QCOMPARE(image.value(QStringLiteral("source")).toString(), QStringLiteral("files/a.png"));
        QCOMPARE(image.value(QStringLiteral("position")).toInt(), 1);
        // Just after it, too.
        QCOMPARE(rich.imageAt(2).value(QStringLiteral("position")).toInt(), 1);
        QVERIFY(rich.imageAt(0).isEmpty());
        QVERIFY(rich.imageAt(3).isEmpty());
        rich.replaceImage(1, QStringLiteral("files/a.png"), 200, 100);
        runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 3);
        QCOMPARE(runs.at(1).width, 200);
        QCOMPARE(runs.at(2).text, QStringLiteral("b"));
    }

    void autoformat_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("type");
        QTest::newRow("h1") << QStringLiteral("# ") << QStringLiteral("h1");
        QTest::newRow("h2") << QStringLiteral("## ") << QStringLiteral("h2");
        QTest::newRow("h3") << QStringLiteral("### ") << QStringLiteral("h3");
        QTest::newRow("bullet") << QStringLiteral("- ") << QStringLiteral("bullet");
        QTest::newRow("star") << QStringLiteral("* ") << QStringLiteral("bullet");
        QTest::newRow("number") << QStringLiteral("1. ") << QStringLiteral("number");
        QTest::newRow("task") << QStringLiteral("[] ") << QStringLiteral("task");
        QTest::newRow("task spaced") << QStringLiteral("[ ] ") << QStringLiteral("task");
        QTest::newRow("task done") << QStringLiteral("[x] ") << QStringLiteral("task!");
        QTest::newRow("quote") << QStringLiteral("> ") << QStringLiteral("quote");
        QTest::newRow("code") << QStringLiteral("```") << QStringLiteral("code");
        QTest::newRow("nothing") << QStringLiteral("#x ") << QStringLiteral("p");
        QTest::newRow("four") << QStringLiteral("#### ") << QStringLiteral("p");
    }
    void autoformat()
    {
        QFETCH(QString, typed);
        QFETCH(QString, type);
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("antes\n")), &document, style);
        RichText rich(&document, style);
        // As the keyboard does it: a character at a time.
        int position = 6;
        int moved = -1;
        for (const QChar c : typed) {
            QTextCursor cursor(&document);
            cursor.setPosition(position);
            cursor.insertText(c);
            position = cursor.position();
            moved = rich.autoformat(position);
            if (moved >= 0) {
                position = moved;
            }
        }
        QCOMPARE(types(document), QStringLiteral("p ") + type);
        if (type != QLatin1String("p")) {
            QCOMPARE(moved, 6);
            QCOMPARE(document.findBlock(6).text(), QString());
        }
    }

    void autoformatInPlace()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("- texto ya escrito")), &document, style);
        RichText rich(&document, style);
        QCOMPARE(rich.autoformat(2), 0);
        QCOMPARE(types(document), QStringLiteral("bullet"));
        QCOMPARE(document.begin().text(), QStringLiteral("texto ya escrito"));
        // «- » in the middle of a line is a dash.
        QTextDocument other;
        RichDoc::write(Doc::fromText(QStringLiteral("a - b")), &other, style);
        QCOMPARE(RichText(&other, style).autoformat(4), -1);
        // A bullet becomes a task with «[] ».
        QTextCursor cursor(&document);
        cursor.setPosition(0);
        cursor.insertText(QStringLiteral("[] "));
        QCOMPARE(rich.autoformat(3), 0);
        QCOMPARE(types(document), QStringLiteral("task"));
        // Nothing of this inside code.
        rich.setType(0, 0, Block::Code);
        cursor.setPosition(0);
        cursor.insertText(QStringLiteral("# "));
        QCOMPARE(rich.autoformat(2), -1);
        QCOMPARE(types(document), QStringLiteral("code"));
    }

    void wikiLinkAsTyped()
    {
        QTextDocument document;
        RichDoc::write(Doc::fromText(QStringLiteral("ver [[Lista de la compra]")), &document, style);
        RichText rich(&document, style);
        QCOMPARE(rich.autoformat(25), -1);
        QTextCursor cursor(&document);
        cursor.setPosition(25);
        cursor.insertText(QStringLiteral("]"));
        const int at = rich.autoformat(26);
        QCOMPARE(at, 22);
        const Runs runs = RichDoc::read(&document).blocks.first().runs;
        QCOMPARE(runs.size(), 2);
        QCOMPARE(runs.at(1).text, QStringLiteral("Lista de la compra"));
        QCOMPARE(Doc::wikiTitle(runs.at(1).href), QStringLiteral("Lista de la compra"));
        // And what is typed next is plain again.
        cursor.setPosition(at);
        cursor.insertText(QStringLiteral(" "));
        rich.autoformat(at + 1);
        cursor.setPosition(at + 1);
        cursor.insertText(QStringLiteral("y"));
        rich.autoformat(at + 2);
        QCOMPARE(RichDoc::read(&document).blocks.first().runs.size(), 3);
        QCOMPARE(RichDoc::read(&document).blocks.first().runs.at(2).text, QStringLiteral(" y"));
    }

    void newlines()
    {
        QTextDocument document;
        Doc doc;
        doc.blocks << block(Block::Heading1, {text(QStringLiteral("Título"))}) << block(Block::Task, {text(QStringLiteral("hecha"))}, 0, true)
                   << block(Block::Bullet, {}, 1) << block(Block::Paragraph, {text(QStringLiteral("fin"))});
        RichDoc::write(doc, &document, style);
        RichText rich(&document, style);
        // After a heading comes a paragraph.
        int at = rich.newline(6);
        QCOMPARE(at, 7);
        QCOMPARE(types(document), QStringLiteral("h1 p task! bullet1 p"));
        // After a done task comes one to do.
        at = rich.newline(13);
        QCOMPARE(types(document), QStringLiteral("h1 p task! task bullet1 p"));
        QCOMPARE(at, 14);
        // Return on an empty nested item brings it up; again, ends the list.
        const int nested = document.findBlockByNumber(4).position();
        QCOMPARE(rich.newline(nested), nested);
        QCOMPARE(types(document), QStringLiteral("h1 p task! task bullet p"));
        QCOMPARE(rich.newline(nested), nested);
        QCOMPARE(types(document), QStringLiteral("h1 p task! task p p"));
        // An ordinary paragraph is the editor's business.
        QCOMPARE(rich.newline(document.lastBlock().position()), -1);
        // Backspace at the start of a list item makes it a paragraph; elsewhere it is a backspace.
        QCOMPARE(rich.backspace(8), 8);
        QCOMPARE(types(document), QStringLiteral("h1 p p task p p"));
        QCOMPARE(rich.backspace(9), -1);
        QCOMPARE(rich.backspace(0), 0);
        QCOMPARE(types(document), QStringLiteral("p p p task p p"));
        QCOMPARE(rich.backspace(0), -1);
    }

    void readsForeignHtml()
    {
        QTextDocument document;
        document.setHtml(QStringLiteral("<h1>Título</h1><p>Un <b>párrafo</b> con <a href=\"https://example.org\">enlace</a> y <code>código</code>.</p>"
                                        "<ul><li>uno</li><li>dos<ul><li>dentro</li></ul></li></ul><ol><li>a</li></ol>"
                                        "<pre>línea 1\nlínea 2</pre><blockquote>cita</blockquote>"
                                        "<table><tr><td>A</td><td>B</td></tr></table>"));
        const Doc doc = RichDoc::read(&document);
        QStringList names;
        for (const Block &block : doc.blocks) {
            names << Block::typeName(block.type) + (block.indent ? QString::number(block.indent) : QString());
        }
        const QString all = names.join(QLatin1Char(' '));
        QVERIFY2(all.startsWith(QStringLiteral("h1 p bullet bullet bullet1 number code code quote table")), qPrintable(all));
        QCOMPARE(doc.blocks.at(1).runs.at(1), (Run{.text = QStringLiteral("párrafo"), .bold = true}));
        QCOMPARE(doc.blocks.at(1).runs.at(3).href, QStringLiteral("https://example.org"));
        QVERIFY(doc.blocks.at(1).runs.at(5).mono);
        QCOMPARE(doc.blocks.at(6).runs.first().text, QStringLiteral("línea 1"));
        QCOMPARE(doc.blocks.at(9).rows.first().at(1), Runs{text(QStringLiteral("B"))});
        // With the title taken, the headings move up.
        QCOMPARE(RichDoc::read(&document, 1).blocks.first().type, Block::Heading1);
        document.setHtml(QStringLiteral("<h2>Dos</h2><h3>Tres</h3>"));
        QCOMPARE(RichDoc::read(&document, 1).blocks.at(0).type, Block::Heading1);
        QCOMPARE(RichDoc::read(&document, 1).blocks.at(1).type, Block::Heading2);
    }
};

QTEST_MAIN(TestRichDoc)
#include "tst_richdoc.moc"
