#include "numfmt.h"

#include <QtTest>

class TestNumFmt : public QObject
{
    Q_OBJECT
private slots:
    void format_data()
    {
        QTest::addColumn<double>("number");
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("shown");

        QTest::newRow("general int") << 42.0 << "" << "42";
        QTest::newRow("general dec") << 3.5 << "General" << "3,5";
        QTest::newRow("general float noise") << 0.1 + 0.2 << "" << "0,3";
        QTest::newRow("general negative") << -1234.5 << "" << "-1234,5";
        QTest::newRow("0") << 2.5 << "0" << "3";
        QTest::newRow("0.00") << 3.14159 << "0.00" << "3,14";
        QTest::newRow("thousands") << 1234567.891 << "#,##0.00" << "1.234.567,89";
        QTest::newRow("thousands int") << 1234567.0 << "#,##0" << "1.234.567";
        QTest::newRow("small thousands") << 12.0 << "#,##0" << "12";
        QTest::newRow("percent") << 0.125 << "0%" << "13%";
        QTest::newRow("percent dec") << 0.125 << "0.0%" << "12,5%";
        QTest::newRow("euro") << 1234.5 << "#,##0.00 €" << "1.234,50 €";
        QTest::newRow("euro quoted") << -5.0 << "#,##0.00 \"€\"" << "-5,00 €";
        QTest::newRow("scientific") << 123456.0 << "0.00E+00" << "1,23E+05";
        QTest::newRow("date") << 46336.0 << "dd/mm/yyyy" << "10/11/2026";
        QTest::newRow("date short") << 46336.0 << "d/m/yy" << "10/11/26";
        QTest::newRow("iso date") << 46336.0 << "yyyy-mm-dd" << "2026-11-10";
        QTest::newRow("time") << 0.75 << "hh:mm" << "18:00";
        QTest::newRow("time secs") << 0.5 + 61.0 / 86400 << "hh:mm:ss" << "12:01:01";
        QTest::newRow("date time") << 46336.25 << "dd/mm/yyyy hh:mm" << "10/11/2026 06:00";
        QTest::newRow("two sections") << -7.0 << "0.0;(0.0)" << "(7,0)";
        QTest::newRow("text code") << 7.0 << "@" << "7";
    }
    void format()
    {
        QFETCH(double, number);
        QFETCH(QString, code);
        QFETCH(QString, shown);
        QCOMPARE(NumFmt::format(Value(number), code), shown);
    }

    void formatOthers()
    {
        QCOMPARE(NumFmt::format(Value(QStringLiteral("hola")), QStringLiteral("0.00")), QStringLiteral("hola"));
        QCOMPARE(NumFmt::format(Value::boolean(true), QString()), QStringLiteral("VERDADERO"));
        QCOMPARE(NumFmt::format(Value(Err::Div0), QString()), QStringLiteral("#DIV/0!"));
        QCOMPARE(NumFmt::format(Value(), QStringLiteral("0.00")), QString());
        QString color;
        QCOMPARE(NumFmt::format(Value(-3.0), QStringLiteral("0;[Red]-0"), &color), QStringLiteral("-3"));
        QVERIFY(color.startsWith(QLatin1Char('#')));
    }

    void serials()
    {
        QCOMPARE(NumFmt::serial(QDate(1900, 3, 1)), 61.0);
        QCOMPARE(NumFmt::serial(QDate(2026, 11, 10)), 46336.0);
        QCOMPARE(NumFmt::date(46336.7), QDate(2026, 11, 10));
        QCOMPARE(NumFmt::dateTime(46336.5).time(), QTime(12, 0));
    }

    void parseInput_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<Value>("value");
        QTest::addColumn<QString>("code");

