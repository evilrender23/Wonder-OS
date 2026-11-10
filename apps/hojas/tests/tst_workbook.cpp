#include "numfmt.h"
#include "workbook.h"

#include <QtTest>

class TestWorkbook : public QObject
{
    Q_OBJECT

    static QString shown(Workbook &book, const char *cell, int sheet = 0)
    {
        CellRef ref;
        Formula::parseCell(QString::fromLatin1(cell), &ref);
        return book.display(sheet, ref.row, ref.col);
    }
    static void put(Workbook &book, const char *cell, const QString &typed, int sheet = 0)
    {
        CellRef ref;
        QVERIFY(Formula::parseCell(QString::fromLatin1(cell), &ref));
        const QString error = book.setInput(sheet, ref.row, ref.col, typed);
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    static QString typed(Workbook &book, const char *cell, int sheet = 0)
    {
        CellRef ref;
        Formula::parseCell(QString::fromLatin1(cell), &ref);
        return book.input(sheet, ref.row, ref.col);
    }

private slots:
    void typing()
    {
        Workbook book;
        QCOMPARE(book.sheetCount(), 1);
        QCOMPARE(book.sheetName(0), QStringLiteral("Hoja 1"));
        QVERIFY(!book.modified());
        put(book, "A1", QStringLiteral("12,5"));
        put(book, "A2", QStringLiteral("hola"));
        put(book, "A3", QStringLiteral("10/11/2026"));
        put(book, "A4", QStringLiteral("25%"));
        put(book, "A5", QStringLiteral("'007"));
        put(book, "A6", QStringLiteral("=A1*2"));
        put(book, "A7", QStringLiteral("verdadero"));
        QVERIFY(book.modified());
        QCOMPARE(shown(book, "A1"), QStringLiteral("12,5"));
        QCOMPARE(shown(book, "A3"), QStringLiteral("10/11/2026"));
        QCOMPARE(book.value(0, 2, 0).num, 46336.0);
        QCOMPARE(shown(book, "A4"), QStringLiteral("25%"));
        QCOMPARE(typed(book, "A4"), QStringLiteral("25%"));
        QCOMPARE(shown(book, "A5"), QStringLiteral("007"));
        QCOMPARE(typed(book, "A5"), QStringLiteral("'007"));
        QVERIFY(book.value(0, 4, 0).isText());
        QCOMPARE(shown(book, "A6"), QStringLiteral("25"));
        QCOMPARE(typed(book, "A6"), QStringLiteral("=A1*2"));
        QCOMPARE(book.formula(0, 5, 0), QStringLiteral("A1*2"));
        QCOMPARE(shown(book, "A7"), QStringLiteral("VERDADERO"));
        // A formula that makes no sense is refused, and the cell keeps what it had.
        QVERIFY(!book.setInput(0, 5, 0, QStringLiteral("=A1*")).isEmpty());
        QCOMPARE(shown(book, "A6"), QStringLiteral("25"));
        // Emptied, the cell goes.
        put(book, "A2", QString());
        QVERIFY(!book.cell(0, 1, 0));
        // Spanish names are shown, whatever was typed.
        put(book, "B1", QStringLiteral("=sum(A1;a6)"));
        QCOMPARE(typed(book, "B1"), QStringLiteral("=SUMA(A1;A6)"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("37,5"));
        // Dates out of formulas show as dates.
        book.setNow(46336.5);
        put(book, "B2", QStringLiteral("=HOY()"));
        QCOMPARE(shown(book, "B2"), QStringLiteral("10/11/2026"));
        put(book, "B3", QStringLiteral("=A3+1"));
        QCOMPARE(book.value(0, 2, 1).num, 46337.0);
    }

    void incrementalRecalc()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("1"));
        put(book, "A2", QStringLiteral("=A1+1"));
        put(book, "A3", QStringLiteral("=A2+1"));
        put(book, "B1", QStringLiteral("=SUMA(A1:A3)"));
        put(book, "C1", QStringLiteral("=SUMA(A:A)"));
        put(book, "D1", QStringLiteral("=B1*2"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("6"));
        QCOMPARE(shown(book, "D1"), QStringLiteral("12"));
        put(book, "A1", QStringLiteral("10"));
        QCOMPARE(shown(book, "A3"), QStringLiteral("12"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("33"));
        QCOMPARE(shown(book, "C1"), QStringLiteral("33"));
        QCOMPARE(shown(book, "D1"), QStringLiteral("66"));
        // A new cell inside a range counts at once.
        put(book, "A9", QStringLiteral("100"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("33"));
        QCOMPARE(shown(book, "C1"), QStringLiteral("133"));
        // A formula replaced by a value lets go of what it read.
        put(book, "A2", QStringLiteral("5"));
        put(book, "A1", QStringLiteral("0"));
        QCOMPARE(shown(book, "A3"), QStringLiteral("6"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("11"));
    }

    void longChain()
    {
        // Deep chains must not run out of stack, and one change reaches the end.
        Workbook book;
        book.beginGroup();
        put(book, "A1", QStringLiteral("1"));
        for (int r = 1; r < 20000; ++r) {
            book.setInput(0, r, 0, QStringLiteral("=A%1+1").arg(r));
        }
        book.endGroup();
        QCOMPARE(book.value(0, 19999, 0).num, 20000.0);
        QElapsedTimer timer;
        timer.start();
        put(book, "A1", QStringLiteral("5"));
        QCOMPARE(book.value(0, 19999, 0).num, 20004.0);
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(book.canUndo());
        book.undo();
        QCOMPARE(book.value(0, 19999, 0).num, 20000.0);
    }

    void cycles()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("=A1+1"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("#CIRC!"));
        put(book, "B1", QStringLiteral("=B2"));
        put(book, "B2", QStringLiteral("=B3"));
        put(book, "B3", QStringLiteral("=B1"));
        put(book, "C1", QStringLiteral("=B1+1"));
        put(book, "D1", QStringLiteral("7"));
        put(book, "D2", QStringLiteral("=D1*2"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("#CIRC!"));
        QCOMPARE(shown(book, "B3"), QStringLiteral("#CIRC!"));
        QCOMPARE(shown(book, "C1"), QStringLiteral("#CIRC!"));
        QCOMPARE(shown(book, "D2"), QStringLiteral("14"));
        // Broken, the circle heals.
        put(book, "B3", QStringLiteral("4"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("4"));
        QCOMPARE(shown(book, "C1"), QStringLiteral("5"));
        put(book, "E1", QStringLiteral("=SUMA(E1:E3)"));
        QCOMPARE(shown(book, "E1"), QStringLiteral("#CIRC!"));
    }

    void errors()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("=1/0"));
        put(book, "A2", QStringLiteral("=\"a\"*2"));
        put(book, "A3", QStringLiteral("=Nada!A1"));
        put(book, "A4", QStringLiteral("=NOEXISTE()"));
        put(book, "A5", QStringLiteral("=BUSCARV(1;C1:D2;2;FALSO)"));
        put(book, "A6", QStringLiteral("=A1+1"));
        put(book, "A7", QStringLiteral("=SI.ERROR(A1;0)"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("#DIV/0!"));
        QCOMPARE(shown(book, "A2"), QStringLiteral("#VALUE!"));
        QCOMPARE(shown(book, "A3"), QStringLiteral("#REF!"));
        QCOMPARE(shown(book, "A4"), QStringLiteral("#NAME?"));
        QCOMPARE(shown(book, "A5"), QStringLiteral("#N/A"));
        QCOMPARE(shown(book, "A6"), QStringLiteral("#DIV/0!"));
        QCOMPARE(shown(book, "A7"), QStringLiteral("0"));
    }

    void sheets()
    {
        Workbook book;
        const int second = book.addSheet();
        QCOMPARE(book.sheetName(second), QStringLiteral("Hoja 2"));
        put(book, "A1", QStringLiteral("5"), second);
        put(book, "A1", QStringLiteral("='Hoja 2'!A1*2"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("10"));
        put(book, "A1", QStringLiteral("6"), second);
        QCOMPARE(shown(book, "A1"), QStringLiteral("12"));
        // Renaming carries the formulas along.
        QVERIFY(book.renameSheet(second, QStringLiteral("Ventas")));
        QCOMPARE(typed(book, "A1"), QStringLiteral("=Ventas!A1*2"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("12"));
        QVERIFY(!book.renameSheet(second, QStringLiteral("hoja 1")));
        QVERIFY(!book.renameSheet(second, QStringLiteral("a/b")));
        QVERIFY(!book.renameSheet(second, QStringLiteral("  ")));
        const int copy = book.duplicateSheet(second);
        QCOMPARE(book.sheetName(copy), QStringLiteral("Ventas (2)"));
        QCOMPARE(shown(book, "A1", copy), QStringLiteral("6"));
        QVERIFY(book.moveSheet(0, 2));
        QCOMPARE(book.sheetName(2), QStringLiteral("Hoja 1"));
        QCOMPARE(shown(book, "A1", 2), QStringLiteral("12"));
        // Without its sheet the reference breaks; undo brings both back.
        QVERIFY(book.removeSheet(book.sheetIndex(QStringLiteral("Ventas"))));
        QCOMPARE(shown(book, "A1", book.sheetIndex(QStringLiteral("Hoja 1"))), QStringLiteral("#REF!"));
        book.undo();
        QCOMPARE(book.sheetCount(), 3);
        QCOMPARE(shown(book, "A1", 2), QStringLiteral("12"));
        Workbook single;
        QVERIFY(!single.removeSheet(0));
    }

    void undoRedo()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("1"));
        put(book, "A2", QStringLiteral("=A1+1"));
        put(book, "A1", QStringLiteral("5"));
        QCOMPARE(shown(book, "A2"), QStringLiteral("6"));
        book.undo();
        QCOMPARE(shown(book, "A1"), QStringLiteral("1"));
        QCOMPARE(shown(book, "A2"), QStringLiteral("2"));
        book.undo();
        QVERIFY(!book.cell(0, 1, 0));
        book.redo();
        book.redo();
        QCOMPARE(shown(book, "A2"), QStringLiteral("6"));
        QVERIFY(!book.canRedo());
        // The graph was rebuilt with the cells: changes still reach.
        put(book, "A1", QStringLiteral("9"));
        QCOMPARE(shown(book, "A2"), QStringLiteral("10"));
        book.undo();
        put(book, "B1", QStringLiteral("x"));
        QVERIFY(!book.canRedo());
        // A group is one step.
        book.beginGroup();
        put(book, "C1", QStringLiteral("1"));
        put(book, "C2", QStringLiteral("2"));
        book.endGroup();
        book.undo();
        QVERIFY(!book.cell(0, 0, 2) && !book.cell(0, 1, 2));
        QCOMPARE(shown(book, "B1"), QStringLiteral("x"));
    }

    void copyPaste()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("1"));
        put(book, "A2", QStringLiteral("2"));
        put(book, "B1", QStringLiteral("=A1*10"));
        put(book, "C1", QStringLiteral("=$A$1+A1"));
        const Clip clip = book.copy(0, Area{0, 1, 0, 2});
        QCOMPARE(clip.tsv, QStringLiteral("10\t2"));
        const Area dest = book.paste(clip, 0, Area{1, 1, 1, 1});
        QVERIFY(dest == (Area{1, 1, 1, 2}));
        QCOMPARE(typed(book, "B2"), QStringLiteral("=A2*10"));
        QCOMPARE(typed(book, "C2"), QStringLiteral("=$A$1+A2"));
        QCOMPARE(shown(book, "B2"), QStringLiteral("20"));
        QCOMPARE(shown(book, "C2"), QStringLiteral("3"));
        // Over a bigger area, it repeats.
        book.paste(book.copy(0, Area{0, 1, 0, 1}), 0, Area{2, 1, 4, 1});
        QCOMPARE(typed(book, "B5"), QStringLiteral("=A5*10"));
        // Values only.
        book.paste(clip, 0, Area{6, 0, 6, 0}, QStringLiteral("values"));
        QCOMPARE(typed(book, "A7"), QStringLiteral("10"));
        // Off the top, a reference has nowhere to go.
        book.paste(book.copy(0, Area{1, 1, 1, 1}), 0, Area{0, 4, 0, 4});
        QCOMPARE(typed(book, "E1"), QStringLiteral("=D1*10"));
        book.paste(book.copy(0, Area{1, 2, 1, 2}), 0, Area{0, 5, 0, 5});
        QCOMPARE(typed(book, "F1"), QStringLiteral("=$A$1+D1"));
        book.paste(book.copy(0, Area{1, 1, 1, 1}), 0, Area{19, 0, 19, 0});
        QCOMPARE(typed(book, "A20"), QStringLiteral("=#REF!*10"));
        // Cut: the cells move as they are and the source empties.
        Clip cut = book.copy(0, Area{0, 1, 0, 1});
        cut.cut = true;
        book.paste(cut, 0, Area{9, 3, 9, 3});
        QCOMPARE(typed(book, "D10"), QStringLiteral("=A1*10"));
        QVERIFY(!book.cell(0, 0, 1));
        // Text from elsewhere.
        const Area pasted = book.pasteText(0, 12, 0, QStringLiteral("uno\t2,5\n\"con\ttab\"\t=1+1\n"));
        QVERIFY(pasted == (Area{12, 0, 13, 1}));
        QCOMPARE(book.value(0, 12, 1).num, 2.5);
        QCOMPARE(shown(book, "A14"), QStringLiteral("con\ttab"));
        QCOMPARE(shown(book, "B14"), QStringLiteral("2"));
        book.undo();
        QVERIFY(!book.cell(0, 12, 0) && !book.cell(0, 13, 1));
    }

    void fills()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("3"));
        put(book, "B1", QStringLiteral("=A1*2"));
        book.fillDown(0, Area{0, 0, 3, 1});
        QCOMPARE(typed(book, "B4"), QStringLiteral("=A4*2"));
        QCOMPARE(shown(book, "B4"), QStringLiteral("6"));
        book.fillRight(0, Area{0, 1, 0, 3});
        QCOMPARE(typed(book, "D1"), QStringLiteral("=C1*2"));
        QCOMPARE(shown(book, "D1"), QStringLiteral("24"));
        put(book, "F1", QStringLiteral("1"));
        put(book, "F2", QStringLiteral("3"));
        book.fillSeries(0, Area{0, 5, 1, 5}, Area{0, 5, 5, 5});
        QCOMPARE(shown(book, "F6"), QStringLiteral("11"));
        put(book, "G1", QStringLiteral("x"));
        book.fillSeries(0, Area{0, 6, 0, 6}, Area{0, 6, 2, 6});
        QCOMPARE(shown(book, "G3"), QStringLiteral("x"));
        put(book, "H1", QStringLiteral("10"));
        put(book, "I1", QStringLiteral("20"));
        book.fillSeries(0, Area{0, 7, 0, 8}, Area{0, 7, 0, 10});
        QCOMPARE(shown(book, "K1"), QStringLiteral("40"));
    }

