#include "formula.h"
#include "numfmt.h"
#include "workbook.h"

#include <QtTest>

/// The formulas of the sheet, one by one, against a small workbook.
class TestFormula : public QObject
{
    Q_OBJECT

    Workbook book;

    Value calc(const QString &formula)
    {
        const QString error = book.setInput(0, 30, 10, QLatin1Char('=') + formula);
        if (!error.isEmpty()) {
            return Value(QStringLiteral("PARSE: ") + error);
        }
        return book.value(0, 30, 10);
    }
    static QString show(const Value &v)
    {
        switch (v.type) {
        case Value::Empty:
            return QStringLiteral("<empty>");
        case Value::Number:
            return QString::number(v.num, 'g', 12);
        case Value::Text:
            return QLatin1Char('"') + v.text + QLatin1Char('"');
        case Value::Bool:
            return v.truth() ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
        case Value::Error:
            return Value::errorText(v.err);
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        // A  B       C      D
        // 1  manzana 10     2026-01-15
        // 2  pera    20     2026-02-20
        // 3  manzana 30     2026-03-25
        // 4  uva     40     2026-04-30
        // 5  (blank) 50     texto
        const QStringList fruit = {QStringLiteral("manzana"), QStringLiteral("pera"), QStringLiteral("manzana"), QStringLiteral("uva"), QString()};
        const QStringList dates = {QStringLiteral("15/01/2026"), QStringLiteral("20/02/2026"), QStringLiteral("25/03/2026"), QStringLiteral("30/04/2026"), QStringLiteral("texto")};
        for (int r = 0; r < 5; ++r) {
            QVERIFY(book.setInput(0, r, 0, QString::number(r + 1)).isEmpty());
            QVERIFY(book.setInput(0, r, 1, fruit.at(r)).isEmpty());
            QVERIFY(book.setInput(0, r, 2, QString::number((r + 1) * 10)).isEmpty());
            QVERIFY(book.setInput(0, r, 3, dates.at(r)).isEmpty());
        }
        book.addSheet(QStringLiteral("Datos"));
        book.addSheet(QStringLiteral("Mi hoja"));
        book.setInput(1, 0, 0, QStringLiteral("7"));
        book.setInput(1, 1, 0, QStringLiteral("8"));
        book.setInput(2, 0, 0, QStringLiteral("100"));
        book.setNow(NumFmt::serial(QDateTime(QDate(2026, 11, 10), QTime(12, 0))));
    }

    void names()
    {
        QCOMPARE(Formula::columnName(0), QStringLiteral("A"));
        QCOMPARE(Formula::columnName(25), QStringLiteral("Z"));
        QCOMPARE(Formula::columnName(26), QStringLiteral("AA"));
        QCOMPARE(Formula::columnName(16383), QStringLiteral("XFD"));
        QCOMPARE(Formula::columnIndex(QStringLiteral("XFD")), 16383);
        QCOMPARE(Formula::columnIndex(QStringLiteral("XFE")), -1);
        QCOMPARE(Formula::cellName(1048575, 16383), QStringLiteral("XFD1048576"));
        CellRef ref;
        QVERIFY(Formula::parseCell(QStringLiteral("$B$3"), &ref));
        QCOMPARE(ref.row, 2);
        QCOMPARE(ref.col, 1);
        QVERIFY(ref.absRow && ref.absCol);
        QVERIFY(!Formula::parseCell(QStringLiteral("B0"), &ref));
        QVERIFY(!Formula::parseCell(QStringLiteral("B1048577"), &ref));
        int r1, c1, r2, c2;
        QVERIFY(Formula::parseArea(QStringLiteral("D9:B3"), &r1, &c1, &r2, &c2));
        QCOMPARE(QList<int>({r1, c1, r2, c2}), QList<int>({2, 1, 8, 3}));
        QVERIFY(Formula::parseArea(QStringLiteral("B:D"), &r1, &c1, &r2, &c2));
        QCOMPARE(r2, Limits::Rows - 1);
        QCOMPARE(Formula::areaName(2, 1, 8, 3), QStringLiteral("B3:D9"));
        QCOMPARE(Formula::areaName(2, 1, 2, 1), QStringLiteral("B3"));
        QCOMPARE(Formula::quoteSheet(QStringLiteral("Datos")), QStringLiteral("Datos"));
        QCOMPARE(Formula::quoteSheet(QStringLiteral("Mi hoja")), QStringLiteral("'Mi hoja'"));
    }

    void printing_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("canonical");
        QTest::addColumn<QString>("spanish");

        QTest::newRow("spanish") << "SUMA(A1:A3;2,5)" << "SUM(A1:A3,2.5)" << "SUMA(A1:A3;2,5)";
        QTest::newRow("english") << "sum(a1:a3,2.5)" << "SUM(A1:A3,2.5)" << "SUMA(A1:A3;2,5)";
        QTest::newRow("absolute") << "$A$1+A$2*$B3" << "$A$1+A$2*$B3" << "$A$1+A$2*$B3";
        QTest::newRow("sheet") << "Datos!A1+'Mi hoja'!A1" << "Datos!A1+'Mi hoja'!A1" << "Datos!A1+'Mi hoja'!A1";
        QTest::newRow("if") << "SI(A1>=2;\"sí\";\"no\")" << "IF(A1>=2,\"sí\",\"no\")" << "SI(A1>=2;\"sí\";\"no\")";
        QTest::newRow("parens") << "(1+2)*-3%" << "(1+2)*-3%" << "(1+2)*-3%";
        QTest::newRow("string quote") << "\"a\"\"b\"&\"c\"" << "\"a\"\"b\"&\"c\"" << "\"a\"\"b\"&\"c\"";
        QTest::newRow("full column") << "SUM(A:A)+SUM(2:3)" << "SUM(A:A)+SUM(2:3)" << "SUMA(A:A)+SUMA(2:3)";
        QTest::newRow("bool") << "VERDADERO" << "TRUE" << "VERDADERO";
        QTest::newRow("missing arg") << "IF(A1,,2)" << "IF(A1,,2)" << "SI(A1;;2)";
        QTest::newRow("vlookup") << "BUSCARV(2;A1:C5;3;FALSO)" << "VLOOKUP(2,A1:C5,3,FALSE)" << "BUSCARV(2;A1:C5;3;FALSO)";
    }
    void printing()
    {
        QFETCH(QString, typed);
        QFETCH(QString, canonical);
        QFETCH(QString, spanish);
        QString error;
        const NodePtr node = Formula::parse(typed, &error);
        QVERIFY2(node, qPrintable(error));
        QCOMPARE(Formula::print(node, false), canonical);
        QCOMPARE(Formula::print(node, true), spanish);
        // What it prints, it reads back the same.
        QCOMPARE(Formula::print(Formula::parse(canonical), false), canonical);
        QCOMPARE(Formula::print(Formula::parse(spanish), false), canonical);
    }