        QTest::newRow("int") << "42" << Value(42.0) << "";
        QTest::newRow("spanish decimal") << "3,5" << Value(3.5) << "";
        QTest::newRow("english decimal") << "3.5" << Value(3.5) << "";
        QTest::newRow("spanish thousands") << "1.234.567,5" << Value(1234567.5) << "";
        QTest::newRow("negative") << "-12,25" << Value(-12.25) << "";
        QTest::newRow("exponent") << "1e3" << Value(1000.0) << "";
        QTest::newRow("percent") << "12,5%" << Value(0.125) << "0.00%";
        QTest::newRow("euro") << "12,50 €" << Value(12.5) << "#,##0.00\\ \"€\"";
        QTest::newRow("date") << "10/11/2026" << Value(46336.0) << "dd/mm/yyyy";
        QTest::newRow("iso date") << "2026-11-10" << Value(46336.0) << "dd/mm/yyyy";
        QTest::newRow("time") << "18:00" << Value(0.75) << "hh:mm";
        QTest::newRow("true") << "verdadero" << Value::boolean(true) << "";
        QTest::newRow("false") << "FALSE" << Value::boolean(false) << "";
        QTest::newRow("error") << "#N/A" << Value(Err::NA) << "";
        QTest::newRow("text") << "hola mundo" << Value(QStringLiteral("hola mundo")) << "";
        QTest::newRow("not a number") << "12 manzanas" << Value(QStringLiteral("12 manzanas")) << "";
        QTest::newRow("phone-like") << "1.2.3" << Value(QStringLiteral("1.2.3")) << "";
    }
    void parseInput()
    {
        QFETCH(QString, typed);
        QFETCH(Value, value);
        QFETCH(QString, code);
        QString got;
        const Value parsed = NumFmt::parseInput(typed, &got);
        QCOMPARE(parsed.type, value.type);
        if (value.isNumber()) {
            QVERIFY2(qAbs(parsed.num - value.num) < 1e-9, qPrintable(QString::number(parsed.num, 'g', 15)));
        } else {
            QVERIFY(parsed == value);
        }
        QCOMPARE(got, code);
    }

    void editText()
    {
        QCOMPARE(NumFmt::editText(Value(46336.0), QStringLiteral("dd/mm/yyyy")), QStringLiteral("10/11/2026"));
        QCOMPARE(NumFmt::editText(Value(0.125), QStringLiteral("0%")), QStringLiteral("12,5%"));
        QCOMPARE(NumFmt::editText(Value(1234.5), QStringLiteral("#,##0.00 €")), QStringLiteral("1234,5"));
        QCOMPARE(NumFmt::editText(Value(1.0 / 3), QString()), QStringLiteral("0,333333333333333"));
    }

    void families()
    {
        QCOMPARE(NumFmt::family(QString()), QStringLiteral("general"));
        QCOMPARE(NumFmt::family(QStringLiteral("#,##0.00")), QStringLiteral("number"));
        QCOMPARE(NumFmt::family(QStringLiteral("#,##0.00 €")), QStringLiteral("currency"));
        QCOMPARE(NumFmt::family(QStringLiteral("0.0%")), QStringLiteral("percent"));
        QCOMPARE(NumFmt::format(Value(1234.5), NumFmt::familyCode(QStringLiteral("currency"))), QStringLiteral("1.234,50 €"));
        QCOMPARE(NumFmt::family(QStringLiteral("dd/mm/yyyy")), QStringLiteral("date"));
        QCOMPARE(NumFmt::family(QStringLiteral("hh:mm")), QStringLiteral("time"));
        QCOMPARE(NumFmt::family(QStringLiteral("0.00E+00")), QStringLiteral("scientific"));
        QCOMPARE(NumFmt::family(QStringLiteral("@")), QStringLiteral("text"));
        for (const char *family : {"number", "currency", "percent", "date", "time", "scientific", "text"}) {
            QCOMPARE(NumFmt::family(NumFmt::familyCode(QString::fromLatin1(family))), QString::fromLatin1(family));
        }
        QCOMPARE(NumFmt::decimals(QStringLiteral("#,##0.00")), 2);
        QCOMPARE(NumFmt::withDecimals(QStringLiteral("#,##0.00"), 1), QStringLiteral("#,##0.000"));
        QCOMPARE(NumFmt::withDecimals(QStringLiteral("0.0%"), -1), QStringLiteral("0%"));
        QCOMPARE(NumFmt::withDecimals(QString(), 1), QStringLiteral("0.0"));
    }

    void builtins()
    {
        QCOMPARE(NumFmt::builtin(0), QStringLiteral("General"));
        QCOMPARE(NumFmt::builtin(77), QString());
        QCOMPARE(NumFmt::builtin(2), QStringLiteral("0.00"));
        QCOMPARE(NumFmt::builtin(9), QStringLiteral("0%"));
        QVERIFY(NumFmt::isDate(NumFmt::builtin(14)));
        QCOMPARE(NumFmt::builtinId(QStringLiteral("0.00")), 2);
        QCOMPARE(NumFmt::builtinId(QStringLiteral("0.000000")), -1);
        QVERIFY(NumFmt::isTime(QStringLiteral("hh:mm")));
        QVERIFY(!NumFmt::isTime(QStringLiteral("dd/mm/yyyy hh:mm")));
        QVERIFY(!NumFmt::isDate(QStringLiteral("0.00 \"d\"")));
    }
};

QTEST_GUILESS_MAIN(TestNumFmt)
#include "tst_numfmt.moc"