    void insertDelete()
    {
        Workbook book;
        for (int r = 0; r < 5; ++r) {
            book.setInput(0, r, 0, QString::number(r + 1));
        }
        put(book, "B1", QStringLiteral("=SUMA(A1:A5)"));
        put(book, "B2", QStringLiteral("=A5*2"));
        put(book, "C7", QStringLiteral("=A1"));
        book.addSheet(QStringLiteral("Otra"));
        put(book, "A1", QStringLiteral("='Hoja 1'!A5"), 1);
        book.setRowHeight(0, 4, 4, 40);
        QVERIFY(book.merge(0, Area{6, 4, 7, 5}));

        book.insertRows(0, 2, 2);
        QCOMPARE(typed(book, "B1"), QStringLiteral("=SUMA(A1:A7)"));
        QCOMPARE(typed(book, "B2"), QStringLiteral("=A7*2"));
        QCOMPARE(typed(book, "C9"), QStringLiteral("=A1"));
        QCOMPARE(typed(book, "A1", 1), QStringLiteral("='Hoja 1'!A7"));
        QCOMPARE(shown(book, "A7"), QStringLiteral("5"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("15"));
        QCOMPARE(book.rowHeight(0, 6), 40.0);
        QCOMPARE(book.rowHeight(0, 4), Workbook::DefaultRowHeight);
        QVERIFY(book.sheet(0).merges.first() == (Area{8, 4, 9, 5}));

        book.deleteRows(0, 2, 2);
        QCOMPARE(typed(book, "B1"), QStringLiteral("=SUMA(A1:A5)"));
        book.deleteRows(0, 4, 1);
        QCOMPARE(typed(book, "B1"), QStringLiteral("=SUMA(A1:A4)"));
        QCOMPARE(typed(book, "B2"), QStringLiteral("=#REF!*2"));
        QCOMPARE(shown(book, "B2"), QStringLiteral("#REF!"));
        QCOMPARE(shown(book, "A1", 1), QStringLiteral("#REF!"));
        QCOMPARE(shown(book, "B1"), QStringLiteral("10"));
        book.undo();
        QCOMPARE(shown(book, "B2"), QStringLiteral("10"));

        book.insertCols(0, 0, 1);
        QCOMPARE(typed(book, "C1"), QStringLiteral("=SUMA(B1:B5)"));
        QCOMPARE(shown(book, "C1"), QStringLiteral("15"));
        QVERIFY(!book.cell(0, 0, 0));
        book.deleteCols(0, 0, 1);
        QCOMPARE(typed(book, "B1"), QStringLiteral("=SUMA(A1:A5)"));
        book.deleteCols(0, 0, 1);
        QCOMPARE(typed(book, "A1"), QStringLiteral("=SUMA(#REF!)"));
    }

    void styles()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("1234,5"));
        book.changeStyle(0, Area{0, 0, 1, 1}, [](Style &s) {
            s.bold = true;
            s.format = QStringLiteral("#,##0.00");
        });
        QVERIFY(book.cellStyle(0, 0, 0).bold);
        QVERIFY(book.cellStyle(0, 1, 1).bold);
        QVERIFY(!book.cellStyle(0, 2, 2).bold);
        QCOMPARE(shown(book, "A1"), QStringLiteral("1.234,50"));
        // The same look is the same style.
        QCOMPARE(book.cell(0, 0, 0)->style, book.cell(0, 1, 1)->style);
        // Typing keeps the format; clearing contents too.
        put(book, "A1", QStringLiteral("2"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("2,00"));
        book.clearArea(0, Area{0, 0, 0, 0});
        QVERIFY(book.cell(0, 0, 0) && book.cellStyle(0, 0, 0).bold);
        book.clearArea(0, Area{0, 0, 5, 5}, true, true);
        QVERIFY(!book.cell(0, 0, 0) && !book.cell(0, 1, 1));
        // Whole columns are handled without walking a million rows.
        QElapsedTimer timer;
        timer.start();
        put(book, "C3", QStringLiteral("x"));
        book.clearArea(0, Area{0, 2, Limits::Rows - 1, 2});
        QVERIFY(!book.cell(0, 2, 2));
        QVERIFY(timer.elapsed() < 500);

        book.setBorders(0, Area{0, 0, 2, 2}, QStringLiteral("outline"));
        QCOMPARE(book.cellStyle(0, 0, 0).borders, 3);
        QCOMPARE(book.cellStyle(0, 2, 2).borders, 12);
        QCOMPARE(book.cellStyle(0, 1, 1).borders, 0);
        book.setBorders(0, Area{0, 0, 2, 2}, QStringLiteral("all"), QStringLiteral("#ff0000"));
        QCOMPARE(book.cellStyle(0, 1, 1).borders, 15);
        QCOMPARE(book.cellStyle(0, 1, 1).borderColor, QStringLiteral("#ff0000"));
        book.setBorders(0, Area{0, 0, 2, 2}, QStringLiteral("none"));
        QVERIFY(!book.cell(0, 1, 1));
        // A text format keeps what is typed as it is.
        book.changeStyle(0, Area{5, 0, 5, 0}, [](Style &s) {
            s.format = QStringLiteral("@");
        });
        put(book, "A6", QStringLiteral("0012"));
        QCOMPARE(shown(book, "A6"), QStringLiteral("0012"));
        QVERIFY(book.value(0, 5, 0).isText());
        QCOMPARE(typed(book, "A6"), QStringLiteral("0012"));
    }