    void parseErrors()
    {
        for (const char *bad : {"1+", "SUM(1", "(1+2", "1 2", "\"abc", "A1:", "SUM(1))", "*3", "IF(1;2,3;)x"}) {
            QString error;
            QVERIFY2(!Formula::parse(QString::fromUtf8(bad), &error), bad);
            QVERIFY2(!error.isEmpty(), bad);
        }
    }

    void evaluate_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<QString>("expected");
        const auto row = [](const char *formula, const char *expected) {
            QTest::newRow(formula) << QString::fromUtf8(formula) << QString::fromUtf8(expected);
        };

        // Operators
        row("1+2*3", "7");
        row("(1+2)*3", "9");
        row("2^3^2", "64");
        row("-2^2", "4");
        row("10/4", "2.5");
        row("1/0", "#DIV/0!");
        row("50%*200", "100");
        row("\"a\"&1&TRUE", "\"a1VERDADERO\"");
        row("\"3\"+4", "7");
        row("\"x\"+4", "#VALUE!");
        row("1=1", "TRUE");
        row("\"a\"=\"A\"", "TRUE");
        row("1<>2", "TRUE");
        row("2>=3", "FALSE");
        row("\"a\"<\"b\"", "TRUE");
        row("1<\"a\"", "TRUE");
        row("A1+A2", "3");
        row("B5+1", "1");
        row("B5&\"x\"", "\"x\"");
        row("NOEXISTE(1)", "#NAME?");
        row("Nada!A1", "#REF!");
        row("Datos!A1+Datos!A2", "15");
        row("'Mi hoja'!A1*2", "200");
        row("SUM(Datos!A1:A2)", "15");
        row("0,1+0,2=0,3", "TRUE");

