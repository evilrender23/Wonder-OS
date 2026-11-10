#include "database.h"
#include "templates.h"

#include <QTemporaryDir>
#include <QTest>

class TestTemplates : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void every()
    {
        const QVariantList list = Templates::list();
        QCOMPARE(list.size(), 3);
        for (const QVariant &item : list) {
            const QString id = item.toMap().value(QStringLiteral("id")).toString();
            Database db;
            QVERIFY(db.create(m_dir.filePath(id + QStringLiteral(".db"))));
            QString error;
            QVERIFY2(Templates::apply(db, id, &error), qPrintable(error));
            QVERIFY(db.tableNames().size() >= 2);
            QVERIFY(!db.relations().isEmpty());
            QCOMPARE(db.objects(QStringLiteral("form")).size(), 1);
            QCOMPARE(db.objects(QStringLiteral("query")).size(), 1);
            // The query it comes with runs.
            const QString sql = db.objects(QStringLiteral("query")).first().toMap().value(QStringLiteral("definition")).toMap().value(QStringLiteral("sql")).toString();
            QVERIFY2(db.run(sql).value(QStringLiteral("ok")).toBool(), qPrintable(sql));
            QVERIFY(db.query(QStringLiteral("PRAGMA foreign_key_check")).rows.isEmpty());
            // A second time it fails, and leaves what there was.
            QVERIFY(!Templates::apply(db, id, &error));
            QVERIFY(!error.isEmpty());
        }
        Database db;
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("x.db"))));
        QString error;
        QVERIFY(!Templates::apply(db, QStringLiteral("nada"), &error));
        QVERIFY(db.tableNames().isEmpty());
    }

    void sample()
    {
        Database db;
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("muestra.db"))));
        QString error;
        QVERIFY2(Templates::sample(db, &error), qPrintable(error));
        QCOMPARE(db.tableNames(), (QStringList{QStringLiteral("clientes"), QStringLiteral("lineas_pedido"), QStringLiteral("pedidos"), QStringLiteral("productos")}));
        QVERIFY(db.isView(QStringLiteral("ventas_por_ciudad")));
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 60);
        QCOMPARE(db.rowCount(QStringLiteral("productos")), 40);
        QCOMPARE(db.rowCount(QStringLiteral("pedidos")), 240);
        QVERIFY(db.rowCount(QStringLiteral("lineas_pedido")) >= 240);
        QCOMPARE(db.relations().size(), 3);
        QVERIFY(db.query(QStringLiteral("PRAGMA foreign_key_check")).rows.isEmpty());
        // Every order's total is the sum of its lines.
        QCOMPARE(db.query(QStringLiteral("SELECT count(*) FROM pedidos p WHERE abs(total - (SELECT sum(cantidad * precio) FROM lineas_pedido WHERE pedido_id = p.id)) > 0.01")).rows.first().first().toInt(), 0);
        QCOMPARE(db.objects(QStringLiteral("query")).size(), 3);
        QCOMPARE(db.objects(QStringLiteral("form")).size(), 1);
        QCOMPARE(db.objects(QStringLiteral("report")).size(), 1);
        for (const QVariant &item : db.objects(QStringLiteral("query"))) {
            const QString sql = item.toMap().value(QStringLiteral("definition")).toMap().value(QStringLiteral("sql")).toString();
            const QVariantMap result = db.run(sql, {{QStringLiteral("ciudad"), QStringLiteral("Madrid")}});
            QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(sql + result.value(QStringLiteral("error")).toString()));
            QVERIFY(!result.value(QStringLiteral("rows")).toList().isEmpty());
        }
        // The same rows every time: the screenshots do not change.
        Database again;
        QVERIFY(again.create(m_dir.filePath(QStringLiteral("muestra2.db"))));
        QVERIFY(Templates::sample(again, &error));
        const QString digest = QStringLiteral("SELECT group_concat(nombre || ciudad || alta) FROM clientes");
        QCOMPARE(again.query(digest).rows, db.query(digest).rows);
        QCOMPARE(again.query(QStringLiteral("SELECT sum(total) FROM pedidos")).rows, db.query(QStringLiteral("SELECT sum(total) FROM pedidos")).rows);
    }
};

QTEST_GUILESS_MAIN(TestTemplates)
#include "tst_templates.moc"