    void conditional()
    {
        Workbook book;
        for (int r = 0; r < 5; ++r) {
            book.setInput(0, r, 0, QString::number((r + 1) * 10));
        }
        put(book, "A6", QStringLiteral("10"));
        put(book, "A7", QStringLiteral("Urgente: llamar"));
        CondFormat rule;
        rule.area = Area{0, 0, 9, 0};
        rule.op = QStringLiteral(">");
        rule.a = QStringLiteral("25");
        rule.fill = QStringLiteral("#ffcccc");
        book.addConditional(0, rule);
        QCOMPARE(book.effectiveStyle(0, 1, 0).fill, QString());
        QCOMPARE(book.effectiveStyle(0, 2, 0).fill, QStringLiteral("#ffcccc"));
        QCOMPARE(book.effectiveStyle(0, 6, 0).fill, QString());
        QCOMPARE(book.effectiveStyle(0, 8, 0).fill, QString());
        QCOMPARE(book.effectiveStyle(0, 2, 1).fill, QString());
        put(book, "A2", QStringLiteral("99"));
        QCOMPARE(book.effectiveStyle(0, 1, 0).fill, QStringLiteral("#ffcccc"));
        book.clearConditionals(0, Area{0, 0, 0, 0});

        rule.op = QStringLiteral("between");
        rule.a = QStringLiteral("30");
        rule.b = QStringLiteral("40");
        rule.fill = QString();
        rule.color = QStringLiteral("#00aa00");
        rule.bold = true;
        book.addConditional(0, rule);
        QVERIFY(book.effectiveStyle(0, 2, 0).bold);
        QCOMPARE(book.effectiveStyle(0, 3, 0).color, QStringLiteral("#00aa00"));
        QVERIFY(!book.effectiveStyle(0, 4, 0).bold);
        book.clearConditionals(0, rule.area);

        rule.op = QStringLiteral("contains");
        rule.a = QStringLiteral("urgente");
        book.addConditional(0, rule);
        QVERIFY(book.effectiveStyle(0, 6, 0).bold);
        QVERIFY(!book.effectiveStyle(0, 0, 0).bold);
        book.clearConditionals(0, rule.area);

        rule.op = QStringLiteral("duplicate");
        book.addConditional(0, rule);
        QVERIFY(book.effectiveStyle(0, 0, 0).bold);
        QVERIFY(book.effectiveStyle(0, 5, 0).bold);
        QVERIFY(!book.effectiveStyle(0, 2, 0).bold);
        put(book, "A6", QStringLiteral("11"));
        QVERIFY(!book.effectiveStyle(0, 0, 0).bold);
    }