        // Maths
        row("SUM(A1:A5)", "15");
        row("SUMA(A1:A5;10;\"5\")", "30");
        row("SUM(A1:B5)", "15");
        row("SUM(A:A)", "15");
        row("SUM(1:1)", "46048");
        row("AVERAGE(C1:C5)", "30");
        row("PROMEDIO(C1:C5)", "30");
        row("AVERAGE(B1:B4)", "#DIV/0!");
        row("MIN(C1:C5)", "10");
        row("MAX(C1:C5;99)", "99");
        row("COUNT(A1:D5)", "14");
        row("CONTAR(B1:B5)", "0");
        row("COUNTA(B1:B5)", "4");
        row("CONTARA(A1:B5)", "9");
        row("COUNTBLANK(B1:B5)", "1");
        row("ROUND(2,5;0)", "3");
        row("ROUND(-2,5;0)", "-3");
        row("ROUND(1,005;2)", "1.01");
        row("REDONDEAR(1234,567;-2)", "1200");
        row("ROUNDUP(1,21;1)", "1.3");
        row("ROUNDDOWN(-1,29;1)", "-1.2");
        row("ABS(-3)", "3");
        row("SQRT(16)", "4");
        row("RAIZ(16)", "4");
        row("SQRT(-1)", "#NUM!");
        row("POWER(2;10)", "1024");
        row("POTENCIA(2;10)", "1024");
        row("MOD(10;3)", "1");
        row("MOD(-1;3)", "2");
        row("RESIDUO(10;3)", "1");
        row("MOD(1;0)", "#DIV/0!");
        row("INT(-1,5)", "-2");
        row("SUM(1,5)", "6");
        row("SUM(1,5;2)", "3.5");
        row("ROUND(2,567,1)", "2.6");
        row("ENTERO(7,9)", "7");
        row("TRUNC(-1,59;1)", "-1.5");
        row("PRODUCT(A1:A5)", "120");
        row("PRODUCTO(2;3;4)", "24");
        row("SUMIF(B1:B5;\"manzana\";C1:C5)", "40");
        row("SUMAR.SI(C1:C5;\">20\")", "120");
        row("SUMIF(B1:B5;\"man*\";C1:C5)", "40");
        row("SUMIFS(C1:C5;B1:B5;\"manzana\";A1:A5;\">1\")", "30");
        row("COUNTIF(B1:B5;\"manzana\")", "2");
        row("CONTAR.SI(C1:C5;\">=30\")", "3");
        row("COUNTIF(B1:B5;\"<>manzana\")", "3");
        row("COUNTIFS(B1:B5;\"manzana\";C1:C5;\"<20\")", "1");
        row("AVERAGEIF(B1:B5;\"manzana\";C1:C5)", "20");
        row("PROMEDIO.SI(C1:C5;\">10\")", "35");
        row("AVERAGEIFS(C1:C5;B1:B5;\"manzana\";A1:A5;\">1\")", "30");
        row("MAXIFS(C1:C5;B1:B5;\"manzana\")", "30");
        row("MINIFS(C1:C5;B1:B5;\"manzana\")", "10");
        row("SUMPRODUCT(A1:A5;C1:C5)", "550");
        row("SUMAPRODUCTO((B1:B5=\"manzana\")*C1:C5)", "40");
        row("SUMPRODUCT(A1:A5;C1:C4)", "#VALUE!");
        row("SUMSQ(1;2;3)", "14");
        row("AND(RAND()>=0;RAND()<1)", "TRUE");
        row("RANDBETWEEN(5;5)", "5");
        row("PI()>3,14", "TRUE");
        row("EXP(0)", "1");
        row("LN(1)", "0");
        row("LN(0)", "#NUM!");
        row("LOG10(1000)", "3");
        row("LOG(8;2)", "3");
        row("ROUND(SIN(PI()/2);6)", "1");
        row("ROUND(COS(0);6)", "1");
        row("ROUND(DEGREES(PI());6)", "180");
        row("SIGN(-9)", "-1");
        row("CEILING(2,1;1)", "3");
        row("FLOOR(2,9;1)", "2");
        row("MROUND(7;5)", "5");
        row("EVEN(3)", "4");
        row("ODD(2)", "3");
        row("FACT(5)", "120");
        row("COMBIN(5;2)", "10");
        row("GCD(12;18)", "6");
        row("LCM(4;6)", "12");
        row("QUOTIENT(7;2)", "3");
        row("SUBTOTAL(9;C1:C5)", "150");
        row("SUBTOTAL(1;C1:C5)", "30");

