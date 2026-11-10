#include "database.h"
#include "designer.h"

#include <QTemporaryDir>
#include <QTest>

class TestDesigner : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_next = 0;

    QString fresh()
    {
        return m_dir.filePath(QStringLiteral("db%1.sqlite").arg(++m_next));
    }

    static ColumnDef column(const QString &name, const QString &type, int pk = 0)
    {
        ColumnDef def;
        def.name = name;
        def.type = type;
        def.pk = pk;
        return def;
    }

    static void shop(Database &db)
    {
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE clientes (id INTEGER PRIMARY KEY AUTOINCREMENT, nombre TEXT NOT NULL, ciudad TEXT DEFAULT 'Madrid', saldo TEXT)")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE pedidos (id INTEGER PRIMARY KEY, cliente_id INTEGER NOT NULL REFERENCES clientes (id) ON DELETE CASCADE, total REAL)")));
        QVERIFY(db.exec(QStringLiteral("CREATE INDEX idx_ciudad ON clientes (ciudad)")));
        QVERIFY(db.exec(QStringLiteral("CREATE INDEX idx_nombre ON clientes (nombre)")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE registro (texto TEXT)")));
        QVERIFY(db.exec(QStringLiteral("CREATE TRIGGER al_borrar AFTER DELETE ON clientes BEGIN INSERT INTO registro VALUES (old.nombre); END")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO clientes (nombre, ciudad, saldo) VALUES ('Ana', 'Madrid', '10.5'), ('Luis', 'Vigo', '7'), ('Marta', NULL, NULL)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO pedidos (cliente_id, total) VALUES (1, 50), (1, 150), (2, 300)")));
        db.touch(true);
    }

private Q_SLOTS:
    void validation()
    {
        TableDef def;
        QVERIFY(!Designer::validate(def).isEmpty());
        def.name = QStringLiteral("t");
        QVERIFY(!Designer::validate(def).isEmpty());
        def.columns = {column(QStringLiteral("a"), QStringLiteral("INTEGER")), column(QStringLiteral("A"), QStringLiteral("TEXT"))};
        QVERIFY(Designer::validate(def).contains(QStringLiteral("repetido")));
        def.columns[1].name = QStringLiteral("b");
        QVERIFY(Designer::validate(def).isEmpty());
        def.columns[1].type = QStringLiteral("TEXT); DROP TABLE x; --");
        QVERIFY(!Designer::validate(def).isEmpty());
        def.columns[1].type = QStringLiteral("TEXT");
        def.columns[1].autoIncrement = true;
        QVERIFY(!Designer::validate(def).isEmpty());
        def.columns[1].autoIncrement = false;
        def.name = QStringLiteral("sqlite_cosa");
        QVERIFY(!Designer::validate(def).isEmpty());
        def.name = QStringLiteral("_wonderland_objects");
        QVERIFY(!Designer::validate(def).isEmpty());
    }

    void ddl()
    {
        TableDef def;
        def.name = QStringLiteral("mi \"tabla\"");
        ColumnDef id = column(QStringLiteral("id"), QStringLiteral("integer"), 1);
        id.autoIncrement = true;
        ColumnDef name = column(QStringLiteral("nombre"), QStringLiteral("TEXT"));
        name.notNull = true;
        name.unique = true;
        name.defaultValue = QStringLiteral("sin nombre");
        ColumnDef owner = column(QStringLiteral("dueño"), QStringLiteral("INTEGER"));
        owner.fkTable = QStringLiteral("otra");
        owner.fkColumn = QStringLiteral("id");
        owner.fkOnDelete = QStringLiteral("set null");
        owner.fkOnUpdate = QStringLiteral("; DROP TABLE x");
        def.columns = {id, name, owner};
        QCOMPARE(Designer::createSql(def),
                 QStringLiteral("CREATE TABLE \"mi \"\"tabla\"\"\" (\n"
                                "    \"id\" INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                                "    \"nombre\" TEXT NOT NULL UNIQUE DEFAULT 'sin nombre',\n"
                                "    \"dueño\" INTEGER REFERENCES \"otra\" (\"id\") ON DELETE SET NULL\n)"));

        TableDef pair;
        pair.name = QStringLiteral("par");
        pair.columns = {column(QStringLiteral("a"), QStringLiteral("INTEGER"), 2), column(QStringLiteral("b"), QStringLiteral("INTEGER"), 1), column(QStringLiteral("c"), QStringLiteral("TEXT"))};
        pair.withoutRowid = true;
        QVERIFY(Designer::createSql(pair).contains(QStringLiteral("PRIMARY KEY (\"b\", \"a\")")));
        QVERIFY(Designer::createSql(pair).endsWith(QStringLiteral(" WITHOUT ROWID")));
    }

    void creating()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        TableDef def;
        def.name = QStringLiteral("notas");
        ColumnDef id = column(QStringLiteral("id"), QStringLiteral("INTEGER"), 1);
        id.autoIncrement = true;
        ColumnDef text = column(QStringLiteral("texto"), QStringLiteral("TEXT"));
        text.defaultValue = QStringLiteral("x'); DROP TABLE notas; --");
        def.columns = {id, text};
        QString error;
        QVERIFY2(Designer::create(db, def, &error), qPrintable(error));
        QVERIFY(!Designer::create(db, def, &error));
        QVERIFY(!db.insertRow(QStringLiteral("notas"), {{QStringLiteral("id"), 1}}).isEmpty());
        QCOMPARE(db.row(QStringLiteral("notas"), {1}).value(QStringLiteral("texto")).toString(), QStringLiteral("x'); DROP TABLE notas; --"));
        // What was created reads back as it was defined.
        const TableDef read = Designer::read(db, QStringLiteral("notas"));
        QCOMPARE(read.columns.size(), 2);
        QVERIFY(read.columns.at(0).autoIncrement);
        QCOMPARE(read.columns.at(0).pk, 1);
        QCOMPARE(read.columns.at(1).origin, QStringLiteral("texto"));
        const TableDef again = Designer::fromVariant(Designer::toVariant(read));
        QCOMPARE(Designer::createSql(again), Designer::createSql(read));
    }

    void renamingOnly()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        TableDef def = Designer::read(db, QStringLiteral("clientes"));
        def.name = QStringLiteral("personas");
        def.columns[1].name = QStringLiteral("nombre_completo");
        const QStringList plan = Designer::plan(db, QStringLiteral("clientes"), def);
        QCOMPARE(plan.size(), 2);
        QVERIFY(plan.at(0).startsWith(QStringLiteral("ALTER TABLE")));
        QString error;
        QVERIFY2(Designer::alter(db, QStringLiteral("clientes"), def, &error), qPrintable(error));
        QVERIFY(db.exists(QStringLiteral("personas")));
        QVERIFY(!db.exists(QStringLiteral("clientes")));
        QCOMPARE(db.columnNames(QStringLiteral("personas")).at(1), QStringLiteral("nombre_completo"));
        // The key that referred to it follows the new name, and so does the index.
        QCOMPARE(db.columns(QStringLiteral("pedidos")).at(1).toMap().value(QStringLiteral("fkTable")).toString(), QStringLiteral("personas"));
        QCOMPARE(db.indexes(QStringLiteral("personas")).size(), 2);
        QCOMPARE(db.rowCount(QStringLiteral("personas")), 3);
    }

    void swappingNames()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        TableDef def = Designer::read(db, QStringLiteral("clientes"));
        def.columns[1].name = QStringLiteral("ciudad");
        def.columns[2].name = QStringLiteral("nombre");
        QString error;
        QVERIFY2(Designer::alter(db, QStringLiteral("clientes"), def, &error), qPrintable(error));
        // The values stayed in their columns; only the names crossed.
        const QVariantMap row = db.row(QStringLiteral("clientes"), {2});
        QCOMPARE(row.value(QStringLiteral("ciudad")).toString(), QStringLiteral("Luis"));
        QCOMPARE(row.value(QStringLiteral("nombre")).toString(), QStringLiteral("Vigo"));
    }

    void rebuilding()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        TableDef def = Designer::read(db, QStringLiteral("clientes"));
        // Drop «ciudad», change the type of «saldo», rename «nombre», add
        // «email» and move «saldo» before the name.
        ColumnDef balance = def.columns.at(3);
        balance.type = QStringLiteral("REAL");
        ColumnDef name = def.columns.at(1);
        name.name = QStringLiteral("titular");
        ColumnDef mail = column(QStringLiteral("email"), QStringLiteral("TEXT"));
        mail.defaultValue = QStringLiteral("-");
        def.columns = {def.columns.at(0), balance, name, mail};
        QString error;
        QVERIFY2(Designer::alter(db, QStringLiteral("clientes"), def, &error), qPrintable(error));

        QCOMPARE(db.columnNames(QStringLiteral("clientes")), (QStringList{QStringLiteral("id"), QStringLiteral("saldo"), QStringLiteral("titular"), QStringLiteral("email")}));
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 3);
        const QVariantMap ana = db.row(QStringLiteral("clientes"), {1});
        QCOMPARE(ana.value(QStringLiteral("titular")).toString(), QStringLiteral("Ana"));
        QCOMPARE(ana.value(QStringLiteral("saldo")).typeId(), int(QMetaType::Double));
        QCOMPARE(ana.value(QStringLiteral("saldo")).toDouble(), 10.5);
        QCOMPARE(ana.value(QStringLiteral("email")).toString(), QStringLiteral("-"));
        QVERIFY(db.row(QStringLiteral("clientes"), {3}).value(QStringLiteral("saldo")).isNull());
        // The rows that referred to it are all still there, and still tied.
        QCOMPARE(db.rowCount(QStringLiteral("pedidos")), 3);
        QVERIFY(db.query(QStringLiteral("PRAGMA foreign_key_check")).rows.isEmpty());
        QCOMPARE(db.query(QStringLiteral("PRAGMA foreign_keys")).rows.first().first().toInt(), 1);
        QVERIFY(db.insertRow(QStringLiteral("pedidos"), {{QStringLiteral("cliente_id"), 77}}).isEmpty());
        // The index on the dropped column went with it; the other followed the rename.
        const QVariantList indexes = db.indexes(QStringLiteral("clientes"));
        QCOMPARE(indexes.size(), 1);
        QCOMPARE(indexes.first().toMap().value(QStringLiteral("columns")).toStringList(), QStringList{QStringLiteral("titular")});
        // AUTOINCREMENT goes on counting where it was.
        QCOMPARE(db.insertRow(QStringLiteral("clientes"), {{QStringLiteral("titular"), QStringLiteral("Hugo")}}), QVariantList{4});
        // The trigger was made again.
        QVERIFY(db.deleteRows(QStringLiteral("clientes"), {{4}}));
        QCOMPARE(db.rowCount(QStringLiteral("registro")), 1);
        QVERIFY(!db.exists(QStringLiteral("_bases_new_clientes")));
    }

    void failedRebuildChangesNothing()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        const QString before = db.sqlOf(QStringLiteral("clientes"));
        TableDef def = Designer::read(db, QStringLiteral("clientes"));
        // «ciudad» has a NULL: making it NOT NULL cannot hold.
        def.columns[2].notNull = true;
        def.columns[2].defaultValue.clear();
        QString error;
        QVERIFY(!Designer::alter(db, QStringLiteral("clientes"), def, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(db.sqlOf(QStringLiteral("clientes")), before);
        QCOMPARE(db.rowCount(QStringLiteral("clientes")), 3);
        QCOMPARE(db.indexes(QStringLiteral("clientes")).size(), 2);
        QVERIFY(!db.exists(QStringLiteral("_bases_new_clientes")));
        QCOMPARE(db.query(QStringLiteral("PRAGMA foreign_keys")).rows.first().first().toInt(), 1);

        // A rename that is part of a failed change is undone with it.
        def.columns[1].name = QStringLiteral("otro");
        QVERIFY(!Designer::alter(db, QStringLiteral("clientes"), def, &error));
        QCOMPARE(db.columnNames(QStringLiteral("clientes")).at(1), QStringLiteral("nombre"));

        // A key that would be left dangling stops the change too.
        TableDef orders = Designer::read(db, QStringLiteral("pedidos"));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE otros (id INTEGER PRIMARY KEY)")));
        db.touch(true);
        orders.columns[1].fkTable = QStringLiteral("otros");
        QVERIFY(!Designer::alter(db, QStringLiteral("pedidos"), orders, &error));
        QVERIFY(error.contains(QStringLiteral("claves foráneas")));
        QCOMPARE(db.columns(QStringLiteral("pedidos")).at(1).toMap().value(QStringLiteral("fkTable")).toString(), QStringLiteral("clientes"));
    }

    void nothingToDo()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        const TableDef def = Designer::read(db, QStringLiteral("clientes"));
        QVERIFY(Designer::plan(db, QStringLiteral("clientes"), def).isEmpty());
        const int revision = db.schemaRevision();
        QString error;
        QVERIFY(Designer::alter(db, QStringLiteral("clientes"), def, &error));
        QCOMPARE(db.schemaRevision(), revision);
    }

    void losses()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE a (x INTEGER CHECK (x > 0), y TEXT COLLATE NOCASE, UNIQUE (x, y))")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE b (x INTEGER, y TEXT)")));
        db.touch(true);
        QCOMPARE(Designer::losses(db, QStringLiteral("a")).size(), 3);
        QVERIFY(Designer::losses(db, QStringLiteral("b")).isEmpty());
    }

    void indexesAndViews()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        shop(db);
        QString error;
        QVERIFY2(Designer::createIndex(db, QStringLiteral("pedidos"), QString(), {QStringLiteral("cliente_id"), QStringLiteral("total")}, false, &error), qPrintable(error));
        QCOMPARE(db.indexes(QStringLiteral("pedidos")).size(), 1);
        const QString name = db.indexes(QStringLiteral("pedidos")).first().toMap().value(QStringLiteral("name")).toString();
        QCOMPARE(name, QStringLiteral("idx_pedidos_cliente_id_total"));
        // A unique index over values that repeat cannot be made.
        QVERIFY(!Designer::createIndex(db, QStringLiteral("pedidos"), QStringLiteral("u"), {QStringLiteral("cliente_id")}, true, &error));
        QVERIFY(Designer::dropIndex(db, name, &error));
        QVERIFY(db.indexes(QStringLiteral("pedidos")).isEmpty());

        QVERIFY(Designer::createView(db, QStringLiteral("caros"), QStringLiteral("SELECT * FROM pedidos WHERE total > 100;"), &error));
        QVERIFY(db.isView(QStringLiteral("caros")));
        QVERIFY(!Designer::createView(db, QStringLiteral("mala"), QStringLiteral("DROP TABLE pedidos"), &error));
        QVERIFY(!Designer::createView(db, QStringLiteral("doble"), QStringLiteral("SELECT 1; DROP TABLE pedidos"), &error));
        QVERIFY(db.exists(QStringLiteral("pedidos")));
        QVERIFY(Designer::renameTable(db, QStringLiteral("registro"), QStringLiteral("bitácora"), &error));
        QVERIFY(!Designer::renameTable(db, QStringLiteral("bitácora"), QStringLiteral("sqlite_x"), &error));
        QVERIFY(Designer::dropTable(db, QStringLiteral("caros"), &error));
        QVERIFY(Designer::dropTable(db, QStringLiteral("bitácora"), &error));
        QVERIFY(!db.exists(QStringLiteral("bitácora")));
    }
};

QTEST_GUILESS_MAIN(TestDesigner)
#include "tst_designer.moc"
