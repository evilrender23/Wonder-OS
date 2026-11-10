// The document as the window uses it: editing, tables, the clipboard, and the
// way to a file and back.
#include "book.h"
#include "xlsx.h"
#include "zip.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestBook : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString data() const { return m_dir.filePath(QStringLiteral("data")); }
    QString file(const char *name) const { return m_dir.filePath(QLatin1String(name)); }

private Q_SLOTS:
    void startsClean()
    {
        Book book(data());
        QVERIFY(!book.isOpen());
        book.create();
        QVERIFY(book.isOpen());
        QVERIFY(!book.modified());
        QVERIFY(!book.canUndo());
        QCOMPARE(book.sheets().size(), 1);
    }

    void editsAndUndoes()
    {
        Book book(data());
        book.create();
        QCOMPARE(book.setInput(0, 0, QStringLiteral("1")), QString());
        book.setInput(1, 0, QStringLiteral("2"));
        book.setInput(2, 0, QStringLiteral("3,5"));
        QCOMPARE(book.setInput(3, 0, QStringLiteral("=SUMA(A1:A3)")), QString());
        QCOMPARE(book.display(3, 0), QStringLiteral("6,5"));
        // English names are read too, and shown in Spanish.
        book.setInput(4, 0, QStringLiteral("=SUM(A1:A3)*2"));
        QCOMPARE(book.display(4, 0), QStringLiteral("13"));
        QCOMPARE(book.input(4, 0), QStringLiteral("=SUMA(A1:A3)*2"));
        QVERIFY(book.modified());
        book.undo();
        QCOMPARE(book.display(4, 0), QString());
        book.redo();
        QCOMPARE(book.display(4, 0), QStringLiteral("13"));
        // What makes no sense is refused, and says why.
        QVERIFY(!book.setInput(5, 0, QStringLiteral("=SUMA(")).isEmpty());
    }

    void copiesWithRelativeReferences()
    {
        Book book(data());
        book.create();
        book.setInput(0, 0, QStringLiteral("10"));
        book.setInput(1, 0, QStringLiteral("20"));
        book.setInput(0, 1, QStringLiteral("=A1*2"));
        const QString tsv = book.copy(0, 1, 0, 1, false);
        book.paste(1, 1, 1, 1, tsv);
        QCOMPARE(book.input(1, 1), QStringLiteral("=A2*2"));
        QCOMPARE(book.display(1, 1), QStringLiteral("40"));
        // Text from elsewhere lands as typed, a cell per tab and per line.
        book.forgetCopy();
        book.paste(3, 0, 3, 0, QStringLiteral("a\tb\n1\t2"));
        QCOMPARE(book.display(3, 1), QStringLiteral("b"));
        QCOMPARE(book.display(4, 1), QStringLiteral("2"));
        book.fillDown(0, 1, 3, 1);
        QCOMPARE(book.input(2, 1), QStringLiteral("=A3*2"));
    }

    void rowsAndColumns()
    {
        Book book(data());
        book.create();
        book.setInput(0, 0, QStringLiteral("1"));
        book.setInput(1, 0, QStringLiteral("=A1+1"));
        book.insertRows(0, 1);
        QCOMPARE(book.input(2, 0), QStringLiteral("=A2+1"));
        book.deleteRows(0, 1);
        QCOMPARE(book.input(1, 0), QStringLiteral("=A1+1"));
        book.insertCols(0, 2);
        QCOMPARE(book.input(1, 2), QStringLiteral("=C1+1"));
        QVERIFY(book.merge(4, 0, 5, 1));
        QCOMPARE(book.extent(5, 1).value(QStringLiteral("r1")).toInt(), 4);
        QVERIFY(book.unmerge(4, 0, 5, 1));
        book.setFreeze(1, 1);
        QCOMPARE(book.freezeRows(), 1);
        QCOMPARE(book.freezeCols(), 1);
    }

    void formats()
    {
        Book book(data());
        book.create();
        book.setInput(0, 0, QStringLiteral("1234,5"));
        book.setFormat(0, 0, 0, 0, QStringLiteral("currency"));
        QCOMPARE(book.display(0, 0), QStringLiteral("1.234,50 €"));
        QCOMPARE(book.info(0, 0).value(QStringLiteral("family")).toString(), QStringLiteral("currency"));
        book.setFormat(0, 0, 0, 0, QStringLiteral("percent"));
        QCOMPARE(book.display(0, 0), QStringLiteral("123450%"));
        book.setStyle(0, 0, 0, 0, QStringLiteral("bold"), true);
        QVERIFY(book.info(0, 0).value(QStringLiteral("bold")).toBool());
        book.setInput(1, 0, QStringLiteral("VERDADERO"));
        QCOMPARE(book.info(1, 0).value(QStringLiteral("kind")).toString(), QStringLiteral("check"));
        book.toggleCheck(1, 0);
        QCOMPARE(book.input(1, 0), QStringLiteral("FALSO"));
    }

    void tablesSortAndFilter()
    {
        Book book(data());
        book.sample();
        QVERIFY(book.tableAt(3, 2).contains(QStringLiteral("index")));
        const QVariantMap stats = book.stats(1, 2, 7, 3);
        QCOMPARE(stats.value(QStringLiteral("count")).toInt(), 14);
        QCOMPARE(stats.value(QStringLiteral("sum")).toDouble(), 134200.0);

        book.create();
        const char *rows[][2] = {{"Fruta", "Kilos"}, {"Pera", "3"}, {"Manzana", "7"}, {"Uva", "5"}};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 2; ++c) {
                book.setInput(r, c, QString::fromUtf8(rows[r][c]));
            }
        }
        QVERIFY(book.makeTable(0, 0, 3, 1));
        const int index = book.tableAt(1, 0).value(QStringLiteral("index")).toInt();
        book.setTableTotals(index, true);
        QVERIFY(book.tableAt(4, 1).value(QStringLiteral("totals")).toBool());
        QCOMPARE(book.display(4, 1), QStringLiteral("15"));
        // Sorting leaves the headers and the totals where they are.
        book.sort(0, 0, 4, 1, 1, true);
        QCOMPARE(book.display(0, 0), QStringLiteral("Fruta"));
        QCOMPARE(book.display(1, 0), QStringLiteral("Pera"));
        QCOMPARE(book.display(3, 0), QStringLiteral("Manzana"));
        QCOMPARE(book.display(4, 1), QStringLiteral("15"));
        // The filter hides rows, and the total counts what is left in sight.
        QVERIFY(book.hasFilter());
        book.setFilterAllowed(0, {QStringLiteral("Pera"), QStringLiteral("Uva")}, false);
        QVERIFY(book.workbook().rowHidden(0, 3));
        QCOMPARE(book.display(4, 1), QStringLiteral("8"));
        book.removeTable(index);
        QVERIFY(!book.tableAt(1, 0).contains(QStringLiteral("index")));
    }

    void findsAndReplaces()
    {
        Book book(data());
        book.sample();
        const QVariantMap found = book.find(QStringLiteral("marzo"), 0, 0, true, false, false);
        QVERIFY(found.value(QStringLiteral("found")).toBool());
        QCOMPARE(found.value(QStringLiteral("row")).toInt(), 3);
        QCOMPARE(book.replaceAll(QStringLiteral("Marzo"), QStringLiteral("Mar."), true, false), 1);
        QCOMPARE(book.display(3, 0), QStringLiteral("Mar."));
        QVERIFY(!book.find(QStringLiteral("zzz"), 0, 0, true, false, false).value(QStringLiteral("found")).toBool());
    }

    void chartsFollowTheirRange()
    {
        Book book(data());
        book.sample();
        const int before = book.charts().size();
        const int id = book.addChart(0, 0, 8, 1, QStringLiteral("line"));
        QVERIFY(id != 0);
        QCOMPARE(book.charts().size(), before + 1);
        book.setChart(id, QStringLiteral("type"), QStringLiteral("pie"));
        book.setChart(id, QStringLiteral("title"), QStringLiteral("Norte"));
        const QVariantMap chart = book.charts().constLast().toMap();
        QCOMPARE(chart.value(QStringLiteral("type")).toString(), QStringLiteral("pie"));
        QCOMPARE(chart.value(QStringLiteral("title")).toString(), QStringLiteral("Norte"));
        QCOMPARE(chart.value(QStringLiteral("range")).toString(), QStringLiteral("A1:B9"));
        book.removeChart(id);
        QCOMPARE(book.charts().size(), before);
    }

    void formulaHelp()
    {
        Book book(data());
        book.create();
        const QVariantList found = book.completions(QStringLiteral("PROM"));
        QVERIFY(!found.isEmpty());
        QCOMPARE(found.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("PROMEDIO"));
        const QString text = QStringLiteral("=SI(A1>B2;C3:C9;");
        const QVariantMap context = book.formulaContext(text, text.size());
        QCOMPARE(context.value(QStringLiteral("name")).toString(), QStringLiteral("SI"));
        QCOMPARE(context.value(QStringLiteral("argument")).toInt(), 2);
        QVERIFY(!context.value(QStringLiteral("help")).toString().isEmpty());
        const QVariantList refs = book.references(text);
        QCOMPARE(refs.size(), 3);
        QVERIFY(refs.at(0).toMap().value(QStringLiteral("color")) != refs.at(1).toMap().value(QStringLiteral("color")));
    }

    void templates()
    {
        Book book(data());
        for (const char *kind : {"blank", "budget", "invoice", "tasks", "schedule"}) {
            book.create(QLatin1String(kind));
            QVERIFY(book.isOpen());
            QVERIFY(!book.modified());
            QVERIFY(!book.canUndo());
            const QVariantMap used = book.usedArea();
            for (int r = 0; r <= used.value(QStringLiteral("r2")).toInt(); ++r) {
                for (int c = 0; c <= used.value(QStringLiteral("c2")).toInt(); ++c) {
                    QVERIFY2(!book.display(r, c).startsWith(QLatin1Char('#')), qPrintable(QStringLiteral("%1 %2,%3: %4").arg(QLatin1String(kind)).arg(r).arg(c).arg(book.display(r, c))));
                }
            }
            QCOMPARE(book.templatePreview(QLatin1String(kind)).size(), 11);
        }
    }

    void xlsxRoundTrip()
    {
        Book book(data());
        book.sample();
        const QString path = file("ventas.xlsx");
        QCOMPARE(book.saveAs(path), QString());
        QVERIFY(!book.modified());
        QCOMPARE(book.path(), path);

        Book again(data());
        QCOMPARE(again.load(path), QString());
        QCOMPARE(again.sheets(), book.sheets());
        for (int r = 0; r < 10; ++r) {
            for (int c = 0; c < 7; ++c) {
                QCOMPARE(again.display(r, c), book.display(r, c));
                QCOMPARE(again.input(r, c), book.input(r, c));
            }
        }
        QCOMPARE(again.info(0, 0).value(QStringLiteral("bold")), book.info(0, 0).value(QStringLiteral("bold")));
        QCOMPARE(again.freezeRows(), book.freezeRows());
        QCOMPARE(again.colWidth(0), book.colWidth(0));
        QCOMPARE(again.charts().size(), book.charts().size());
        QCOMPARE(again.conditionals().size(), book.conditionals().size());
        QVERIFY(again.tableAt(3, 2).contains(QStringLiteral("index")));
        QVERIFY(!again.modified());
    }

    void csvAndTsv()
    {
        Book book(data());
        book.create();
        book.setInput(0, 0, QStringLiteral("Nombre; con punto y coma"));
        book.setInput(0, 1, QStringLiteral("12,5"));
        book.setInput(1, 0, QStringLiteral("dice \"hola\""));
        book.setInput(1, 1, QStringLiteral("=B1*2"));
        for (const char *name : {"datos.csv", "datos.tsv"}) {
            QCOMPARE(book.exportTo(file(name)), QString());
            Book again(data());
            QCOMPARE(again.load(file(name)), QString());
            QCOMPARE(again.display(0, 0), QStringLiteral("Nombre; con punto y coma"));
            QCOMPARE(again.display(1, 0), QStringLiteral("dice \"hola\""));
            QCOMPARE(again.display(0, 1), QStringLiteral("12,5"));
            // A CSV holds what is seen, not the formulas.
            QCOMPARE(again.display(1, 1), QStringLiteral("25"));
        }
    }

    void ods()
    {
        const QByteArray content = R"(<?xml version="1.0" encoding="UTF-8"?>
<office:document-content xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0" xmlns:style="urn:oasis:names:tc:opendocument:xmlns:style:1.0" xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0" xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" xmlns:fo="urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0" xmlns:number="urn:oasis:names:tc:opendocument:xmlns:datastyle:1.0" office:version="1.3">
<office:automatic-styles>
<number:percentage-style style:name="N11"><number:number number:decimal-places="1" number:min-integer-digits="1"/><number:text>%</number:text></number:percentage-style>
<style:style style:name="co1" style:family="table-column"><style:table-column-properties style:column-width="5.08cm"/></style:style>
<style:style style:name="ce1" style:family="table-cell"><style:text-properties fo:font-weight="bold" fo:color="#ff0000"/><style:table-cell-properties fo:background-color="#ffff00"/></style:style>
<number:number-style style:name="N136"><number:number number:decimal-places="0" number:min-integer-digits="1" number:grouping="true"/><number:text> €</number:text></number:number-style>
<style:style style:name="ce2" style:family="table-cell" style:data-style-name="N11"/>
<style:style style:name="ce3" style:family="table-cell" style:data-style-name="N136"/>
</office:automatic-styles>
<office:body><office:spreadsheet>
<table:table table:name="Datos">
<table:table-column table:style-name="co1"/><table:table-column table:number-columns-repeated="16383"/>
<table:table-row>
<table:table-cell table:style-name="ce1" office:value-type="string" table:number-columns-spanned="2" table:number-rows-spanned="1"><text:p>Título</text:p></table:table-cell><table:covered-table-cell/>
<table:table-cell office:value-type="float" office:value="2.5" table:number-columns-repeated="2"><text:p>2,5</text:p></table:table-cell>
</table:table-row>
<table:table-row table:number-rows-repeated="2">
<table:table-cell table:style-name="ce3" office:value-type="float" office:value="4"><text:p>4 €</text:p></table:table-cell>
<table:table-cell table:formula="of:=SUM([.C$1:.D$1];[.A2])*2" office:value-type="float" office:value="18"><text:p>18</text:p></table:table-cell>
<table:table-cell office:value-type="date" office:date-value="2026-03-05"><text:p>05/03/26</text:p></table:table-cell>
<table:table-cell table:style-name="ce2" office:value-type="percentage" office:value="0.125"><text:p>12,5%</text:p></table:table-cell>
<table:table-cell office:value-type="boolean" office:boolean-value="true"><text:p>VERDADERO</text:p></table:table-cell>
<table:table-cell table:formula="of:=[$'Otra hoja'.A1]&amp;&quot;; fin&quot;" office:value-type="string" office:string-value="x; fin"><text:p>x; fin</text:p></table:table-cell>
</table:table-row>
<table:table-row table:number-rows-repeated="1048573"><table:table-cell table:number-columns-repeated="16384"/></table:table-row>
</table:table>
<table:table table:name="Otra hoja"><table:table-column/><table:table-row><table:table-cell office:value-type="string"><text:p>x</text:p></table:table-cell></table:table-row></table:table>
</office:spreadsheet></office:body></office:document-content>)";
        ZipWriter zip;
        zip.add(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.spreadsheet", true);
        zip.add(QStringLiteral("content.xml"), content);
        QFile out(file("fuera.ods"));
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(zip.finish());
        out.close();

        Book book(data());
        QCOMPARE(book.load(file("fuera.ods")), QString());
        QCOMPARE(book.sheets(), QStringList({QStringLiteral("Datos"), QStringLiteral("Otra hoja")}));
        QCOMPARE(book.display(0, 0), QStringLiteral("Título"));
        QVERIFY(book.info(0, 0).value(QStringLiteral("bold")).toBool());
        QCOMPARE(book.info(0, 0).value(QStringLiteral("fill")).toString(), QStringLiteral("#ffff00"));
        QCOMPARE(book.extent(0, 1).value(QStringLiteral("c1")).toInt(), 0);
        QCOMPARE(book.display(0, 3), QStringLiteral("2,5"));
        QCOMPARE(book.colWidth(0), 192.0);
        // The row that comes twice, its formula moved with it.
        QCOMPARE(book.input(1, 1), QStringLiteral("=SUMA(C$1:D$1;A2)*2"));
        QCOMPARE(book.input(2, 1), QStringLiteral("=SUMA(C$1:D$1;A3)*2"));
        QCOMPARE(book.display(2, 1), QStringLiteral("18"));
        QCOMPARE(book.display(1, 0), QStringLiteral("4 €"));
        QCOMPARE(book.display(1, 2), QStringLiteral("05/03/2026"));
        QCOMPARE(book.display(1, 3), QStringLiteral("12,5%"));
        QCOMPARE(book.info(1, 4).value(QStringLiteral("kind")).toString(), QStringLiteral("check"));
        QCOMPARE(book.input(1, 5), QStringLiteral("='Otra hoja'!A1&\"; fin\""));
        QCOMPARE(book.display(1, 5), QStringLiteral("x; fin"));
        // An .ods is read, not written: saving asks for another name.
        QVERIFY(!book.saveAs(file("fuera2.ods")).isEmpty());
    }

    void versionsAndRecovery()
    {
        Book book(data());
        book.create();
        book.setInput(0, 0, QStringLiteral("uno"));
        const QString path = file("versiones.xlsx");
        QCOMPARE(book.saveAs(path), QString());
        book.setInput(0, 0, QStringLiteral("dos"));
        QCOMPARE(book.save(), QString());
        const QVariantList versions = book.versions();
        QVERIFY(versions.size() >= 1);
        const QString oldest = versions.constLast().toMap().value(QStringLiteral("file")).toString();
        QVERIFY(!book.versionPreview(oldest).isEmpty());
        QCOMPARE(book.restoreVersion(oldest), QString());
        QCOMPARE(book.display(0, 0), QStringLiteral("uno"));
        QVERIFY(book.modified());

        // No more than twenty copies of a document are kept.
        for (int i = 0; i < 24; ++i) {
            book.setInput(1, 0, QString::number(i));
            QCOMPARE(book.save(), QString());
        }
        QCOMPARE(book.versions().size(), 20);

        Book other(m_dir.filePath(QStringLiteral("data2")));
        other.create();
        other.setInput(0, 0, QStringLiteral("sin guardar"));
        other.saveRecovery();
        Book later(m_dir.filePath(QStringLiteral("data2")));
        const QVariantList found = later.recoveries();
        QCOMPARE(found.size(), 1);
        QCOMPARE(later.recover(found.first().toMap().value(QStringLiteral("file")).toString()), QString());
        QCOMPARE(later.display(0, 0), QStringLiteral("sin guardar"));
        QVERIFY(later.modified());
    }

    void pdf()
    {
        Book book(data());
        book.sample();
        QCOMPARE(book.exportTo(file("ventas.pdf")), QString());
        QFile out(file("ventas.pdf"));
        QVERIFY(out.open(QIODevice::ReadOnly));
        QVERIFY(out.read(5).startsWith("%PDF"));
        QVERIFY(out.size() > 2000);
    }
};

QTEST_MAIN(TestBook)
#include "tst_book.moc"