        // Logic
        row("IF(A1=1;\"sí\";\"no\")", "\"sí\"");
        row("SI(A1>1;\"sí\";\"no\")", "\"no\"");
        row("IF(FALSE;1)", "FALSE");
        row("IF(TRUE;1;1/0)", "1");
        row("IF(\"x\";1;2)", "#VALUE!");
        row("AND(TRUE;1;A1)", "TRUE");
        row("Y(A1=1;A2=3)", "FALSE");
        row("OR(FALSE;0;A1=1)", "TRUE");
        row("O(FALSO;FALSO)", "FALSE");
        row("NOT(TRUE)", "FALSE");
        row("NO(0)", "TRUE");
        row("XOR(TRUE;TRUE;TRUE)", "TRUE");
        row("IFERROR(1/0;\"mal\")", "\"mal\"");
        row("SI.ERROR(5;\"mal\")", "5");
        row("IFNA(NA();\"nada\")", "\"nada\"");
        row("IFNA(1/0;\"nada\")", "#DIV/0!");
        row("IFS(A1>1;\"a\";A2>1;\"b\")", "\"b\"");
        row("IFS(A1>1;\"a\")", "#N/A");
        row("SWITCH(A2;1;\"uno\";2;\"dos\";\"otro\")", "\"dos\"");
        row("SWITCH(9;1;\"uno\";\"otro\")", "\"otro\"");
        row("SWITCH(9;1;\"uno\")", "#N/A");
        row("TRUE()", "TRUE");
        row("ISBLANK(B5)", "TRUE");
        row("ESBLANCO(B1)", "FALSE");
        row("ISNUMBER(A1)", "TRUE");
        row("ISTEXT(B1)", "TRUE");
        row("ISERROR(1/0)", "TRUE");
        row("ESERROR(1)", "FALSE");
        row("ISNA(NA())", "TRUE");
        row("ISERR(NA())", "FALSE");
        row("ISLOGICAL(TRUE)", "TRUE");
        row("ISEVEN(4)", "TRUE");
        row("ISODD(4)", "FALSE");
        row("N(TRUE)", "1");
        row("ROW()", "31");
        row("COLUMN()", "11");
        row("ROW(C7)", "7");
        row("FILA(C7)", "7");
        row("COLUMNA(C7)", "3");
        row("ROWS(A1:C5)", "5");
        row("COLUMNS(A1:C5)", "3");

