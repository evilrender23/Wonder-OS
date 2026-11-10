#include "describe.h"
#include "editor.h"
#include "formats.h"

#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestEditor : public QObject
{
    Q_OBJECT

    static void type(Editor &editor, const QString &text)
    {
        for (const QChar c : text) {
            if (c == QLatin1Char('\n')) {
                editor.key(Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
            } else {
                editor.key(0, Qt::NoModifier, QString(c));
            }
        }
    }

private Q_SLOTS:
    void startsBlank()
    {
        Editor editor;
        QCOMPARE(editor.name(), QStringLiteral("Sin título"));
        QVERIFY(!editor.modified());
        QCOMPARE(editor.pageCount(), 1);
        QCOMPARE(editor.plainText(), QString());
        QCOMPARE(editor.format().value(QStringLiteral("style")).toString(), QStringLiteral("body"));
        QCOMPARE(editor.format().value(QStringLiteral("size")).toDouble(), 11.0);
        QCOMPARE(editor.styles().size(), 7);
    }

    void typingAndCounts()
    {
        Editor editor;
        type(editor, QStringLiteral("Hola mundo cruel\nSegunda línea"));
        QVERIFY(editor.modified());
        editor.refreshNow();
        QCOMPARE(editor.counts().value(QStringLiteral("words")).toInt(), 5);
        QCOMPARE(editor.counts().value(QStringLiteral("characters")).toInt(), 29);
        QCOMPARE(editor.counts().value(QStringLiteral("charactersNoSpaces")).toInt(), 26);
        QCOMPARE(editor.counts().value(QStringLiteral("paragraphs")).toInt(), 2);
        editor.select(0, 4);
        editor.refreshNow();
        QCOMPARE(editor.counts().value(QStringLiteral("selectedWords")).toInt(), 1);
        QCOMPARE(editor.counts().value(QStringLiteral("selectedCharacters")).toInt(), 4);
        QCOMPARE(editor.selectedText(), QStringLiteral("Hola"));
    }

    void characterFormat()
    {
        Editor editor;
        type(editor, QStringLiteral("uno dos tres"));
        editor.select(4, 7);
        editor.toggleBold();
        editor.toggleItalic();
        editor.toggleUnderline();
        editor.toggleStrike();
        editor.setVertical(1);
        editor.setFamily(Doc::monoFamily());
        editor.setPointSize(18);
        editor.setColor(QColor(0xd7, 0x00, 0x15));
        editor.setHighlight(QColor(0xff, 0xe0, 0x66));
        const QVariantMap format = editor.format();
        QVERIFY(format.value(QStringLiteral("bold")).toBool());
        QVERIFY(format.value(QStringLiteral("italic")).toBool());
        QVERIFY(format.value(QStringLiteral("underline")).toBool());
        QVERIFY(format.value(QStringLiteral("strike")).toBool());
        QCOMPARE(format.value(QStringLiteral("vertical")).toInt(), 1);
        QCOMPARE(format.value(QStringLiteral("family")).toString(), Doc::monoFamily());
        QCOMPARE(format.value(QStringLiteral("size")).toDouble(), 18.0);
        QCOMPARE(format.value(QStringLiteral("color")).toString(), QStringLiteral("#d70015"));
        QCOMPARE(format.value(QStringLiteral("highlight")).toString(), QStringLiteral("#ffe066"));
        // Only the word in the middle.
        editor.goTo(2);
        QVERIFY(!editor.format().value(QStringLiteral("bold")).toBool());
        editor.goTo(10);
        QVERIFY(!editor.format().value(QStringLiteral("bold")).toBool());

        editor.select(4, 7);
        editor.toggleBold();
        editor.clearHighlight();
        editor.setVertical(0);
        QVERIFY(!editor.format().value(QStringLiteral("bold")).toBool());
        QCOMPARE(editor.format().value(QStringLiteral("highlight")).toString(), QString());
        QCOMPARE(editor.format().value(QStringLiteral("vertical")).toInt(), 0);
    }

    void boldAppliesToWhatIsTypedNext()
    {
        Editor editor;
        type(editor, QStringLiteral("a"));
        editor.key(Qt::Key_B, Qt::ControlModifier, QString());
        type(editor, QStringLiteral("b"));
        editor.key(Qt::Key_B, Qt::ControlModifier, QString());
        type(editor, QStringLiteral("c"));
        const QStringList lines = describe(editor.document(), false);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first(), QStringLiteral("body left | [a ----0 #1d1d1f] [b b---0 #1d1d1f] [c ----0 #1d1d1f]"));
    }

    void paragraphFormat()
    {
        Editor editor;
        type(editor, QStringLiteral("uno\ndos\ntres"));
        editor.select(0, 5);
        editor.setAlignment(QStringLiteral("center"));
        editor.setLineHeight(150);
        editor.setSpacing(6, 12);
        editor.setIndents(1, 0.5, 0.75);
        const QVariantMap format = editor.format();
        QCOMPARE(format.value(QStringLiteral("alignment")).toString(), QStringLiteral("center"));
        QCOMPARE(format.value(QStringLiteral("lineHeight")).toInt(), 150);
        QCOMPARE(format.value(QStringLiteral("before")).toDouble(), 6.0);
        QCOMPARE(format.value(QStringLiteral("after")).toDouble(), 12.0);
        QCOMPARE(format.value(QStringLiteral("left")).toDouble(), 1.0);
        QCOMPARE(format.value(QStringLiteral("right")).toDouble(), 0.5);
        QCOMPARE(format.value(QStringLiteral("first")).toDouble(), 0.75);
        editor.goTo(9);
        QCOMPARE(editor.format().value(QStringLiteral("alignment")).toString(), QStringLiteral("left"));
        editor.goTo(5);
        QCOMPARE(editor.format().value(QStringLiteral("alignment")).toString(), QStringLiteral("center"));
    }

    void stylesAndOutline()
    {
        Editor editor;
        editor.applyStyle(QStringLiteral("title"));
        type(editor, QStringLiteral("Mi título\n"));
        // After a heading comes body text.
        QCOMPARE(editor.format().value(QStringLiteral("style")).toString(), QStringLiteral("body"));
        type(editor, QStringLiteral("Texto\n"));
        editor.applyStyle(QStringLiteral("h1"));
        type(editor, QStringLiteral("Capítulo\n"));
        editor.applyStyle(QStringLiteral("h2"));
        type(editor, QStringLiteral("Sección"));
        editor.refreshNow();
        const QVariantList outline = editor.outline();
        QCOMPARE(outline.size(), 3);
        QCOMPARE(outline.at(0).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("Mi título"));
        QCOMPARE(outline.at(0).toMap().value(QStringLiteral("level")).toInt(), 0);
        QCOMPARE(outline.at(1).toMap().value(QStringLiteral("level")).toInt(), 1);
        QCOMPARE(outline.at(2).toMap().value(QStringLiteral("level")).toInt(), 2);
        QCOMPARE(outline.at(2).toMap().value(QStringLiteral("page")).toInt(), 1);
        editor.goTo(outline.at(1).toMap().value(QStringLiteral("position")).toInt());
        QCOMPARE(editor.format().value(QStringLiteral("style")).toString(), QStringLiteral("h1"));
        QCOMPARE(editor.format().value(QStringLiteral("size")).toDouble(), 20.0);
    }

    void lists()
    {
        Editor editor;
        type(editor, QStringLiteral("uno\ndos\ntres"));
        editor.selectAll();
        editor.setList(1);
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 1);
        QCOMPARE(editor.format().value(QStringLiteral("listLevel")).toInt(), 1);
        editor.goTo(5);
        editor.indentMore();
        QCOMPARE(editor.format().value(QStringLiteral("listLevel")).toInt(), 2);
        QStringList lines = describe(editor.document(), false);
        QVERIFY2(lines.at(0).startsWith(QStringLiteral("body left bullet1#1")), qPrintable(lines.at(0)));
        QVERIFY2(lines.at(1).startsWith(QStringLiteral("body left bullet2#1")), qPrintable(lines.at(1)));
        QVERIFY2(lines.at(2).startsWith(QStringLiteral("body left bullet1#2")), qPrintable(lines.at(2)));
        editor.key(Qt::Key_Backtab, Qt::ShiftModifier, QString());
        QCOMPARE(editor.format().value(QStringLiteral("listLevel")).toInt(), 1);
        lines = describe(editor.document(), false);
        QVERIFY2(lines.at(1).startsWith(QStringLiteral("body left bullet1#2")), qPrintable(lines.at(1)));
        QVERIFY2(lines.at(2).startsWith(QStringLiteral("body left bullet1#3")), qPrintable(lines.at(2)));

        editor.selectAll();
        editor.setList(2);
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 2);
        lines = describe(editor.document(), false);
        QVERIFY2(lines.at(2).startsWith(QStringLiteral("body left number1#3")), qPrintable(lines.at(2)));
        // The same button again takes the list away.
        editor.setList(2);
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 0);

        // Return on an empty item ends the list.
        editor.key(Qt::Key_End, Qt::ControlModifier, QString());
        editor.setList(1);
        type(editor, QStringLiteral("\n"));
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 1);
        type(editor, QStringLiteral("\n"));
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 0);
    }

    void tables()
    {
        Editor editor;
        type(editor, QStringLiteral("antes"));
        editor.insertTable(2, 3);
        QVERIFY(editor.format().value(QStringLiteral("table")).toBool());
        QCOMPARE(editor.format().value(QStringLiteral("rows")).toInt(), 2);
        QCOMPARE(editor.format().value(QStringLiteral("columns")).toInt(), 3);
        type(editor, QStringLiteral("a"));
        editor.key(Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
        type(editor, QStringLiteral("b"));
        editor.addRow(true);
        editor.addColumn(true);
        QCOMPARE(editor.format().value(QStringLiteral("rows")).toInt(), 3);
        QCOMPARE(editor.format().value(QStringLiteral("columns")).toInt(), 4);
        QStringList lines = describe(editor.document(), false);
        QCOMPARE(lines.first(), QStringLiteral("body left | [antes ----0 #1d1d1f]"));
        QCOMPARE(lines.at(1), QStringLiteral("body left cell0,0of3x4 | [a ----0 #1d1d1f]"));
        QCOMPARE(lines.at(2), QStringLiteral("body left cell0,1of3x4 | [b ----0 #1d1d1f]"));
        QCOMPARE(lines.size(), 1 + 12 + 1);
        // Tab in the last cell makes a row.
        for (int i = 0; i < 11; ++i) {
            editor.key(Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
        }
        QCOMPARE(editor.format().value(QStringLiteral("rows")).toInt(), 4);
        editor.removeRow();
        editor.removeColumn();
        QCOMPARE(editor.format().value(QStringLiteral("rows")).toInt(), 3);
        QCOMPARE(editor.format().value(QStringLiteral("columns")).toInt(), 3);
        // Backspace at the start of a cell stays in it.
        editor.key(Qt::Key_Backspace, Qt::NoModifier, QString());
        QCOMPARE(editor.format().value(QStringLiteral("rows")).toInt(), 3);
        editor.removeTable();
        QVERIFY(!editor.format().value(QStringLiteral("table")).toBool());
        lines = describe(editor.document(), false);
        QCOMPARE(lines.first(), QStringLiteral("body left | [antes ----0 #1d1d1f]"));
        QVERIFY(lines.size() <= 2);
        editor.undo();
        QVERIFY(describe(editor.document(), false).size() > 5);
    }

    void images()
    {
        QTemporaryDir dir;
        QImage picture(200, 100, QImage::Format_ARGB32);
        picture.fill(Qt::blue);
        const QString path = dir.filePath(QStringLiteral("azul.png"));
        QVERIFY(picture.save(path));

        Editor editor;
        QSignalSpy said(&editor, &Editor::said);
        QVERIFY(!editor.insertImage(dir.filePath(QStringLiteral("no-está.png"))));
        QCOMPARE(said.size(), 1);
        QVERIFY(editor.insertImage(path));
        editor.select(0, 1);
        QVERIFY(editor.format().value(QStringLiteral("image")).toBool());
        QCOMPARE(qRound(editor.format().value(QStringLiteral("imageWidth")).toDouble() * Doc::pxPerCm), 200);
        editor.setImageWidth(2.0);
        QCOMPARE(editor.format().value(QStringLiteral("imageWidth")).toDouble(), 2.0);
        QCOMPARE(editor.format().value(QStringLiteral("imageHeight")).toDouble(), 1.0);
        QCOMPARE(describe(editor.document(), false).first(), QStringLiteral("body left | [image 76x38 of 200x100]"));

        // A picture wider than the text is brought in to fit.
        QImage wide(3000, 300, QImage::Format_RGB32);
        wide.fill(Qt::green);
        editor.key(Qt::Key_End, Qt::ControlModifier, QString());
        QVERIFY(editor.insertImage(wide));
        editor.select(1, 2);
        QCOMPARE(qRound(editor.format().value(QStringLiteral("imageWidth")).toDouble() * Doc::pxPerCm), qRound(editor.setup().contentPx().width()));

        // A click on it selects it.
        editor.goTo(0);
        editor.press(0, editor.setup().left * Doc::pxPerCm + 30, editor.setup().top * Doc::pxPerCm + 15);
        QVERIFY(editor.format().value(QStringLiteral("image")).toBool());
    }

    void links()
    {
        Editor editor;
        type(editor, QStringLiteral("ver el sitio hoy"));
        editor.select(7, 12);
        editor.setLink(QStringLiteral("example.org"));
        QCOMPARE(editor.format().value(QStringLiteral("link")).toString(), QStringLiteral("https://example.org"));
        QCOMPARE(describe(editor.document(), false).first(),
                 QStringLiteral("body left | [ver el  ----0 #1d1d1f] [sitio --u-0 %1 ->https://example.org] [ hoy ----0 #1d1d1f]").arg(Doc::lookOf(Doc::linkFormat(QString())).color.name()));
        // What is typed after a link is not part of it.
        editor.goTo(12);
        type(editor, QStringLiteral("!"));
        QVERIFY(describe(editor.document(), false).first().contains(QStringLiteral("[! hoy ----0 #1d1d1f]")));
        editor.goTo(9);
        QCOMPARE(editor.format().value(QStringLiteral("link")).toString(), QStringLiteral("https://example.org"));
        editor.removeLink();
        QCOMPARE(describe(editor.document(), false).first(), QStringLiteral("body left | [ver el sitio! hoy ----0 #1d1d1f]"));

        // Without a selection, the text is written.
        editor.key(Qt::Key_End, Qt::ControlModifier, QString());
        editor.setLink(QStringLiteral("ana@example.org"), QStringLiteral(" Ana"));
        QVERIFY(describe(editor.document(), false).first().endsWith(QStringLiteral("->mailto:ana@example.org]")));
    }

    void pageBreaksAndPages()
    {
        Editor editor;
        QSignalSpy pages(&editor, &Editor::pagesChanged);
        type(editor, QStringLiteral("primera"));
        editor.insertPageBreak();
        type(editor, QStringLiteral("segunda"));
        QCOMPARE(editor.pageCount(), 2);
        QVERIFY(pages.size() >= 1);
        QCOMPARE(editor.cursorPage(), 1);
        QVERIFY(editor.format().value(QStringLiteral("pageBreak")).toBool());
        QVERIFY(editor.caretRect().top() >= editor.setup().top * Doc::pxPerCm - 1);
        QVERIFY(editor.caretRect().top() < editor.setup().top * Doc::pxPerCm + 40);
        editor.goTo(3);
        QCOMPARE(editor.cursorPage(), 0);
        // Backspace at the start of the second page joins them again.
        editor.goTo(8);
        editor.key(Qt::Key_Backspace, Qt::NoModifier, QString());
        QCOMPARE(editor.pageCount(), 1);
        QCOMPARE(editor.plainText(), QStringLiteral("primera\nsegunda"));

        // Enough paragraphs spill onto a second sheet by themselves.
        editor.newDocument();
        for (int i = 0; i < 60; ++i) {
            type(editor, QStringLiteral("línea\n"));
        }
        QVERIFY(editor.pageCount() >= 2);
        const int before = editor.pageCount();
        editor.setSetup({{QStringLiteral("paper"), QStringLiteral("A5")}});
        QVERIFY(editor.pageCount() > before);
        QVERIFY(editor.pageWidth() < 600);
        editor.setSetup({{QStringLiteral("landscape"), true}});
        QVERIFY(editor.pageWidth() > editor.pageHeight());
        QVERIFY(editor.modified());
    }

    void mouse()
    {
        Editor editor;
        type(editor, QStringLiteral("hola mundo\notra línea"));
        editor.goTo(5);
        const QRectF caret = editor.caretRect();
        editor.goTo(0);
        editor.press(0, caret.x() + 1, caret.center().y());
        QCOMPARE(editor.cursor().position(), 5);
        editor.selectWord(0, caret.x() + 4, caret.center().y());
        QCOMPARE(editor.selectedText(), QStringLiteral("mundo"));
        editor.selectParagraph(0, caret.x() + 4, caret.center().y());
        QCOMPARE(editor.selectedText(), QStringLiteral("hola mundo"));
        editor.press(0, caret.x(), caret.center().y());
        editor.goTo(15);
        const QRectF second = editor.caretRect();
        editor.goTo(5);
        editor.drag(0, second.x(), second.center().y());
        QCOMPARE(editor.selectedText(), QStringLiteral("mundo\notra"));
        // Shift+click extends.
        editor.press(0, caret.x(), caret.center().y());
        editor.press(0, second.x(), second.center().y(), true);
        QCOMPARE(editor.selectedText(), QStringLiteral("mundo\notra"));

        editor.select(0, 4);
        editor.setLink(QStringLiteral("https://example.org/"));
        editor.goTo(2);
        const QRectF inLink = editor.caretRect();
        QCOMPARE(editor.linkAt(0, inLink.x(), inLink.center().y()), QStringLiteral("https://example.org/"));
        QCOMPARE(editor.linkAt(0, second.x(), second.center().y()), QString());
    }

    void keys()
    {
        Editor editor;
        type(editor, QStringLiteral("uno dos"));
        editor.key(Qt::Key_Home, Qt::NoModifier, QString());
        QCOMPARE(editor.cursor().position(), 0);
        editor.key(Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier, QString());
        QCOMPARE(editor.selectedText(), QStringLiteral("uno "));
        editor.key(Qt::Key_Right, Qt::NoModifier, QString());
        QCOMPARE(editor.cursor().position(), 4);
        QVERIFY(!editor.cursor().hasSelection());
        editor.key(Qt::Key_Delete, Qt::NoModifier, QString());
        QCOMPARE(editor.plainText(), QStringLiteral("uno os"));
        editor.key(Qt::Key_Backspace, Qt::ControlModifier, QString());
        QCOMPARE(editor.plainText(), QStringLiteral("os"));
        editor.key(Qt::Key_Return, Qt::ShiftModifier, QString());
        QCOMPARE(editor.document()->blockCount(), 1);
        editor.key(Qt::Key_A, Qt::ControlModifier, QString());
        type(editor, QStringLiteral("x"));
        QCOMPARE(editor.plainText(), QStringLiteral("x"));
        editor.key(Qt::Key_Tab, Qt::NoModifier, QStringLiteral("\t"));
        QCOMPARE(editor.plainText(), QStringLiteral("x\t"));
        // Not the editor's: the window may do something with them.
        QVERIFY(!editor.key(Qt::Key_F5, Qt::NoModifier, QString()));
        QVERIFY(!editor.key(Qt::Key_S, Qt::ControlModifier, QString()));
        QVERIFY(!editor.key(Qt::Key_Escape, Qt::NoModifier, QStringLiteral("\x1b")));
    }

    void undoRedo()
    {
        Editor editor;
        QVERIFY(!editor.canUndo());
        type(editor, QStringLiteral("hola"));
        editor.selectAll();
        editor.toggleBold();
        QVERIFY(editor.canUndo());
        editor.undo();
        editor.goTo(1);
        QVERIFY(!editor.format().value(QStringLiteral("bold")).toBool());
        QVERIFY(editor.canRedo());
        editor.redo();
        editor.goTo(1);
        QVERIFY(editor.format().value(QStringLiteral("bold")).toBool());
        editor.key(Qt::Key_Z, Qt::ControlModifier, QString());
        editor.key(Qt::Key_Z, Qt::ControlModifier, QString());
        QCOMPARE(editor.plainText(), QString());
        editor.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier, QString());
        QCOMPARE(editor.plainText(), QStringLiteral("hola"));
    }

    void clipboard()
    {
        Editor editor;
        type(editor, QStringLiteral("copia esto"));
        editor.select(0, 5);
        editor.toggleBold();
        editor.copy();
        editor.key(Qt::Key_End, Qt::ControlModifier, QString());
        editor.paste();
        QCOMPARE(editor.plainText(), QStringLiteral("copia estocopia"));
        editor.goTo(13);
        QVERIFY(editor.format().value(QStringLiteral("bold")).toBool());
        editor.select(0, 6);
        editor.cut();
        QCOMPARE(editor.plainText(), QStringLiteral("estocopia"));
    }

    void findAndReplace()
    {
        Editor editor;
        type(editor, QStringLiteral("Casa, casa y casas.\nOtra casa"));
        editor.setFind(QStringLiteral("casa"));
        QCOMPARE(editor.matchCount(), 4);
        QCOMPARE(editor.matchIndex(), 0);
        editor.goTo(0);
        editor.findNext();
        QCOMPARE(editor.matchIndex(), 1);
        editor.findNext();
        QCOMPARE(editor.matchIndex(), 2);
        editor.findNext(false);
        QCOMPARE(editor.matchIndex(), 1);
        editor.findNext(false);
        QCOMPARE(editor.matchIndex(), 4);
        editor.setFind(QStringLiteral("casa"), true, false);
        QCOMPARE(editor.matchCount(), 3);
        editor.setFind(QStringLiteral("casa"), true, true);
        QCOMPARE(editor.matchCount(), 2);

        editor.goTo(0);
        editor.findNext();
        editor.replaceOne(QStringLiteral("torre"));
        QCOMPARE(editor.plainText(), QStringLiteral("Casa, torre y casas.\nOtra casa"));
        QCOMPARE(editor.matchCount(), 1);
        QCOMPARE(editor.matchIndex(), 1);

        editor.setFind(QStringLiteral("a"), false, false);
        const int count = editor.matchCount();
        QCOMPARE(editor.replaceAll(QStringLiteral("AA")), count);
        QCOMPARE(editor.plainText(), QStringLiteral("CAAsAA, torre y cAAsAAs.\nOtrAA cAAsAA"));
        // One step back for all of them.
        editor.undo();
        QCOMPARE(editor.plainText(), QStringLiteral("Casa, torre y casas.\nOtra casa"));
        editor.setFind(QString());
        QCOMPARE(editor.matchCount(), 0);
    }

    void replaceKeepsTheFormat()
    {
        Editor editor;
        type(editor, QStringLiteral("uno dos"));
        editor.select(4, 7);
        editor.toggleBold();
        editor.goTo(0);
        editor.setFind(QStringLiteral("dos"));
        QCOMPARE(editor.replaceAll(QStringLiteral("tres")), 1);
        QCOMPARE(describe(editor.document(), false).first(), QStringLiteral("body left | [uno  ----0 #1d1d1f] [tres b---0 #1d1d1f]"));
    }

    void markdownAsYouType()
    {
        Editor editor;
        const auto style = [&] { return editor.format().value(QStringLiteral("style")).toString(); };
        type(editor, QStringLiteral("# Uno"));
        QCOMPARE(style(), QStringLiteral("h1"));
        QCOMPARE(editor.plainText(), QStringLiteral("Uno"));
        type(editor, QStringLiteral("\n## Dos"));
        QCOMPARE(style(), QStringLiteral("h2"));
        type(editor, QStringLiteral("\n> Cita"));
        QCOMPARE(style(), QStringLiteral("quote"));
        type(editor, QStringLiteral("\n"));
        editor.applyStyle(QStringLiteral("body"));
        type(editor, QStringLiteral("- punto"));
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 1);
        type(editor, QStringLiteral("\n\n1. primero"));
        QCOMPARE(editor.format().value(QStringLiteral("list")).toInt(), 2);
        type(editor, QStringLiteral("\n\nes **muy fuerte** y ~~no~~ vale"));
        QCOMPARE(editor.cursor().block().text(), QStringLiteral("es muy fuerte y no vale"));
        const int at = editor.cursor().block().position();
        editor.select(at + 4, at + 6);
        QVERIFY(editor.format().value(QStringLiteral("bold")).toBool());
        editor.select(at + 16, at + 18);
        QVERIFY(editor.format().value(QStringLiteral("strike")).toBool());
        QVERIFY(!editor.format().value(QStringLiteral("bold")).toBool());
        editor.select(at + 20, at + 23);
        QVERIFY(!editor.format().value(QStringLiteral("strike")).toBool());
        editor.goTo(editor.cursor().block().position() + editor.cursor().block().length() - 1);
        type(editor, QStringLiteral("\n```\ncodigo"));
        QCOMPARE(style(), QStringLiteral("code"));
        QCOMPARE(editor.cursor().block().text(), QStringLiteral("codigo"));
        // Undo gives the typed mark back.
        Editor other;
        type(other, QStringLiteral("# "));
        other.undo();
        QCOMPARE(other.plainText(), QStringLiteral("# "));
        QCOMPARE(other.format().value(QStringLiteral("style")).toString(), QStringLiteral("body"));
        // And it can be turned off.
        Editor plain;
        plain.setProperty("markdown", false);
        type(plain, QStringLiteral("# Uno"));
        QCOMPARE(plain.plainText(), QStringLiteral("# Uno"));
    }

    void separator()
    {
        Editor editor;
        type(editor, QStringLiteral("antes"));
        editor.insertSeparator();
        type(editor, QStringLiteral("después"));
        QCOMPARE(editor.plainText(), QStringLiteral("antes\n* * *\ndespués"));
        QCOMPARE(editor.format().value(QStringLiteral("alignment")).toString(), QStringLiteral("left"));
    }

    void templates()
    {
        Editor editor;
        for (const QString &kind : {QStringLiteral("letter"), QStringLiteral("resume"), QStringLiteral("report"), QStringLiteral("minutes"), QStringLiteral("sample")}) {
            editor.newDocument(kind);
            QVERIFY2(editor.plainText().size() > 200, qPrintable(kind));
            QVERIFY(!editor.modified());
            QVERIFY(!editor.canUndo());
            QCOMPARE(editor.cursor().position(), 0);
            QCOMPARE(editor.name(), QStringLiteral("Sin título"));
        }
        // The sample has one of everything.
        const QString all = describe(editor.document()).join(QLatin1Char('\n'));
        for (const char *wanted : {"title left", "h1 left", "h2 left", "h3 left", "quote left", "code left", "bullet1#3", "number1#3", "cell2,2of3x3", "[image 360x140", "break",
                                   "->https://example.org/", "b---0", "-i--0", "--u-0", "---s0", "----1", "----2", "bg#ffe066", "#d70015", "justify"}) {
            QVERIFY2(all.contains(QLatin1String(wanted)), wanted);
        }
        QCOMPARE(editor.pageCount(), 2);
        QVERIFY(editor.setup().pageNumbers);
        editor.refreshNow();
        QCOMPARE(editor.outline().size(), 6);
        QCOMPARE(editor.outline().last().toMap().value(QStringLiteral("page")).toInt(), 2);
        editor.newDocument();
        QCOMPARE(editor.plainText(), QString());
        QVERIFY(!editor.setup().pageNumbers);
    }

    void saveAndLoad()
    {
        QTemporaryDir dir;
        Editor editor;
        QSignalSpy said(&editor, &Editor::said);
        QVERIFY(!editor.save());
        editor.newDocument(QStringLiteral("sample"));
        type(editor, QStringLiteral("x"));
        QVERIFY(editor.modified());
        const QStringList before = describe(editor.document());
        for (const char *suffix : {"docx", "odt", "rtf"}) {
            const QString path = dir.filePath(QStringLiteral("muestra.") + QLatin1String(suffix));
            QVERIFY2(editor.saveAs(path), suffix);
            QVERIFY(!editor.modified());
            QVERIFY(!editor.lossy());
            QCOMPARE(editor.path(), path);
            Editor other;
            QVERIFY2(other.load(path), suffix);
            QCOMPARE(other.name(), QStringLiteral("muestra.") + QLatin1String(suffix));
            QVERIFY(!other.modified());
            QVERIFY(!other.canUndo());
            COMPARE_LINES(describe(other.document()), before);
            QCOMPARE(other.setup(), editor.setup());
            QCOMPARE(other.pageCount(), editor.pageCount());
        }
        // A PDF is a copy: the document stays the file it was.
        const QString was = editor.path();
        QVERIFY(editor.saveAs(dir.filePath(QStringLiteral("muestra.pdf"))));
        QCOMPARE(editor.path(), was);
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("muestra.pdf"))).size() > 1000);

        QVERIFY(editor.saveAs(dir.filePath(QStringLiteral("muestra.txt"))));
        QVERIFY(editor.lossy());
        type(editor, QStringLiteral("y"));
        QVERIFY(editor.save());
        QVERIFY(!editor.modified());

        QCOMPARE(said.size(), 0);
        QVERIFY(!editor.load(dir.filePath(QStringLiteral("no-está.docx"))));
        QCOMPARE(said.size(), 1);
        // A failed load leaves what was open.
        QCOMPARE(editor.name(), QStringLiteral("muestra.txt"));
        QVERIFY(!editor.saveAs(QStringLiteral("/proc/no/se/puede.docx")));
        QCOMPARE(said.size(), 2);
    }

    void paints()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        QImage sheet(qRound(editor.pageWidth()), qRound(editor.pageHeight()), QImage::Format_ARGB32_Premultiplied);
        sheet.fill(Qt::white);
        {
            QPainter painter(&sheet);
            editor.paintPage(&painter, 0, true);
        }
        // Ink inside the margins, none in them; the header above.
        int inside = 0;
        int margin = 0;
        const int left = qRound(editor.setup().left * Doc::pxPerCm);
        for (int y = 100; y < 600; ++y) {
            for (int x = 0; x < sheet.width(); ++x) {
                if (sheet.pixelColor(x, y).lightness() < 200) {
                    (x >= left - 2 && x <= sheet.width() - left + 2 ? inside : margin) += 1;
                }
            }
        }
        QVERIFY(inside > 2000);
        QCOMPARE(margin, 0);
        int header = 0;
        for (int y = 20; y < 80; ++y) {
            for (int x = 0; x < sheet.width(); ++x) {
                header += sheet.pixelColor(x, y).lightness() < 200 ? 1 : 0;
            }
        }
        QVERIFY(header > 50);
    }
};

QTEST_MAIN(TestEditor)
#include "tst_editor.moc"
