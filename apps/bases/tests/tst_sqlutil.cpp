#include "sqlutil.h"

#include <QTest>

class TestSqlUtil : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void quoting()
    {
        QCOMPARE(Sql::ident(QStringLiteral("nombre")), QStringLiteral("\"nombre\""));
        QCOMPARE(Sql::ident(QStringLiteral("a\"b")), QStringLiteral("\"a\"\"b\""));
        QCOMPARE(Sql::ident(QStringLiteral("x\"; DROP TABLE t; --")), QStringLiteral("\"x\"\"; DROP TABLE t; --\""));
        QCOMPARE(Sql::literal(QVariant()), QStringLiteral("NULL"));
        QCOMPARE(Sql::literal(42), QStringLiteral("42"));
        QCOMPARE(Sql::literal(1.5), QStringLiteral("1.5"));
        QCOMPARE(Sql::literal(2.0), QStringLiteral("2.0"));
        QCOMPARE(Sql::literal(0.1), QStringLiteral("0.1"));
        QCOMPARE(Sql::literal(QStringLiteral("O'Brien")), QStringLiteral("'O''Brien'"));
        QCOMPARE(Sql::literal(QByteArray("\x01\xff", 2)), QStringLiteral("X'01ff'"));
        QCOMPARE(Sql::literal(true), QStringLiteral("1"));
    }

    void types()
    {
        QCOMPARE(Sql::affinity(QStringLiteral("VARCHAR(40)")), QStringLiteral("TEXT"));
        QCOMPARE(Sql::affinity(QStringLiteral("BIGINT")), QStringLiteral("INTEGER"));
        QCOMPARE(Sql::affinity(QStringLiteral("DOUBLE")), QStringLiteral("REAL"));
        QCOMPARE(Sql::affinity(QString()), QStringLiteral("BLOB"));
        QCOMPARE(Sql::affinity(QStringLiteral("DECIMAL(10,2)")), QStringLiteral("NUMERIC"));
        QCOMPARE(Sql::kind(QStringLiteral("INTEGER")), QStringLiteral("integer"));
        QCOMPARE(Sql::kind(QStringLiteral("REAL")), QStringLiteral("real"));
        QCOMPARE(Sql::kind(QStringLiteral("DATE")), QStringLiteral("date"));
        QCOMPARE(Sql::kind(QStringLiteral("DATETIME")), QStringLiteral("datetime"));
        QCOMPARE(Sql::kind(QStringLiteral("BOOLEAN")), QStringLiteral("bool"));
        QCOMPARE(Sql::kind(QStringLiteral("TEXT")), QStringLiteral("text"));
        QCOMPARE(Sql::kind(QStringLiteral("BLOB")), QStringLiteral("blob"));
        QVERIFY(Sql::validType(QStringLiteral("VARCHAR(40)")));
        QVERIFY(Sql::validType(QStringLiteral("DECIMAL(10, 2)")));
        QVERIFY(Sql::validType(QStringLiteral("UNSIGNED BIG INT")));
        QVERIFY(Sql::validType(QString()));
        QVERIFY(!Sql::validType(QStringLiteral("TEXT); DROP TABLE x; --")));
        QVERIFY(!Sql::validType(QStringLiteral("TEXT NOT NULL")));
        QVERIFY(!Sql::validType(QStringLiteral("INT REFERENCES otra")));
        QVERIFY(!Sql::validType(QStringLiteral("INT, y TEXT")));
        QVERIFY(!Sql::validType(QStringLiteral("a\"b")));
    }

    void defaults()
    {
        QCOMPARE(Sql::defaultClause(QString()), QString());
        QCOMPARE(Sql::defaultClause(QStringLiteral("5")), QStringLiteral("5"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("-1.5")), QStringLiteral("-1.5"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("null")).toUpper(), QStringLiteral("NULL"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("CURRENT_DATE")), QStringLiteral("CURRENT_DATE"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("'hola'")), QStringLiteral("'hola'"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("pendiente")), QStringLiteral("'pendiente'"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("it's")), QStringLiteral("'it''s'"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("(1 + 2)")), QStringLiteral("(1 + 2)"));
        // What is not one balanced expression is text, never SQL.
        QCOMPARE(Sql::defaultClause(QStringLiteral("0); DROP TABLE t; --")), QStringLiteral("'0); DROP TABLE t; --'"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("(1)) , x TEXT, y INT DEFAULT ((2)")), QStringLiteral("'(1)) , x TEXT, y INT DEFAULT ((2)'"));
        QCOMPARE(Sql::defaultClause(QStringLiteral("'a' || 'b'")), QStringLiteral("'''a'' || ''b'''"));
    }

    void tokens()
    {
        const QString sql = QStringLiteral("SELECT nombre, count(*) FROM \"mi tabla\" WHERE x = 'a''b' AND y > 1.5 -- fin\n/* c */ AND z = :p");
        const QList<Sql::Token> tokens = Sql::tokenize(sql);
        QStringList seen;
        int covered = 0;
        for (const Sql::Token &token : tokens) {
            QCOMPARE(token.start, covered);
            covered += token.length;
            if (token.type != Sql::Token::Space) {
                seen.append(QString::number(token.type) + QLatin1Char(':') + sql.mid(token.start, token.length));
            }
        }
        QCOMPARE(covered, int(sql.size()));
        QVERIFY(seen.contains(QString::number(Sql::Token::Keyword) + QStringLiteral(":SELECT")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Identifier) + QStringLiteral(":nombre")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Function) + QStringLiteral(":count")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Identifier) + QStringLiteral(":\"mi tabla\"")));
        QVERIFY(seen.contains(QString::number(Sql::Token::String) + QStringLiteral(":'a''b'")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Number) + QStringLiteral(":1.5")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Comment) + QStringLiteral(":-- fin")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Comment) + QStringLiteral(":/* c */")));
        QVERIFY(seen.contains(QString::number(Sql::Token::Parameter) + QStringLiteral(":\x3ap")));
        // Unfinished quotes and comments end with the text instead of looping.
        QVERIFY(!Sql::tokenize(QStringLiteral("SELECT 'abc")).isEmpty());
        QVERIFY(!Sql::tokenize(QStringLiteral("SELECT /* abc")).isEmpty());
        QVERIFY(Sql::tokenize(QString()).isEmpty());
    }

    void splitting()
    {
        QCOMPARE(Sql::split(QStringLiteral("SELECT 1; SELECT 2;")), (QStringList{QStringLiteral("SELECT 1"), QStringLiteral("SELECT 2")}));
        QCOMPARE(Sql::split(QStringLiteral("SELECT ';' ; -- a; b\nSELECT 2")).size(), 2);
        QCOMPARE(Sql::split(QStringLiteral("  ;; ")).size(), 0);
        // A trigger's body holds semicolons of its own.
        const QString trigger = QStringLiteral("CREATE TRIGGER t AFTER INSERT ON a BEGIN UPDATE a SET x = 1; DELETE FROM b; END; SELECT 1");
        const QStringList parts = Sql::split(trigger);
        QCOMPARE(parts.size(), 2);
        QVERIFY(parts.first().endsWith(QStringLiteral("END")));
        QCOMPARE(Sql::verb(QStringLiteral("  -- x\n select 1")), QStringLiteral("SELECT"));
        QVERIFY(Sql::selects(QStringLiteral("with a as (select 1) select * from a")));
        QVERIFY(Sql::selects(QStringLiteral("VALUES (1)")));
        QVERIFY(!Sql::selects(QStringLiteral("DELETE FROM a")));
        QVERIFY(!Sql::selects(QStringLiteral("PRAGMA table_info(a)")));
        QCOMPARE(Sql::trimmed(QStringLiteral(" SELECT 1 ; -- fin\n")), QStringLiteral("SELECT 1"));
    }

    void parameters()
    {
        QCOMPARE(Sql::parameters(QStringLiteral("SELECT * FROM a WHERE x = :uno AND y = @dos AND z = :uno AND t = ':no' AND u = $tres")),
                 (QStringList{QStringLiteral(":uno"), QStringLiteral("@dos"), QStringLiteral("$tres")}));
        QVERIFY(Sql::parameters(QStringLiteral("SELECT 1 -- :x")).isEmpty());
    }

    void criteria_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("sql");
        QTest::addColumn<QVariantList>("binds");
        QTest::newRow("empty") << "  " << "" << QVariantList{};
        QTest::newRow("contains") << "ana" << "\"c\" LIKE ? ESCAPE '\\'" << QVariantList{QStringLiteral("%ana%")};
        QTest::newRow("wild") << "ana*" << "\"c\" LIKE ? ESCAPE '\\'" << QVariantList{QStringLiteral("ana%")};
        QTest::newRow("escaped") << "50%" << "\"c\" LIKE ? ESCAPE '\\'" << QVariantList{QStringLiteral("%50\\%%")};
        QTest::newRow("greater") << ">5" << "\"c\" > ?" << QVariantList{5};
        QTest::newRow("at most") << "<= 2.5" << "\"c\" <= ?" << QVariantList{2.5};
        QTest::newRow("not") << "!=x" << "\"c\" <> ?" << QVariantList{QStringLiteral("x")};
        QTest::newRow("exact") << "=Ana" << "\"c\" = ?" << QVariantList{QStringLiteral("Ana")};
        QTest::newRow("range") << "1..10" << "\"c\" BETWEEN ? AND ?" << QVariantList{1, 10};
        QTest::newRow("dates") << "2026-01-01..2026-12-31" << "\"c\" BETWEEN ? AND ?" << QVariantList{QStringLiteral("2026-01-01"), QStringLiteral("2026-12-31")};
        QTest::newRow("null") << "null" << "\"c\" IS NULL" << QVariantList{};
        QTest::newRow("not null") << "no null" << "\"c\" IS NOT NULL" << QVariantList{};
        QTest::newRow("attack") << "'; DROP TABLE t; --" << "\"c\" LIKE ? ESCAPE '\\'" << QVariantList{QStringLiteral("%'; DROP TABLE t; --%")};
    }
    void criteria()
    {
        QFETCH(QString, typed);
        QFETCH(QString, sql);
        QFETCH(QVariantList, binds);
        QVariantList got;
        QCOMPARE(Sql::criterion(QStringLiteral("\"c\""), typed, &got), sql);
        QCOMPARE(got.size(), binds.size());
        for (int i = 0; i < binds.size(); ++i) {
            QCOMPARE(got.at(i).toString(), binds.at(i).toString());
        }
    }

    void criteriaInline()
    {
        QCOMPARE(Sql::criterionInline(QStringLiteral("\"c\""), QStringLiteral(">5")), QStringLiteral("\"c\" > 5"));
        QCOMPARE(Sql::criterionInline(QStringLiteral("\"c\""), QStringLiteral("=O'Brien")), QStringLiteral("\"c\" = 'O''Brien'"));
        QCOMPARE(Sql::criterionInline(QStringLiteral("\"c?\""), QStringLiteral("1..2")), QStringLiteral("\"c?\" BETWEEN 1 AND 2"));
        QCOMPARE(Sql::criterionInline(QStringLiteral("\"c\""), QStringLiteral("a?b")), QStringLiteral("\"c\" LIKE 'a_b' ESCAPE '\\'"));
    }

    void conditions()
    {
        QVariantList binds;
        QCOMPARE(Sql::condition(QStringLiteral("\"a\""), QStringLiteral("eq"), 3, &binds), QStringLiteral("\"a\" = ?"));
        QCOMPARE(binds, QVariantList({3}));
        binds.clear();
        QCOMPARE(Sql::condition(QStringLiteral("\"a\""), QStringLiteral("contains"), QStringLiteral("50%_\\"), &binds), QStringLiteral("\"a\" LIKE ? ESCAPE '\\'"));
        QCOMPARE(binds, QVariantList({QStringLiteral("%50\\%\\_\\\\%")}));
        binds.clear();
        QCOMPARE(Sql::condition(QStringLiteral("\"a\""), QStringLiteral("null"), 1, &binds), QStringLiteral("\"a\" IS NULL"));
        QVERIFY(binds.isEmpty());
        QVERIFY(Sql::condition(QStringLiteral("\"a\""), QStringLiteral("; DROP TABLE x"), 1, &binds).isEmpty());
        QVERIFY(binds.isEmpty());
        for (const QVariant &op : Sql::operators()) {
            QVariantList each;
            QVERIFY(!Sql::condition(QStringLiteral("\"a\""), op.toMap().value(QStringLiteral("id")).toString(), 1, &each).isEmpty());
            QCOMPARE(each.isEmpty(), op.toMap().value(QStringLiteral("unary")).toBool());
        }
    }

    void inlining()
    {
        QCOMPARE(Sql::inlined(QStringLiteral("\"¿qué?\" = ? AND b LIKE ? ESCAPE '?' AND c = ?"), {QStringLiteral("O'Brien"), QStringLiteral("%?%"), 7}),
                 QStringLiteral("\"¿qué?\" = 'O''Brien' AND b LIKE '%?%' ESCAPE '?' AND c = 7"));
        // More marks than values: the rest stay.
        QCOMPARE(Sql::inlined(QStringLiteral("a = ? AND b = ?"), {1}), QStringLiteral("a = 1 AND b = ?"));
    }

    void completion()
    {
        const QVariantMap schema{{QStringLiteral("clientes"), QStringList{QStringLiteral("id"), QStringLiteral("nombre"), QStringLiteral("ciudad")}},
                                 {QStringLiteral("pedidos"), QStringList{QStringLiteral("id"), QStringLiteral("cliente_id"), QStringLiteral("total")}}};
        const auto texts = [](const QVariantMap &result) {
            QStringList list;
            for (const QVariant &item : result.value(QStringLiteral("items")).toList()) {
                list.append(item.toMap().value(QStringLiteral("text")).toString());
            }
            return list;
        };
        QString sql = QStringLiteral("SELECT * FROM cl");
        QVariantMap result = Sql::complete(sql, int(sql.size()), schema);
        QCOMPARE(result.value(QStringLiteral("prefix")).toString(), QStringLiteral("cl"));
        QCOMPARE(result.value(QStringLiteral("start")).toInt(), int(sql.size()) - 2);
        QVERIFY(texts(result).contains(QStringLiteral("clientes")));
        QVERIFY(!texts(result).contains(QStringLiteral("pedidos")));

        sql = QStringLiteral("SELECT clientes.n");
        result = Sql::complete(sql, int(sql.size()), schema);
        QCOMPARE(texts(result), QStringList{QStringLiteral("nombre")});

        // An alias stands for its table.
        sql = QStringLiteral("SELECT p. FROM pedidos p");
        result = Sql::complete(sql, 9, schema);
        QCOMPARE(texts(result), (QStringList{QStringLiteral("id"), QStringLiteral("cliente_id"), QStringLiteral("total")}));

        sql = QStringLiteral("SEL");
        QVERIFY(texts(Sql::complete(sql, 3, schema)).contains(QStringLiteral("SELECT")));
        sql = QStringLiteral("SELECT to FROM pedidos");
        QVERIFY(texts(Sql::complete(sql, 9, schema)).contains(QStringLiteral("total")));
        // Nothing to offer inside a string.
        sql = QStringLiteral("SELECT 'cl");
        QVERIFY(texts(Sql::complete(sql, int(sql.size()), schema)).isEmpty());
    }

    void pretty()
    {
        QCOMPARE(Sql::pretty(QStringLiteral("fecha_alta")), QStringLiteral("Fecha alta"));
        QCOMPARE(Sql::pretty(QStringLiteral("id")), QStringLiteral("Id"));
        QCOMPARE(Sql::pretty(QString()), QString());
    }
};

QTEST_GUILESS_MAIN(TestSqlUtil)
#include "tst_sqlutil.moc"