        // Text
        row("CONCAT(\"a\";1;B1)", "\"a1manzana\"");
        row("CONCAT(B1:B2)", "\"manzanapera\"");
        row("CONCATENATE(\"a\";\"b\")", "\"ab\"");
        row("CONCATENAR(\"a\";\"b\")", "\"ab\"");
        row("LEFT(\"abcdef\";2)", "\"ab\"");
        row("IZQUIERDA(\"abcdef\")", "\"a\"");
        row("RIGHT(\"abcdef\";3)", "\"def\"");
        row("DERECHA(\"abcdef\";3)", "\"def\"");
        row("MID(\"abcdef\";2;3)", "\"bcd\"");
        row("EXTRAE(\"abcdef\";5;10)", "\"ef\"");
        row("LEN(\"año\")", "3");
        row("LARGO(B1)", "7");
        row("UPPER(\"año\")", "\"AÑO\"");
        row("MAYUSC(\"a\")", "\"A\"");
        row("LOWER(\"AÑO\")", "\"año\"");
        row("MINUSC(\"A\")", "\"a\"");
        row("PROPER(\"juan pérez\")", "\"Juan Pérez\"");
        row("TRIM(\"  a   b  \")", "\"a b\"");
        row("ESPACIOS(\" a \")", "\"a\"");
        row("TEXT(1234,5;\"#.##0,00\")", "\"1.234,50\"");
        row("TEXT(1234,5;\"#,##0.00\")", "\"1.234,50\"");
        row("TEXTO(0,25;\"0%\")", "\"25%\"");
        row("TEXT(D1;\"dd/mm/yyyy\")", "\"15/01/2026\"");
        row("VALUE(\"12,5\")", "12.5");
        row("VALOR(\"1.234,5\")", "1234.5");
        row("VALUE(\"abc\")", "#VALUE!");
        row("FIND(\"c\";\"abcabc\")", "3");
        row("ENCONTRAR(\"c\";\"abcabc\";4)", "6");
        row("FIND(\"z\";\"abc\")", "#VALUE!");
        row("FIND(\"B\";\"abc\")", "#VALUE!");
        row("SEARCH(\"B\";\"abc\")", "2");
        row("HALLAR(\"b?\";\"abc\")", "2");
        row("SUBSTITUTE(\"a-b-c\";\"-\";\"+\")", "\"a+b+c\"");
        row("SUSTITUIR(\"a-b-c\";\"-\";\"+\";2)", "\"a-b+c\"");
        row("REPLACE(\"abcdef\";2;3;\"X\")", "\"aXef\"");
        row("REEMPLAZAR(\"abcdef\";1;1;\"\")", "\"bcdef\"");
        row("TEXTJOIN(\", \";TRUE;B1:B5)", "\"manzana, pera, manzana, uva\"");
        row("UNIRCADENAS(\"-\";FALSO;\"a\";\"\";\"b\")", "\"a--b\"");
        row("REPT(\"ab\";3)", "\"ababab\"");
        row("REPETIR(\"x\";0)", "\"\"");
        row("EXACT(\"a\";\"A\")", "FALSE");
        row("IGUAL(\"a\";\"a\")", "TRUE");
        row("CHAR(65)", "\"A\"");
        row("CODE(\"A\")", "65");
        row("T(5)", "\"\"");
        row("FIXED(1234,567;1)", "\"1.234,6\"");
        row("DOLLAR(1234,5;2)", "\"1.234,50 €\"");
        row("MONEDA(2)", "\"2,00 €\"");

        // Dates
        row("TODAY()", "46336");
        row("HOY()", "46336");
        row("NOW()", "46336.5");
        row("AHORA()-HOY()", "0.5");
        row("DATE(2026;11;10)", "46336");
        row("FECHA(2026;13;1)", "46388");
        row("DATE(2026;3;0)", "46081");
        row("YEAR(D1)", "2026");
        row("AÑO(D1)", "2026");
        row("MONTH(D2)", "2");
        row("MES(D2)", "2");
        row("DAY(D3)", "25");
        row("DIA(D3)", "25");
        row("YEAR(\"10/11/2026\")", "2026");
        row("YEAR(\"hola\")", "#VALUE!");
        row("HOUR(0,75)", "18");
        row("HORA(0,75)", "18");
        row("MINUTE(TIME(5;42;7))", "42");
        row("SECOND(TIME(5;42;7))", "7");
        row("TIME(12;0;0)", "0.5");
        row("WEEKDAY(DATE(2026;11;10))", "3");
        row("DIASEM(DATE(2026;11;10);2)", "2");
        row("WEEKDAY(DATE(2026;11;15);2)", "7");
        row("WEEKNUM(DATE(2026;1;1))", "1");
        row("ISOWEEKNUM(DATE(2026;11;10))", "46");
        row("EDATE(DATE(2026;1;31);1)", "46081");
        row("FECHA.MES(DATE(2026;11;10);-11)", "46001");
        row("EOMONTH(DATE(2026;2;10);0)", "46081");
        row("FIN.MES(DATE(2026;11;10);1)", "46387");
        row("DATEDIF(DATE(2000;2;29);DATE(2026;11;10);\"Y\")", "26");
        row("SIFECHA(DATE(2026;1;15);DATE(2026;11;10);\"M\")", "9");
        row("DATEDIF(DATE(2026;1;15);DATE(2026;11;10);\"D\")", "299");
        row("DATEDIF(DATE(2026;1;15);DATE(2026;11;10);\"MD\")", "26");
        row("DATEDIF(DATE(2025;1;15);DATE(2026;11;10);\"YM\")", "9");
        row("DATEDIF(DATE(2026;11;10);DATE(2026;1;15);\"D\")", "#NUM!");
        row("DAYS(DATE(2026;11;10);DATE(2026;11;1))", "9");
        row("DIAS(D2;D1)", "36");
        row("NETWORKDAYS(DATE(2026;11;9);DATE(2026;11;15))", "5");
        row("DIAS.LAB(DATE(2026;11;9);DATE(2026;11;20);DATE(2026;11;11))", "9");
        row("NETWORKDAYS(DATE(2026;11;15);DATE(2026;11;9))", "-5");
        row("WORKDAY(DATE(2026;11;13);1)", "46342");
        row("DIA.LAB(DATE(2026;11;13);-5)", "46332");
        row("DATEVALUE(\"10/11/2026\")", "46336");
        row("TIMEVALUE(\"18:00\")", "0.75");
        row("D2-D1", "36");