    void sorting()
    {
        Workbook book;
        const QStringList names = {QStringLiteral("Nombre"), QStringLiteral("pera"), QStringLiteral("Uva"), QStringLiteral("manzana"), QStringLiteral("kiwi")};
        const QStringList amounts = {QStringLiteral("Kg"), QStringLiteral("20"), QStringLiteral("5"), QString(), QStringLiteral("12")};
        for (int r = 0; r < 5; ++r) {
            book.setInput(0, r, 0, names.at(r));
            book.setInput(0, r, 1, amounts.at(r));
            book.setInput(0, r, 2, r == 0 ? QStringLiteral("Doble") : QStringLiteral("=B%1*2").arg(r + 1));
        }
        book.sort(0, Area{0, 0, 4, 2}, 0, true, true);
        QCOMPARE(shown(book, "A1"), QStringLiteral("Nombre"));
        QCOMPARE(shown(book, "A2"), QStringLiteral("kiwi"));
        QCOMPARE(shown(book, "A3"), QStringLiteral("manzana"));
        QCOMPARE(shown(book, "A5"), QStringLiteral("Uva"));
        // Formulas travel with their rows.
        QCOMPARE(typed(book, "C2"), QStringLiteral("=B2*2"));
        QCOMPARE(shown(book, "C2"), QStringLiteral("24"));
        QCOMPARE(shown(book, "C5"), QStringLiteral("10"));
        book.sort(0, Area{0, 0, 4, 2}, 1, false, true);
        QCOMPARE(shown(book, "A2"), QStringLiteral("pera"));
        QCOMPARE(shown(book, "A4"), QStringLiteral("Uva"));
        // Blanks last, either way.
        QCOMPARE(shown(book, "A5"), QStringLiteral("manzana"));
        book.sort(0, Area{0, 0, 4, 2}, 1, true, true);
        QCOMPARE(shown(book, "A2"), QStringLiteral("Uva"));
        QCOMPARE(shown(book, "A5"), QStringLiteral("manzana"));
        book.undo();
        QCOMPARE(shown(book, "A2"), QStringLiteral("pera"));
    }

