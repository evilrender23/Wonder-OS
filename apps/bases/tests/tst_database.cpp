#include "database.h"
#include "sqlutil.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestDatabase : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_next = 0;

    QString fresh()
    {
        return m_dir.filePath(QStringLiteral("db%1.sqlite").arg(++m_next));
    }

    static void shop(Database &db)
    {
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE clientes (id INTEGER PRIMARY KEY AUTOINCREMENT, nombre TEXT NOT NULL, ciudad TEXT DEFAULT 'Madrid', alta DATE, activo BOOLEAN DEFAULT 1, saldo REAL)")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE pedidos (id INTEGER PRIMARY KEY, cliente_id INTEGER NOT NULL REFERENCES clientes (id) ON DELETE CASCADE, total REAL, nota VARCHAR(40) UNIQUE)")));
        QVERIFY(db.exec(QStringLiteral("CREATE INDEX idx_ciudad ON clientes (ciudad)")));
        QVERIFY(db.exec(QStringLiteral("CREATE VIEW grandes AS SELECT * FROM pedidos WHERE total > 100")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO clientes (nombre, ciudad) VALUES ('Ana', 'Madrid'), ('Luis', 'Vigo'), ('Marta', NULL)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO pedidos (cliente_id, total) VALUES (1, 50), (1, 150), (2, 300)")));
        db.touch(true);
    }

private Q_SLOTS:
    void creatingAndOpening()
    {
        Database db;
        QVERIFY(!db.isOpen());
        QVERIFY(!db.open(m_dir.filePath(QStringLiteral("nope.db"))));
        QVERIFY(!db.lastError().isEmpty());
        const QString path = fresh();
        QVERIFY(db.create(path));
        QVERIFY(db.isOpen());
        QCOMPARE(db.path(), path);
        QVERIFY(db.tables().isEmpty());
        shop(db);
        db.close();
        QVERIFY(!db.isOpen());
        QVERIFY(db.open(path));
        QCOMPARE(db.tableNames(), (QStringList{QStringLiteral("clientes"), QStringLiteral("pedidos")}));

        // A file that is not a database is refused, and left as it was.
        const QString text = m_dir.filePath(QStringLiteral("texto.db"));
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("esto no es una base de datos, solo un texto largo para pasar de cien bytes de cabecera y algo más todavía, por si acaso.");
        file.close();
        Database other;
        QVERIFY(!other.open(text));
        QVERIFY(!other.isOpen());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("esto no es"));
    }

    void schema()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QCOMPARE(db.tables().size(), 3);
        QCOMPARE(db.tableNames(true).size(), 3);
        QVERIFY(db.exists(QStringLiteral("CLIENTES")));
        QVERIFY(!db.exists(QStringLiteral("nada")));
        QVERIFY(db.isView(QStringLiteral("grandes")));
        QVERIFY(!db.isView(QStringLiteral("pedidos")));

        const QVariantList columns = db.columns(QStringLiteral("clientes"));
        QCOMPARE(columns.size(), 6);
        const QVariantMap id = columns.at(0).toMap();
        QCOMPARE(id.value(QStringLiteral("name")).toString(), QStringLiteral("id"));
        QCOMPARE(id.value(QStringLiteral("pk")).toInt(), 1);
        QVERIFY(id.value(QStringLiteral("autoIncrement")).toBool());
        QCOMPARE(id.value(QStringLiteral("kind")).toString(), QStringLiteral("integer"));
        const QVariantMap name = columns.at(1).toMap();
        QVERIFY(name.value(QStringLiteral("notNull")).toBool());
        QCOMPARE(columns.at(2).toMap().value(QStringLiteral("defaultValue")).toString(), QStringLiteral("'Madrid'"));
        QCOMPARE(columns.at(3).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("date"));
        QCOMPARE(columns.at(4).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("bool"));

        const QVariantList orders = db.columns(QStringLiteral("pedidos"));
        const QVariantMap customer = orders.at(1).toMap();
        QCOMPARE(customer.value(QStringLiteral("fkTable")).toString(), QStringLiteral("clientes"));
        QCOMPARE(customer.value(QStringLiteral("fkColumn")).toString(), QStringLiteral("id"));
        QCOMPARE(customer.value(QStringLiteral("fkOnDelete")).toString(), QStringLiteral("CASCADE"));
        QVERIFY(!orders.at(0).toMap().value(QStringLiteral("autoIncrement")).toBool());
        QVERIFY(orders.at(3).toMap().value(QStringLiteral("unique")).toBool());
        QCOMPARE(orders.at(3).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("VARCHAR(40)"));

        QCOMPARE(db.foreignKeys(QStringLiteral("pedidos")).size(), 1);
        const QVariantList relations = db.relations();
        QCOMPARE(relations.size(), 1);
        QCOMPARE(relations.first().toMap().value(QStringLiteral("refTable")).toString(), QStringLiteral("clientes"));

        const QVariantList indexes = db.indexes(QStringLiteral("clientes"));
        QCOMPARE(indexes.size(), 1);
        QCOMPARE(indexes.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("idx_ciudad"));
        QCOMPARE(indexes.first().toMap().value(QStringLiteral("columns")).toStringList(), QStringList{QStringLiteral("ciudad")});

        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 3);
        QVERIFY(db.sqlOf(QStringLiteral("grandes")).startsWith(QStringLiteral("CREATE VIEW")));
        QCOMPARE(db.schema().value(QStringLiteral("pedidos")).toStringList().size(), 4);
        QCOMPARE(db.keyColumns(QStringLiteral("grandes")), QStringList());
        QCOMPARE(db.keyColumns(QStringLiteral("clientes")).size(), 1);
        QCOMPARE(db.rowidAlias(QStringLiteral("clientes")), QStringLiteral("id"));

        const QVariantList lookup = db.lookup(QStringLiteral("clientes"), QStringLiteral("id"));
        QCOMPARE(lookup.size(), 3);
        QCOMPARE(lookup.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Ana"));
    }

    void awkwardNames()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        const QString table = QStringLiteral("mi \"tabla\"; DROP TABLE x");
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE %1 (%2 TEXT, rowid TEXT, oid TEXT)").arg(Sql::ident(table), Sql::ident(QStringLiteral("un 'campo'")))));
        db.touch(true);
        QVERIFY(db.exists(table));
        QCOMPARE(db.columnNames(table).size(), 3);
        // rowid and oid are hidden by columns: the key goes by the name left.
        QCOMPARE(db.keyColumns(table), QStringList{QStringLiteral("_rowid_")});
        const QVariantList key = db.insertRow(table, {{QStringLiteral("un 'campo'"), QStringLiteral("x'y\"z")}});
        QCOMPARE(key.size(), 1);
        QVERIFY(db.updateCell(table, key, QStringLiteral("un 'campo'"), QStringLiteral("'); DELETE FROM t; --")));
        QCOMPARE(db.row(table, key).value(QStringLiteral("un 'campo'")).toString(), QStringLiteral("'); DELETE FROM t; --"));
        QCOMPARE(db.rowCount(table), 1);
    }

    void editingWithUndo()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QVERIFY(!db.canUndo());
        QSignalSpy changed(&db, &Database::dataChanged);

        QVERIFY(db.updateCell(QStringLiteral("clientes"), {2}, QStringLiteral("ciudad"), QStringLiteral("Lugo")));
        QCOMPARE(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("ciudad")).toString(), QStringLiteral("Lugo"));
        QVERIFY(changed.size() >= 1);
        QVERIFY(db.canUndo());
        QVERIFY(!db.undoText().isEmpty());
        QVERIFY(db.undo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("ciudad")).toString(), QStringLiteral("Vigo"));
        QVERIFY(db.canRedo());
        QVERIFY(db.redo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("ciudad")).toString(), QStringLiteral("Lugo"));

        // NULL goes in and comes back as NULL, not as text.
        QVERIFY(db.updateCell(QStringLiteral("clientes"), {2}, QStringLiteral("ciudad"), QVariant()));
        QVERIFY(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("ciudad")).isNull());
        QVERIFY(db.undo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("ciudad")).toString(), QStringLiteral("Lugo"));

        // A constraint that fails changes nothing and says why.
        QVERIFY(!db.updateCell(QStringLiteral("clientes"), {2}, QStringLiteral("nombre"), QVariant()));
        QVERIFY(!db.lastError().isEmpty());
        QCOMPARE(db.row(QStringLiteral("clientes"), {2}).value(QStringLiteral("nombre")).toString(), QStringLiteral("Luis"));
        QVERIFY(!db.updateCell(QStringLiteral("pedidos"), {1}, QStringLiteral("cliente_id"), 99));

        // Inserting: defaults fill what is not given.
        const QVariantList key = db.insertRow(QStringLiteral("clientes"), {{QStringLiteral("nombre"), QStringLiteral("Hugo")}});
        QCOMPARE(key, QVariantList{4});
        QCOMPARE(db.row(QStringLiteral("clientes"), key).value(QStringLiteral("ciudad")).toString(), QStringLiteral("Madrid"));
        QVERIFY(db.undo());
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 3);
        QVERIFY(db.redo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {4}).value(QStringLiteral("nombre")).toString(), QStringLiteral("Hugo"));
        QVERIFY(db.insertRow(QStringLiteral("clientes"), {}).isEmpty());

        // Changing the key of a row moves it, and undo finds it again.
        QVariantList moved;
        QVERIFY(db.updateCell(QStringLiteral("clientes"), {4}, QStringLiteral("id"), 40, &moved));
        QCOMPARE(moved, QVariantList{40});
        QVERIFY(db.undo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {4}).value(QStringLiteral("nombre")).toString(), QStringLiteral("Hugo"));

        // Deleting takes the rows that hang from it (ON DELETE CASCADE); undo brings back the row.
        QVERIFY(db.deleteRows(QStringLiteral("clientes"), {{3}, {4}}));
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 2);
        QVERIFY(db.undo());
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 4);
        QVERIFY(db.row(QStringLiteral("clientes"), {3}).value(QStringLiteral("ciudad")).isNull());
        QCOMPARE(db.row(QStringLiteral("clientes"), {3}).value(QStringLiteral("nombre")).toString(), QStringLiteral("Marta"));

        // A new change forgets what could be redone.
        QVERIFY(db.undo());
        QVERIFY(db.canRedo());
        QVERIFY(db.updateCell(QStringLiteral("clientes"), {1}, QStringLiteral("saldo"), 12.5));
        QVERIFY(!db.canRedo());
        QCOMPARE(db.row(QStringLiteral("clientes"), {1}).value(QStringLiteral("saldo")).toDouble(), 12.5);
    }

    void relatedRecords()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QCOMPARE(db.labelColumn(QStringLiteral("clientes")), QStringLiteral("nombre"));
        QCOMPARE(db.labelColumn(QStringLiteral("pedidos")), QStringLiteral("nota"));
        // A client: the orders that refer to it.
        QVariantList related = db.related(QStringLiteral("clientes"), {{QStringLiteral("id"), 1}, {QStringLiteral("nombre"), QStringLiteral("Ana")}});
        QCOMPARE(related.size(), 1);
        QVariantMap link = related.first().toMap();
        QCOMPARE(link.value(QStringLiteral("direction")).toString(), QStringLiteral("children"));
        QCOMPARE(link.value(QStringLiteral("table")).toString(), QStringLiteral("pedidos"));
        QCOMPARE(link.value(QStringLiteral("column")).toString(), QStringLiteral("cliente_id"));
        QCOMPARE(link.value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(link.value(QStringLiteral("rows")).toList().size(), 2);
        // No more rows than asked, though all are counted.
        related = db.related(QStringLiteral("clientes"), {{QStringLiteral("id"), 1}}, 1);
        QCOMPARE(related.first().toMap().value(QStringLiteral("rows")).toList().size(), 1);
        QCOMPARE(related.first().toMap().value(QStringLiteral("count")).toInt(), 2);
        related = db.related(QStringLiteral("clientes"), {{QStringLiteral("id"), 3}});
        QCOMPARE(related.first().toMap().value(QStringLiteral("count")).toInt(), 0);
        // An order: the client it refers to.
        related = db.related(QStringLiteral("pedidos"), {{QStringLiteral("id"), 3}, {QStringLiteral("cliente_id"), 2}});
        QCOMPARE(related.size(), 1);
        link = related.first().toMap();
        QCOMPARE(link.value(QStringLiteral("direction")).toString(), QStringLiteral("parent"));
        QCOMPARE(link.value(QStringLiteral("table")).toString(), QStringLiteral("clientes"));
        QCOMPARE(link.value(QStringLiteral("rows")).toList().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Luis"));
        // A key that holds nothing refers to nothing.
        QVERIFY(db.related(QStringLiteral("pedidos"), {{QStringLiteral("id"), 9}, {QStringLiteral("cliente_id"), QVariant()}}).isEmpty());
        QVERIFY(db.related(QStringLiteral("clientes"), {}).isEmpty());
    }

    void foreignKeysAreEnforced()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QVERIFY(db.insertRow(QStringLiteral("pedidos"), {{QStringLiteral("cliente_id"), 77}}).isEmpty());
        QVERIFY(db.deleteRows(QStringLiteral("clientes"), {{1}}));
        QCOMPARE(db.rowCount(QStringLiteral("pedidos")), 1);
    }

    void coercion()
    {
        QCOMPARE(Database::coerce(QStringLiteral("integer"), QStringLiteral(" 42 ")), QVariant(qlonglong(42)));
        QCOMPARE(Database::coerce(QStringLiteral("real"), QStringLiteral("12,5")), QVariant(12.5));
        QCOMPARE(Database::coerce(QStringLiteral("real"), QStringLiteral("3")), QVariant(qlonglong(3)));
        QVERIFY(Database::coerce(QStringLiteral("integer"), QStringLiteral("")).isNull());
        QCOMPARE(Database::coerce(QStringLiteral("integer"), QStringLiteral("abc")), QVariant(QStringLiteral("abc")));
        QCOMPARE(Database::coerce(QStringLiteral("text"), QStringLiteral(" a ")), QVariant(QStringLiteral(" a ")));
        QCOMPARE(Database::coerce(QStringLiteral("text"), QStringLiteral("")), QVariant(QString()));
        QCOMPARE(Database::coerce(QStringLiteral("bool"), true).toInt(), 1);
        QCOMPARE(Database::coerce(QStringLiteral("bool"), QStringLiteral("sí")).toInt(), 1);
        QCOMPARE(Database::coerce(QStringLiteral("bool"), QStringLiteral("no")).toInt(), 0);
        QCOMPARE(Database::coerce(QStringLiteral("date"), QStringLiteral("10/11/2026")).toString(), QStringLiteral("2026-11-10"));
        QCOMPARE(Database::coerce(QStringLiteral("date"), QStringLiteral("2026-11-10")).toString(), QStringLiteral("2026-11-10"));
        QVERIFY(Database::coerce(QStringLiteral("date"), QVariant()).isNull());
    }

    void running()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QVariantMap result = db.run(QStringLiteral("SELECT nombre FROM clientes WHERE ciudad = :c"), {{QStringLiteral("c"), QStringLiteral("Vigo")}});
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("columns")).toStringList(), QStringList{QStringLiteral("nombre")});
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 1);

        // A value is a value, whatever it says.
        result = db.run(QStringLiteral("SELECT count(*) FROM clientes WHERE nombre = :n"), {{QStringLiteral("n"), QStringLiteral("x' OR '1'='1")}});
        QCOMPARE(result.value(QStringLiteral("rows")).toList().first().toList().first().toInt(), 0);

        const int before = db.schemaRevision();
        result = db.run(QStringLiteral("CREATE TABLE t (a); INSERT INTO t VALUES (1), (2); UPDATE t SET a = a + 1;"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("statements")).toInt(), 3);
        // The rows every statement changed, added up: two inserted, two updated.
        QCOMPARE(result.value(QStringLiteral("affected")).toInt(), 4);
        QVERIFY(db.schemaRevision() > before);
        QVERIFY(db.exists(QStringLiteral("t")));

        result = db.run(QStringLiteral("INSERT INTO t VALUES (9); SELEC nada; INSERT INTO t VALUES (10)"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
        QCOMPARE(result.value(QStringLiteral("statement")).toString(), QStringLiteral("SELEC nada"));
        // The script stops at the failure: nothing after it ran.
        QCOMPARE(db.query(QStringLiteral("SELECT count(*) FROM t WHERE a = 10")).rows.first().first().toInt(), 0);

        result = db.run(QStringLiteral("SELECT * FROM pedidos"), {}, 2);
        QCOMPARE(result.value(QStringLiteral("rows")).toList().size(), 2);
        QVERIFY(result.value(QStringLiteral("truncated")).toBool());
    }

    void objects()
    {
        Database db;
        const QString path = fresh();
        QVERIFY(db.create(path));
        shop(db);
        QVERIFY(db.objects(QStringLiteral("query")).isEmpty());
        const int id = db.saveObject(QStringLiteral("query"), QStringLiteral("Todos"), {{QStringLiteral("sql"), QStringLiteral("SELECT * FROM clientes")}});
        QVERIFY(id > 0);
        QCOMPARE(db.saveObject(QStringLiteral("query"), QStringLiteral("Todos"), {}), 0);
        QCOMPARE(db.saveObject(QStringLiteral("query"), QStringLiteral(" "), {}), 0);
        QVERIFY(db.saveObject(QStringLiteral("form"), QStringLiteral("Todos"), {{QStringLiteral("table"), QStringLiteral("clientes")}}) > 0);
        QCOMPARE(db.saveObject(QStringLiteral("query"), QStringLiteral("Todos"), {{QStringLiteral("sql"), QStringLiteral("SELECT 1")}}, id), id);
        QCOMPARE(db.object(id).value(QStringLiteral("definition")).toMap().value(QStringLiteral("sql")).toString(), QStringLiteral("SELECT 1"));
        QVERIFY(db.renameObject(id, QStringLiteral("Uno")));
        QCOMPARE(db.objectNamed(QStringLiteral("query"), QStringLiteral("Uno")).value(QStringLiteral("id")).toInt(), id);
        // Bases' own tables are not the person's.
        QCOMPARE(db.tableNames().size(), 2);

        db.addHistory(QStringLiteral("SELECT 1"), true);
        db.addHistory(QStringLiteral("SELEC"), false);
        db.addHistory(QStringLiteral("SELECT 1"), true);
        const QVariantList history = db.history();
        QCOMPARE(history.size(), 2);
        QCOMPARE(history.first().toMap().value(QStringLiteral("sql")).toString(), QStringLiteral("SELECT 1"));
        QVERIFY(!history.last().toMap().value(QStringLiteral("ok")).toBool());

        db.close();
        QVERIFY(db.open(path));
        QCOMPARE(db.objects(QStringLiteral("query")).size(), 1);
        QVERIFY(db.removeObject(id));
        QVERIFY(db.objects(QStringLiteral("query")).isEmpty());
        db.clearHistory();
        QVERIFY(db.history().isEmpty());
    }

    void transactions()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QVERIFY(db.begin());
        QVERIFY(db.exec(QStringLiteral("DELETE FROM pedidos")));
        db.rollback();
        QCOMPARE(db.rowCount(QStringLiteral("pedidos")), 3);
        QVERIFY(db.begin());
        QVERIFY(db.exec(QStringLiteral("DELETE FROM pedidos WHERE id = ?"), {1}));
        QVERIFY(db.commit());
        QCOMPARE(db.rowCount(QStringLiteral("pedidos")), 2);
    }
};

QTEST_GUILESS_MAIN(TestDatabase)
#include "tst_database.moc"