        // Lookup
        row("VLOOKUP(3;A1:C5;3;FALSE)", "30");
        row("BUSCARV(3;A1:C5;2;FALSO)", "\"manzana\"");
        row("VLOOKUP(9;A1:C5;2;FALSE)", "#N/A");
        row("VLOOKUP(3,7;A1:C5;3)", "30");
        row("VLOOKUP(0;A1:C5;3)", "#N/A");
        row("VLOOKUP(3;A1:C5;4;FALSE)", "#REF!");
        row("VLOOKUP(\"PERA\";B1:C5;2;FALSE)", "20");
        row("VLOOKUP(\"u*\";B1:C5;2;FALSE)", "40");
        row("HLOOKUP(2;A2:C3;2;FALSE)", "3");
        row("BUSCARH(\"pera\";A2:C3;2;FALSO)", "\"manzana\"");
        row("INDEX(A1:C5;2;3)", "20");
        row("INDICE(B1:B5;4)", "\"uva\"");
        row("INDEX(A1:C5;9;1)", "#REF!");
        row("SUM(INDEX(A1:C5;0;3))", "150");
        row("SUM(INDEX(A1:C5;2;0))", "22");
        row("MATCH(\"uva\";B1:B5;0)", "4");
        row("COINCIDIR(30;C1:C5;0)", "3");
        row("MATCH(35;C1:C5;1)", "3");
        row("MATCH(35;C1:C5)", "3");
        row("MATCH(\"nada\";B1:B5;0)", "#N/A");
        row("INDEX(C1:C5;MATCH(\"pera\";B1:B5;0))", "20");
        row("XLOOKUP(\"uva\";B1:B5;C1:C5)", "40");
        row("BUSCARX(\"kiwi\";B1:B5;C1:C5;\"no hay\")", "\"no hay\"");
        row("XLOOKUP(\"kiwi\";B1:B5;C1:C5)", "#N/A");
        row("XLOOKUP(\"manzana\";B1:B5;C1:C5;;0;-1)", "30");
        row("XLOOKUP(35;C1:C5;B1:B5;;-1)", "\"manzana\"");
        row("XLOOKUP(35;C1:C5;B1:B5;;1)", "\"uva\"");
        row("LOOKUP(3,5;A1:A5;C1:C5)", "30");
        row("CHOOSE(2;\"a\";\"b\";\"c\")", "\"b\"");
        row("ELEGIR(4;\"a\";\"b\";\"c\")", "#VALUE!");
        row("SUM(OFFSET(A1;1;2;2;1))", "50");
        row("DESREF(A1;2;1)", "\"manzana\"");
        row("OFFSET(A1;-1;0)", "#REF!");
        row("INDIRECT(\"C\"&2)", "20");
        row("SUM(INDIRECTO(\"A1:A3\"))", "6");
        row("INDIRECT(\"Datos!A2\")", "8");
        row("INDIRECT(\"no vale\")", "#REF!");
        row("ADDRESS(3;2)", "\"$B$3\"");
        row("ADDRESS(3;2;4)", "\"B3\"");

