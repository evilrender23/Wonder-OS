#include "database.h"
#include "report.h"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

class TestReport : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Database m_db;

    static QVariantMap column(const QString &name, const QString &total = {}, bool show = true)
    {
        return {{QStringLiteral("name"), name}, {QStringLiteral("show"), show}, {QStringLiteral("total"), total}};
    }

    static QList<Report::Line> lines(const Report::Document &document)
    {
        QList<Report::Line> all;
        for (const QList<Report::Line> &page : document.pages) {
            all.append(page);
        }
        return all;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_db.create(m_dir.filePath(QStringLiteral("r.db"))));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE ventas (id INTEGER PRIMARY KEY, zona TEXT, producto TEXT, unidades INTEGER, importe REAL)")));
        QVERIFY(m_db.exec(QStringLiteral("INSERT INTO ventas (zona, producto, unidades, importe) VALUES ('Sur', 'Té', 2, 10.5), ('Norte', 'Café', 3, 20), ('Sur', 'Cacao', 5, 4.25), ('Norte', 'Té', 1, 5.25), (NULL, 'Café', 4, 40)")));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE muchas (n INTEGER, grupo INTEGER)")));
        QVERIFY(m_db.exec(QStringLiteral("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 500) INSERT INTO muchas SELECT x, x % 7 FROM c")));
        m_db.touch(true);
    }

    void tabular()
    {
        const Report::Document document = Report::build(m_db, {{QStringLiteral("title"), QStringLiteral("Ventas")}, {QStringLiteral("source"), QStringLiteral("ventas")}});
        QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
        QCOMPARE(document.title, QStringLiteral("Ventas"));
        QCOMPARE(document.headers, (QStringList{QStringLiteral("Id"), QStringLiteral("Zona"), QStringLiteral("Producto"), QStringLiteral("Unidades"), QStringLiteral("Importe")}));
        QCOMPARE(document.rows, 5);
        QCOMPARE(document.pages.size(), 1);
        QCOMPARE(document.pages.first().size(), 5);
        QCOMPARE(document.right, (QList<bool>{true, false, false, true, true}));
        double sum = 0;
        for (const double width : document.widths) {
            QVERIFY(width > 0);
            sum += width;
        }
        QVERIFY(qAbs(sum - 1) < 1e-9);
    }

    void totals()
    {
        const Report::Document document = Report::build(m_db, {{QStringLiteral("source"), QStringLiteral("ventas")},
            {QStringLiteral("columns"), QVariantList{column(QStringLiteral("producto")), column(QStringLiteral("id"), {}, false), column(QStringLiteral("unidades"), QStringLiteral("sum")),
                                                     column(QStringLiteral("importe"), QStringLiteral("avg")), column(QStringLiteral("no_existe"))}}});
        QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
        QCOMPARE(document.headers.size(), 3);
        const QList<Report::Line> all = lines(document);
        QCOMPARE(all.size(), 6);
        QCOMPARE(all.last().type, Report::Line::Total);
        QCOMPARE(all.last().cells, (QStringList{QStringLiteral("Total (5)"), QStringLiteral("15"), QStringLiteral("16,00")}));
    }

    void grouped()
    {
        const Report::Document document = Report::build(m_db, {{QStringLiteral("source"), QStringLiteral("ventas")}, {QStringLiteral("groupBy"), QStringLiteral("zona")},
            {QStringLiteral("columns"), QVariantList{column(QStringLiteral("producto")), column(QStringLiteral("unidades"), QStringLiteral("sum")), column(QStringLiteral("importe"), QStringLiteral("max"))}}});
        QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
        QStringList shape;
        for (const Report::Line &line : lines(document)) {
            shape.append(line.type == Report::Line::Group ? QStringLiteral("G:") + line.text
                         : line.type == Report::Line::Subtotal ? QStringLiteral("S:") + line.cells.join(QLatin1Char('|'))
                         : line.type == Report::Line::Total ? QStringLiteral("T:") + line.cells.join(QLatin1Char('|')) : QStringLiteral("R"));
        }
        QCOMPARE(shape, (QStringList{QStringLiteral("G:(sin valor)"), QStringLiteral("R"), QStringLiteral("S:Subtotal (1)|4|40,00"),
                                     QStringLiteral("G:Norte"), QStringLiteral("R"), QStringLiteral("R"), QStringLiteral("S:Subtotal (2)|4|20,00"),
                                     QStringLiteral("G:Sur"), QStringLiteral("R"), QStringLiteral("R"), QStringLiteral("S:Subtotal (2)|7|10,50"),
                                     QStringLiteral("T:Total (5)|15|40,00")}));
    }

    void fromQuery()
    {
        Report::Document document = Report::build(m_db, {{QStringLiteral("sql"), QStringLiteral("SELECT producto, importe FROM ventas WHERE zona = :zona ORDER BY importe;")}}, {{QStringLiteral("zona"), QStringLiteral("Sur")}});
        QVERIFY2(document.error.isEmpty(), qPrintable(document.error));
        QCOMPARE(document.rows, 2);
        QCOMPARE(document.pages.first().first().cells.first(), QStringLiteral("Cacao"));
        document = Report::build(m_db, {{QStringLiteral("sql"), QStringLiteral("DELETE FROM ventas")}});
        QVERIFY(!document.error.isEmpty());
        document = Report::build(m_db, {{QStringLiteral("sql"), QStringLiteral("SELECT 1; DELETE FROM ventas")}});
        QVERIFY(!document.error.isEmpty());
        QCOMPARE(m_db.rowCount(QStringLiteral("ventas")), 5);
        document = Report::build(m_db, {{QStringLiteral("source"), QStringLiteral("nada")}});
        QVERIFY(!document.error.isEmpty());
    }

    void pages()
    {
        const int first = Report::linesPerPage(false, true);
        const int other = Report::linesPerPage(false, false);
        QVERIFY(first > 10 && other > first);
        QVERIFY(Report::linesPerPage(true, false) < other);
        Report::Document document = Report::build(m_db, {{QStringLiteral("source"), QStringLiteral("muchas")}});
        QCOMPARE(document.rows, 500);
        QCOMPARE(document.pages.size(), 1 + (500 - first + other - 1) / other);
        QCOMPARE(document.pages.first().size(), first);
        QCOMPARE(lines(document).size(), 500);

        // A heading is never the last line of a page, and no line is lost.
        document = Report::build(m_db, {{QStringLiteral("source"), QStringLiteral("muchas")}, {QStringLiteral("groupBy"), QStringLiteral("grupo")},
                                        {QStringLiteral("columns"), QVariantList{column(QStringLiteral("n"), QStringLiteral("count"))}}});
        QCOMPARE(lines(document).size(), 500 + 7 + 7 + 1);
        for (int i = 0; i < document.pages.size(); ++i) {
            QVERIFY(!document.pages.at(i).isEmpty());
            QVERIFY(document.pages.at(i).size() <= (i == 0 ? first : other));
            QVERIFY(document.pages.at(i).last().type != Report::Line::Group);
        }
    }

    void numbers()
    {
        QCOMPARE(Report::number(1234567, true), QStringLiteral("1.234.567"));
        QCOMPARE(Report::number(12345.5, false), QStringLiteral("12.345,50"));
        // Spanish does not group four figures.
        QCOMPARE(Report::number(1234.5, false), QStringLiteral("1234,50"));
    }

    void painting()
    {
        const Report::Document document = Report::build(m_db, {{QStringLiteral("title"), QStringLiteral("Ventas")}, {QStringLiteral("source"), QStringLiteral("ventas")}});
        const QSizeF size = Report::pageSize(false);
        QImage image(size.toSize(), QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        Report::paint(&painter, document, 0);
        // Out of range paints nothing and does not crash.
        Report::paint(&painter, document, 7);
        painter.end();
        int inked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                inked += image.pixel(x, y) != 0xffffffff;
            }
        }
        QVERIFY(inked > 500);
    }

    void pdf()
    {
        const Report::Document document = Report::build(m_db, {{QStringLiteral("title"), QStringLiteral("Muchas filas")}, {QStringLiteral("source"), QStringLiteral("muchas")}});
        const QString path = m_dir.filePath(QStringLiteral("informe.pdf"));
        QString error;
        QVERIFY2(Report::exportPdf(document, path, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QVERIFY(bytes.startsWith("%PDF-"));
        QVERIFY(bytes.size() > 2000);
        QVERIFY(!Report::exportPdf(document, m_dir.filePath(QStringLiteral("no/existe/x.pdf")), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestReport)
#include "tst_report.moc"