    void filter()
    {
        Workbook book;
        const QStringList fruit = {QStringLiteral("Fruta"), QStringLiteral("pera"), QStringLiteral("uva"), QStringLiteral("pera"), QStringLiteral("kiwi")};
        for (int r = 0; r < 5; ++r) {
            book.setInput(0, r, 0, fruit.at(r));
            book.setInput(0, r, 1, r == 0 ? QStringLiteral("Kg") : QString::number(r));
        }
        book.setFilter(0, Area{2, 0, 2, 0});
        QVERIFY(book.sheet(0).hasFilter);
        QVERIFY(book.sheet(0).filterArea == (Area{0, 0, 4, 1}));
        QCOMPARE(book.filterValues(0, 0), QStringList({QStringLiteral("kiwi"), QStringLiteral("pera"), QStringLiteral("uva")}));
        QCOMPARE(book.filterValues(0, 1), QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")}));
        book.setFilterAllowed(0, 0, {QStringLiteral("pera")}, false);
        QVERIFY(!book.rowHidden(0, 0));
        QVERIFY(!book.rowHidden(0, 1));
        QVERIFY(book.rowHidden(0, 2));
        QVERIFY(!book.rowHidden(0, 3));
        QVERIFY(book.rowHidden(0, 4));
        book.setFilterAllowed(0, 1, {QStringLiteral("3")}, false);
        QVERIFY(book.rowHidden(0, 1));
        QVERIFY(!book.rowHidden(0, 3));
        book.setFilterAllowed(0, 0, {}, true);
        book.setFilterAllowed(0, 1, {}, true);
        QVERIFY(!book.rowHidden(0, 2));
        book.setFilterAllowed(0, 0, {QStringLiteral("uva")}, false);
        book.clearFilter(0);
        QVERIFY(!book.sheet(0).hasFilter);
        QVERIFY(!book.rowHidden(0, 1));
    }

    void findReplace()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("Hola mundo"));
        put(book, "B2", QStringLiteral("hola"));
        put(book, "C3", QStringLiteral("=LARGO(\"hola\")"));
        put(book, "A4", QStringLiteral("adiós"));
        int row = 0, col = 0;
        QVERIFY(book.find(0, QStringLiteral("hola"), false, false, true, &row, &col));
        QCOMPARE(QPair(row, col), QPair(1, 1));
        QVERIFY(book.find(0, QStringLiteral("hola"), false, false, true, &row, &col));
        QCOMPARE(QPair(row, col), QPair(2, 2));
        QVERIFY(book.find(0, QStringLiteral("hola"), false, false, true, &row, &col));
        QCOMPARE(QPair(row, col), QPair(0, 0));
        QVERIFY(book.find(0, QStringLiteral("hola"), false, false, false, &row, &col));
        QCOMPARE(QPair(row, col), QPair(2, 2));
        row = col = 0;
        QVERIFY(book.find(0, QStringLiteral("hola"), true, true, true, &row, &col));
        QCOMPARE(QPair(row, col), QPair(1, 1));
        QVERIFY(!book.find(0, QStringLiteral("nada"), false, false, true, &row, &col));
        QCOMPARE(book.replaceAll(0, QStringLiteral("hola"), QStringLiteral("adiós"), false, false), 3);
        QCOMPARE(shown(book, "A1"), QStringLiteral("adiós mundo"));
        QCOMPARE(shown(book, "C3"), QStringLiteral("5"));
        book.undo();
        QCOMPARE(shown(book, "A1"), QStringLiteral("Hola mundo"));
        QCOMPARE(shown(book, "C3"), QStringLiteral("4"));
        QVERIFY(book.replaceAt(0, 1, 1, QStringLiteral("hola"), QStringLiteral("42"), true, true));
        QCOMPARE(book.value(0, 1, 1).num, 42.0);
    }

    void validation()
    {
        Workbook book;
        Validation list;
        list.area = Area{0, 0, 9, 0};
        list.kind = QStringLiteral("list");
        list.items = {QStringLiteral("Alta"), QStringLiteral("Media"), QStringLiteral("Baja")};
        book.setValidation(0, list);
        QVERIFY(book.setInput(0, 0, 0, QStringLiteral("media")).isEmpty());
        QVERIFY(!book.setInput(0, 1, 0, QStringLiteral("Otra")).isEmpty());
        QVERIFY(!book.cell(0, 1, 0));
        QVERIFY(book.setInput(0, 1, 0, QString()).isEmpty());
        QVERIFY(book.validationAt(0, 3, 0));
        QVERIFY(!book.validationAt(0, 3, 1));

        Validation number;
        number.area = Area{0, 1, 9, 1};
        number.kind = QStringLiteral("integer");
        number.min = QStringLiteral("1");
        number.max = QStringLiteral("10");
        number.message = QStringLiteral("Del 1 al 10.");
        book.setValidation(0, number);
        QVERIFY(book.setInput(0, 0, 1, QStringLiteral("7")).isEmpty());
        QCOMPARE(book.setInput(0, 1, 1, QStringLiteral("11")), QStringLiteral("Del 1 al 10."));
        QVERIFY(!book.setInput(0, 1, 1, QStringLiteral("2,5")).isEmpty());
        QVERIFY(!book.setInput(0, 1, 1, QStringLiteral("x")).isEmpty());
        number.kind = QStringLiteral("number");
        number.max = QString();
        book.setValidation(0, number);
        QVERIFY(book.setInput(0, 1, 1, QStringLiteral("2500,5")).isEmpty());
        QVERIFY(!book.setInput(0, 1, 1, QStringLiteral("0")).isEmpty());
        book.clearValidation(0, Area{0, 0, 0, 5});
        QVERIFY(book.setInput(0, 1, 1, QStringLiteral("0")).isEmpty());
    }

    void merges()
    {
        Workbook book;
        put(book, "A1", QStringLiteral("título"));
        put(book, "B1", QStringLiteral("sobra"));
        QVERIFY(book.merge(0, Area{0, 0, 0, 2}));
        QVERIFY(!book.cell(0, 0, 1));
        Area area;
        QVERIFY(book.mergeAt(0, 0, 2, &area));
        QVERIFY(area == (Area{0, 0, 0, 2}));
        // Typed anywhere in it, it lands on the corner.
        put(book, "C1", QStringLiteral("nuevo"));
        QCOMPARE(shown(book, "A1"), QStringLiteral("nuevo"));
        QVERIFY(!book.merge(0, Area{3, 3, 3, 3}));
        QVERIFY(book.merge(0, Area{0, 1, 1, 3}));
        QCOMPARE(book.sheet(0).merges.size(), 1);
        QVERIFY(book.unmerge(0, Area{1, 1, 1, 1}));
        QVERIFY(!book.unmerge(0, Area{1, 1, 1, 1}));
        book.undo();
        QVERIFY(book.mergeAt(0, 1, 3, nullptr));
    }

    void layout()
    {
        Workbook book;
        QCOMPARE(book.colWidth(0, 3), Workbook::DefaultColWidth);
        book.setColWidth(0, 1, 2, 150);
        QCOMPARE(book.colWidth(0, 2), 150.0);
        book.setColsHidden(0, 2, 2, true);
        QVERIFY(book.colHidden(0, 2));
        book.setRowsHidden(0, 4, 6, true);
        QVERIFY(book.rowHidden(0, 5));
        book.setRowsHidden(0, 0, 100, false);
        QVERIFY(!book.rowHidden(0, 5));
        book.setFreeze(0, 1, 2);
        QCOMPARE(book.sheet(0).freezeRows, 1);
        QCOMPARE(book.sheet(0).freezeCols, 2);
        book.insertCols(0, 0, 1);
        QCOMPARE(book.colWidth(0, 3), 150.0);
        QVERIFY(book.colHidden(0, 3));
        QCOMPARE(book.sheet(0).freezeCols, 3);
        put(book, "A1", QStringLiteral("un texto bastante largo"));
        put(book, "A2", QStringLiteral("x"));
        const double width = book.fittingWidth(0, 0, [](const QString &text, const Style &) {
            return text.size() * 7.0;
        });
        QCOMPARE(width, 23 * 7.0);
        int rows = 0, cols = 0;
        book.used(0, &rows, &cols);
        QCOMPARE(QPair(rows, cols), QPair(2, 1));
        put(book, "XFD1048576", QStringLiteral("fin"));
        book.used(0, &rows, &cols);
        QCOMPARE(QPair(rows, cols), QPair(Limits::Rows, Limits::Cols));
        QVERIFY(!book.setInput(0, Limits::Rows, 0, QStringLiteral("x")).isEmpty());
    }

    void statsAndRegion()
    {
        Workbook book;
        put(book, "B2", QStringLiteral("1"));
        put(book, "B3", QStringLiteral("2"));
        put(book, "C2", QStringLiteral("x"));
        put(book, "C3", QStringLiteral("=B2+B3"));
        put(book, "F9", QStringLiteral("lejos"));
        const SelectionStats stats = book.stats(0, Area{0, 0, 5, 5});
        QCOMPARE(stats.count, 4);
        QCOMPARE(stats.numbers, 3);
        QCOMPARE(stats.sum, 6.0);
        QCOMPARE(stats.average, 2.0);
        QCOMPARE(stats.min, 1.0);
        QCOMPARE(stats.max, 3.0);
        QVERIFY(book.region(0, 1, 1) == (Area{1, 1, 2, 2}));
        QVERIFY(book.region(0, 0, 0) == (Area{0, 0, 2, 2}));
        QVERIFY(book.region(0, 20, 20) == (Area{20, 20, 20, 20}));
    }

    void charts()
    {
        Workbook book;
        const QStringList header = {QString(), QStringLiteral("2025"), QStringLiteral("2026")};
        const QStringList names = {QStringLiteral("Norte"), QStringLiteral("Sur"), QStringLiteral("Este")};
        for (int c = 0; c < 3; ++c) {
            book.setInput(0, 0, c, c == 0 ? QStringLiteral("Zona") : QLatin1Char('\'') + header.at(c));
        }
        for (int r = 0; r < 3; ++r) {
            book.setInput(0, r + 1, 0, names.at(r));
            book.setInput(0, r + 1, 1, QString::number((r + 1) * 10));
            book.setInput(0, r + 1, 2, r == 1 ? QStringLiteral("n/d") : QString::number((r + 1) * 15));
        }
        Chart chart;
        chart.area = Area{0, 0, 3, 2};
        chart.title = QStringLiteral("Ventas");
        const int id = book.addChart(0, chart);
        QVERIFY(id > 0);
        chart = book.sheet(0).charts.first();
        ChartData data = book.chartData(0, chart);
        QCOMPARE(data.categories, names);
        QCOMPARE(data.seriesNames, QStringList({QStringLiteral("2025"), QStringLiteral("2026")}));
        QCOMPARE(data.series.at(0), QList<double>({10, 20, 30}));
        QCOMPARE(data.series.at(1).at(0), 15.0);
        QVERIFY(std::isnan(data.series.at(1).at(1)));
        // Tied to the cells: a change shows.
        put(book, "B2", QStringLiteral("99"));
        QCOMPARE(book.chartData(0, chart).series.at(0).at(0), 99.0);
        // And it follows them when rows come in above.
        book.insertRows(0, 0, 2);
        chart = book.sheet(0).charts.first();
        QVERIFY(chart.area == (Area{2, 0, 5, 2}));
        QCOMPARE(book.chartData(0, chart).categories, names);
        chart.type = QStringLiteral("pie");
        chart.headerRow = false;
        book.updateChart(0, chart);
        QCOMPARE(book.sheet(0).charts.first().type, QStringLiteral("pie"));
        QCOMPARE(book.chartData(0, chart).categories.size(), 4);
        book.removeChart(0, id);
        QVERIFY(book.sheet(0).charts.isEmpty());
        book.undo();
        QCOMPARE(book.sheet(0).charts.size(), 1);
    }

    void volatileFormulas()
    {
        Workbook book;
        book.setNow(46336.25);
        put(book, "A1", QStringLiteral("=AHORA()"));
        put(book, "A2", QStringLiteral("=A1+1"));
        put(book, "B1", QStringLiteral("5"));
        put(book, "B2", QStringLiteral("=DESREF(A1;0;1)"));
        put(book, "B3", QStringLiteral("=INDIRECTO(\"B\"&1)*2"));
        QCOMPARE(book.value(0, 1, 0).num, 46337.25);
        QCOMPARE(shown(book, "B2"), QStringLiteral("5"));
        book.setNow(46340.0);
        book.recalc(true);
        QCOMPARE(book.value(0, 1, 0).num, 46341.0);
        // What OFFSET and INDIRECT read is not in the graph, but they go again every time.
        put(book, "B1", QStringLiteral("8"));
        QCOMPARE(shown(book, "B2"), QStringLiteral("8"));
        QCOMPARE(shown(book, "B3"), QStringLiteral("16"));
    }

    void loading()
    {
        Workbook book;
        Style money;
        money.format = QStringLiteral("#,##0.00");
        const int style = book.styleId(money);
        book.loadCell(0, 0, 0, Value(2.0), QString(), style);
        book.loadCell(0, 1, 0, Value(999.0), QStringLiteral("A1*3"), 0);
        book.loadCell(0, 2, 0, Value(42.0), QStringLiteral("FUNCIONRARA(A1)"), 0);
        book.loadCell(0, 3, 0, Value(), QStringLiteral("1+"), 0);
        book.finishLoading();
        QVERIFY(!book.modified());
        QVERIFY(!book.canUndo());
        QCOMPARE(shown(book, "A1"), QStringLiteral("2,00"));
        // Calculated afresh, not trusted.
        QCOMPARE(shown(book, "A2"), QStringLiteral("6"));
        // What is not known here keeps the value the file brought.
        QCOMPARE(shown(book, "A3"), QStringLiteral("42"));
        QCOMPARE(book.formula(0, 2, 0), QStringLiteral("FUNCIONRARA(A1)"));
        QCOMPARE(shown(book, "A4"), QStringLiteral("=1+"));
    }

    void bigSheet()
    {
        // A hundred thousand cells and their sums, quick enough to work with.
        Workbook book;
        QElapsedTimer timer;
        timer.start();
        for (int r = 0; r < 50000; ++r) {
            book.loadCell(0, r, 0, Value(double(r)), QString(), 0);
            book.loadCell(0, r, 1, Value(), QStringLiteral("A%1*2").arg(r + 1), 0);
        }
        book.loadCell(0, 0, 3, Value(), QStringLiteral("SUM(B:B)"), 0);
        book.finishLoading();
        QCOMPARE(book.value(0, 0, 3).num, 2.0 * (49999.0 * 50000 / 2));
        const qint64 loaded = timer.restart();
        put(book, "A1", QStringLiteral("1000"));
        QCOMPARE(book.value(0, 0, 3).num, 2.0 * (49999.0 * 50000 / 2) + 2000);
        const qint64 edited = timer.elapsed();
        qInfo("load+calc %lld ms, edit %lld ms", loaded, edited);
        QVERIFY(loaded < 5000);
        QVERIFY(edited < 500);
    }
};

QTEST_GUILESS_MAIN(TestWorkbook)
#include "tst_workbook.moc"