        // Statistics
        row("MEDIAN(C1:C5)", "30");
        row("MEDIANA(1;2;3;4)", "2.5");
        row("ROUND(STDEV(C1:C5);4)", "15.8114");
        row("ROUND(DESVEST(C1:C5);4)", "15.8114");
        row("ROUND(STDEV.P(C1:C5);4)", "14.1421");
        row("STDEVP(1;1)", "0");
        row("VAR(C1:C5)", "250");
        row("VAR.P(C1:C5)", "200");
        row("STDEV(5)", "#DIV/0!");
        row("LARGE(C1:C5;2)", "40");
        row("K.ESIMO.MAYOR(C1:C5;1)", "50");
        row("SMALL(C1:C5;2)", "20");
        row("K.ESIMO.MENOR(C1:C5;9)", "#NUM!");
        row("RANK(40;C1:C5)", "2");
        row("JERARQUIA(40;C1:C5;1)", "4");
        row("RANK(41;C1:C5)", "#N/A");
        row("MODE(1;2;2;3)", "2");
        row("MODA(1;2;3)", "#N/A");
        row("PERCENTILE(C1:C5;0,5)", "30");
        row("PERCENTILE(C1:C5;0,9)", "46");
        row("QUARTILE(C1:C5;1)", "20");
        row("ROUND(CORREL(A1:A5;C1:C5);6)", "1");
        row("ROUND(SLOPE(C1:C5;A1:A5);6)", "10");
        row("ROUND(INTERCEPT(C1:C5;A1:A5);6)", "0");
        row("ROUND(FORECAST(6;C1:C5;A1:A5);6)", "60");
        row("ROUND(GEOMEAN(2;8);6)", "4");
        row("AVEDEV(1;3)", "1");
        row("COUNTBLANK(A1:A5)", "0");

