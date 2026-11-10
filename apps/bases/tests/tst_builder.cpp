#include "builder.h"
#include "database.h"

#include <QTemporaryDir>
#include <QTest>

class TestBuilder : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Database m_db;

    static QVariantMap column(const QString &table, const QString &name, const QVariantMap &more = {})
    {
        QVariantMap map{{QStringLiteral("table"), table}, {QStringLiteral("column"), name}, {QStringLiteral("show"), true}};
        map.insert(more);
        return map;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_db.create(m_dir.filePath(QStringLiteral("b.db"))));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE clientes (id INTEGER PRIMARY KEY, nombre TEXT, ciudad TEXT)")));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE pedidos (id INTEGER PRIMARY KEY, cliente_id INTEGER REFERENCES clientes (id), total REAL, fecha DATE)")));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE lineas (id INTEGER PRIMARY KEY, pedido_id INTEGER REFERENCES pedidos (id), cantidad INTEGER)")));
        QVERIFY(m_db.exec(QStringLiteral("CREATE TABLE suelta (x TEXT)")));
        QVERIFY(m_db.exec(QStringLiteral("INSERT INTO clientes VALUES (1, 'Ana', 'Madrid'), (2, 'Luis', 'Vigo'), (3, 'O''Brien', 'Madrid')")));
        QVERIFY(m_db.exec(QStringLiteral("INSERT INTO pedidos VALUES (1, 1, 50, '2026-01-02'), (2, 1, 150, '2026-02-03'), (3, 2, 300, '2026-03-04')")));
        QVERIFY(m_db.exec(QStringLiteral("INSERT INTO lineas VALUES (1, 1, 2), (2, 2, 1), (3, 3, 5)")));
        m_db.touch(true);
    }

    void oneTable()
    {
        QCOMPARE(Builder::sql(m_db, {}), QString());
        QCOMPARE(Builder::sql(m_db, {{QStringLiteral("tables"), QStringList{QStringLiteral("no_existe")}}}), QString());
        QCOMPARE(Builder::sql(m_db, {{QStringLiteral("tables"), QStringList{QStringLiteral("clientes")}}}), QStringLiteral("SELECT *\nFROM \"clientes\""));
        const QVariantMap design{
            {QStringLiteral("tables"), QStringList{QStringLiteral("clientes")}},
            {QStringLiteral("columns"), QVariantList{column(QStringLiteral("clientes"), QStringLiteral("nombre"), {{QStringLiteral("sort"), QStringLiteral("desc")}, {QStringLiteral("alias"), QStringLiteral("quién")}}),
                                                     column(QStringLiteral("clientes"), QStringLiteral("ciudad"), {{QStringLiteral("criterion"), QStringLiteral("=Madrid")}, {QStringLiteral("show"), false}}),
                                                     column(QStringLiteral("clientes"), QStringLiteral("no_existe"))}},
            {QStringLiteral("distinct"), true},
            {QStringLiteral("limit"), 10},
        };
        const QString sql = Builder::sql(m_db, design);
        QCOMPARE(sql, QStringLiteral("SELECT DISTINCT \"nombre\" AS \"quién\"\nFROM \"clientes\"\nWHERE \"ciudad\" = 'Madrid'\nORDER BY \"nombre\" DESC\nLIMIT 10"));
        const Result result = m_db.query(sql);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rows.size(), 2);
        QCOMPARE(result.rows.first().first().toString(), QStringLiteral("O'Brien"));
    }

    void criteriaCannotInject()
    {
        const QVariantMap design{
            {QStringLiteral("tables"), QStringList{QStringLiteral("clientes")}},
            {QStringLiteral("columns"), QVariantList{column(QStringLiteral("clientes"), QStringLiteral("nombre"), {{QStringLiteral("criterion"), QStringLiteral("=x'; DROP TABLE clientes; --")}, {QStringLiteral("alias"), QStringLiteral("a\" FROM clientes; DROP TABLE pedidos; --")}})}},
        };
        const QVariantMap result = m_db.run(Builder::sql(m_db, design));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("statements")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 0);
        QVERIFY(m_db.exists(QStringLiteral("clientes")));
        QVERIFY(m_db.exists(QStringLiteral("pedidos")));
    }

    void joins()
    {
        const QVariantList joins = Builder::joins(m_db, {QStringLiteral("clientes"), QStringLiteral("pedidos"), QStringLiteral("lineas"), QStringLiteral("suelta")});
        QCOMPARE(joins.size(), 2);
        QCOMPARE(joins.at(0).toMap().value(QStringLiteral("table")).toString(), QStringLiteral("pedidos"));
        QCOMPARE(joins.at(1).toMap().value(QStringLiteral("table")).toString(), QStringLiteral("lineas"));

        // Whichever side is named first, the key joins them.
        for (const QStringList &tables : {QStringList{QStringLiteral("clientes"), QStringLiteral("pedidos")}, QStringList{QStringLiteral("pedidos"), QStringLiteral("clientes")}}) {
            const QVariantMap design{{QStringLiteral("tables"), tables},
                                     {QStringLiteral("columns"), QVariantList{column(QStringLiteral("clientes"), QStringLiteral("nombre")), column(QStringLiteral("pedidos"), QStringLiteral("total"))}}};
            const QString sql = Builder::sql(m_db, design);
            QVERIFY2(sql.contains(QStringLiteral("\nJOIN ")), qPrintable(sql));
            const Result result = m_db.query(sql);
            QVERIFY2(result.ok, qPrintable(result.error));
            QCOMPARE(result.rows.size(), 3);
        }
        const QString crossed = Builder::sql(m_db, {{QStringLiteral("tables"), QStringList{QStringLiteral("clientes"), QStringLiteral("suelta")}}});
        QVERIFY(crossed.contains(QStringLiteral("CROSS JOIN \"suelta\"")));
        QVERIFY(m_db.query(crossed).ok);
        // Through a table in the middle.
        const QString chain = Builder::sql(m_db, {{QStringLiteral("tables"), QStringList{QStringLiteral("lineas"), QStringLiteral("pedidos"), QStringLiteral("clientes")}}});
        QCOMPARE(chain.count(QStringLiteral("\nJOIN ")), 2);
        QCOMPARE(m_db.query(chain).rows.size(), 3);
    }

    void grouping()
    {
        const QVariantMap design{
            {QStringLiteral("tables"), QStringList{QStringLiteral("clientes"), QStringLiteral("pedidos")}},
            {QStringLiteral("columns"), QVariantList{column(QStringLiteral("clientes"), QStringLiteral("nombre"), {{QStringLiteral("total"), QStringLiteral("group")}, {QStringLiteral("sort"), QStringLiteral("asc")}}),
                                                     column(QStringLiteral("pedidos"), QStringLiteral("total"), {{QStringLiteral("total"), QStringLiteral("sum")}, {QStringLiteral("criterion"), QStringLiteral(">100")}}),
                                                     column(QStringLiteral("pedidos"), QStringLiteral("id"), {{QStringLiteral("total"), QStringLiteral("count")}, {QStringLiteral("alias"), QStringLiteral("n")}})}},
        };
        const QString sql = Builder::sql(m_db, design);
        QVERIFY(sql.contains(QStringLiteral("GROUP BY \"clientes\".\"nombre\"")));
        QVERIFY(sql.contains(QStringLiteral("HAVING SUM(\"pedidos\".\"total\") > 100")));
        const Result result = m_db.query(sql);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.columns, (QStringList{QStringLiteral("nombre"), QStringLiteral("sum_total"), QStringLiteral("n")}));
        QCOMPARE(result.rows.size(), 2);
        QCOMPARE(result.rows.at(0).at(1).toDouble(), 200.0);
        QCOMPARE(result.rows.at(0).at(2).toInt(), 2);
    }

    void defaults()
    {
        const QVariantMap form = Builder::defaultForm(m_db, QStringLiteral("clientes"));
        QCOMPARE(form.value(QStringLiteral("fields")).toList().size(), 3);
        QCOMPARE(form.value(QStringLiteral("fields")).toList().at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Nombre"));
        const QVariantList subforms = form.value(QStringLiteral("subforms")).toList();
        QCOMPARE(subforms.size(), 1);
        QCOMPARE(subforms.first().toMap().value(QStringLiteral("table")).toString(), QStringLiteral("pedidos"));
        QCOMPARE(subforms.first().toMap().value(QStringLiteral("column")).toString(), QStringLiteral("cliente_id"));
        QVERIFY(Builder::defaultForm(m_db, QStringLiteral("lineas")).value(QStringLiteral("subforms")).toList().isEmpty());

        QVariantMap report = Builder::defaultReport(m_db, QStringLiteral("pedidos"));
        QCOMPARE(report.value(QStringLiteral("source")).toString(), QStringLiteral("pedidos"));
        QCOMPARE(report.value(QStringLiteral("columns")).toList().size(), 4);
        QCOMPARE(report.value(QStringLiteral("columns")).toList().at(2).toMap().value(QStringLiteral("total")).toString(), QStringLiteral("sum"));
        report = Builder::defaultReport(m_db, QStringLiteral("SELECT nombre, ciudad FROM clientes;"));
        QCOMPARE(report.value(QStringLiteral("source")).toString(), QString());
        QCOMPARE(report.value(QStringLiteral("sql")).toString(), QStringLiteral("SELECT nombre, ciudad FROM clientes"));
        QCOMPARE(report.value(QStringLiteral("columns")).toList().size(), 2);
        QVERIFY(Builder::defaultReport(m_db, QStringLiteral("DROP TABLE clientes")).value(QStringLiteral("columns")).toList().isEmpty());
        QVERIFY(m_db.exists(QStringLiteral("clientes")));
    }
};

QTEST_GUILESS_MAIN(TestBuilder)
#include "tst_builder.moc"