        // Finance
        row("ROUND(PMT(0,05/12;120;10000);2)", "-106.07");
        row("ROUND(PAGO(0;10;1000);2)", "-100");
        row("ROUND(FV(0,05;10;-100);2)", "1257.79");
        row("ROUND(VF(0,05;10;0;-1000);2)", "1628.89");
        row("ROUND(PV(0,05;10;-100);2)", "772.17");
        row("ROUND(VA(0,05;10;0;-1000);2)", "613.91");
        row("ROUND(NPV(0,1;100;100;100);2)", "248.69");
        row("ROUND(VNA(0,1;A1:A5);4)", "10.6526");
        row("ROUND(NPER(0,05;-100;1000);4)", "14.2067");
        row("ROUND(RATE(10;-100;772,17);4)", "0.05");
        row("ROUND(IPMT(0,1;1;3;1000);2)", "-100");
        row("ROUND(PPMT(0,1;1;3;1000);2)", "-302.11");
        row("ROUND(SLN(1000;100;9);2)", "100");
    }
    void evaluate()
    {
        QFETCH(QString, formula);
        QFETCH(QString, expected);
        QCOMPARE(show(calc(formula)), expected);
    }

    void irr()
    {
        book.addSheet(QStringLiteral("Flujos"));
        const int s = book.sheetIndex(QStringLiteral("Flujos"));
        const QStringList flows = {QStringLiteral("-1000"), QStringLiteral("300"), QStringLiteral("400"), QStringLiteral("500"), QStringLiteral("200")};
        for (int i = 0; i < flows.size(); ++i) {
            book.setInput(s, i, 0, flows.at(i));
        }
        QCOMPARE(show(calc(QStringLiteral("ROUND(IRR(Flujos!A1:A5);4)"))), QStringLiteral("0.1532"));
        QCOMPARE(show(calc(QStringLiteral("ROUND(TIR(Flujos!A1:A5);4)"))), QStringLiteral("0.1532"));
        QCOMPARE(show(calc(QStringLiteral("IRR(Flujos!A2:A5)"))), QStringLiteral("#NUM!"));
        book.removeSheet(s);
    }

    void catalogue()
    {
        const QList<FunctionInfo> all = Formula::functions();
        QVERIFY2(all.size() >= 100, qPrintable(QString::number(all.size())));
        qInfo("functions: %d", int(all.size()));
        QSet<QString> seen;
        for (const FunctionInfo &info : all) {
            QVERIFY2(!seen.contains(info.name), qPrintable(info.name));
            seen.insert(info.name);
            QVERIFY2(!info.help.isEmpty(), qPrintable(info.name));
            QCOMPARE(Formula::canonicalFunction(info.name.toLower()), info.name);
            if (!info.spanish.isEmpty()) {
                QCOMPARE(Formula::canonicalFunction(info.spanish), info.name);
            }
        }
        QCOMPARE(Formula::canonicalFunction(QStringLiteral("suma")), QStringLiteral("SUM"));
        QCOMPARE(Formula::canonicalFunction(QStringLiteral("nada")), QString());
    }

    void shifting()
    {
        const NodePtr node = Formula::parse(QStringLiteral("A1+$A1+A$1+$A$1+SUM(B2:C3)"));
        QCOMPARE(Formula::print(Formula::shifted(node, 2, 1)), QStringLiteral("B3+$A3+B$1+$A$1+SUM(C4:D5)"));
        QCOMPARE(Formula::print(Formula::shifted(Formula::parse(QStringLiteral("A1+B2")), -1, 0)), QStringLiteral("#REF!+B1"));
        QCOMPARE(Formula::print(Formula::shifted(Formula::parse(QStringLiteral("SUM(A:A)+SUM(2:3)")), 1, 1)), QStringLiteral("SUM(B:B)+SUM(3:4)"));
    }

    void adjusting()
    {
        const auto adjust = [](const char *formula, SheetEdit::Kind kind, int at, int count, const char *home = "Hoja 1") {
            return Formula::print(Formula::adjusted(Formula::parse(QString::fromUtf8(formula)), SheetEdit{kind, QStringLiteral("Hoja 1"), at, count}, QString::fromUtf8(home)));
        };
        QCOMPARE(adjust("A1+A5+$A$9", SheetEdit::InsertRows, 2, 3), QStringLiteral("A1+A8+$A$12"));
        QCOMPARE(adjust("SUM(A1:A5)", SheetEdit::InsertRows, 2, 1), QStringLiteral("SUM(A1:A6)"));
        QCOMPARE(adjust("SUM(A1:A5)", SheetEdit::InsertRows, 5, 1), QStringLiteral("SUM(A1:A5)"));
        QCOMPARE(adjust("A1+A5", SheetEdit::DeleteRows, 1, 2), QStringLiteral("A1+A3"));
        QCOMPARE(adjust("A1+A2", SheetEdit::DeleteRows, 1, 1), QStringLiteral("A1+#REF!"));
        QCOMPARE(adjust("SUM(A1:A5)", SheetEdit::DeleteRows, 1, 2), QStringLiteral("SUM(A1:A3)"));
        QCOMPARE(adjust("SUM(A2:A3)", SheetEdit::DeleteRows, 1, 2), QStringLiteral("SUM(#REF!)"));
        QCOMPARE(adjust("SUM(A2:A9)", SheetEdit::DeleteRows, 0, 4), QStringLiteral("SUM(A1:A5)"));
        QCOMPARE(adjust("B1+D1", SheetEdit::InsertCols, 2, 1), QStringLiteral("B1+E1"));
        QCOMPARE(adjust("B1+D1", SheetEdit::DeleteCols, 0, 1), QStringLiteral("A1+C1"));
        QCOMPARE(adjust("SUM(B:D)", SheetEdit::InsertCols, 2, 2), QStringLiteral("SUM(B:F)"));
        // Another sheet's formula only follows where it names this one.
        QCOMPARE(adjust("A5+'Hoja 1'!A5", SheetEdit::InsertRows, 0, 1, "Datos"), QStringLiteral("A5+'Hoja 1'!A6"));
        QCOMPARE(adjust("A5+Datos!A5", SheetEdit::InsertRows, 0, 1), QStringLiteral("A6+Datos!A5"));
        QCOMPARE(Formula::print(Formula::sheetRenamed(Formula::parse(QStringLiteral("Datos!A1+A2")), QStringLiteral("Datos"), QStringLiteral("Mis datos"))),
                 QStringLiteral("'Mis datos'!A1+A2"));
    }

    void volatility()
    {
        QVERIFY(Formula::isVolatile(Formula::parse(QStringLiteral("1+NOW()"))));
        QVERIFY(Formula::isVolatile(Formula::parse(QStringLiteral("SUM(OFFSET(A1;1;1))"))));
        QVERIFY(!Formula::isVolatile(Formula::parse(QStringLiteral("SUM(A1:A3)"))));
    }
};

QTEST_GUILESS_MAIN(TestFormula)
#include "tst_formula.moc"
