#include "database.h"
#include "gridmodel.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestGridModel : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_next = 0;

    void open(Database &db)
    {
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("g%1.db").arg(++m_next))));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE ciudades (id INTEGER PRIMARY KEY, nombre TEXT)")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE gente (id INTEGER PRIMARY KEY AUTOINCREMENT, nombre TEXT NOT NULL, edad INTEGER, ciudad_id INTEGER REFERENCES ciudades (id), alta DATE, activo BOOLEAN DEFAULT 1)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO ciudades VALUES (1, 'Madrid'), (2, 'Vigo')")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre, edad, ciudad_id, alta) VALUES ('Ana', 30, 1, '2026-01-05'), ('Luis', 45, 2, '2026-02-01'), ('Marta', NULL, 1, NULL), ('Óscar', 22, NULL, '2025-12-31'), ('50% Ana_', 61, 2, NULL)")));
        QVERIFY(db.exec(QStringLiteral("CREATE VIEW mayores AS SELECT nombre, edad FROM gente WHERE edad > 40")));
        db.touch(true);
    }

    static QStringList column(const GridModel &model, int at)
    {
        QStringList values;
        for (int row = 0; row < model.count(); ++row) {
            values.append(model.text(row, at));
        }
        return values;
    }

private Q_SLOTS:
    void table()
    {
        Database db;
        open(db);
        GridModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(&db);
        QCOMPARE(model.count(), 0);
        model.setTable(QStringLiteral("gente"));
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.count(), 5);
        QCOMPARE(model.total(), 5);
        QCOMPARE(model.rowCount(), 5);
        QCOMPARE(model.columnCount(), 6);
        QVERIFY(model.editable());
        QVERIFY(!model.filtered());
        QCOMPARE(model.fields().size(), 6);
        QCOMPARE(model.fields().at(3).toMap().value(QStringLiteral("fkTable")).toString(), QStringLiteral("ciudades"));
        QCOMPARE(model.headerData(1, Qt::Horizontal).toString(), QStringLiteral("nombre"));
        QCOMPARE(model.text(0, 1), QStringLiteral("Ana"));
        QCOMPARE(model.value(0, 2).toInt(), 30);
        QVERIFY(model.value(2, 2).isNull());
        QCOMPARE(model.text(2, 2), QString());
        QVERIFY(model.data(model.index(2, 2), GridModel::NullRole).toBool());
        QVERIFY(!model.data(model.index(0, 2), GridModel::NullRole).toBool());
        QCOMPARE(model.data(model.index(0, 5), GridModel::KindRole).toString(), QStringLiteral("bool"));
        QCOMPARE(model.record(1).value(QStringLiteral("nombre")).toString(), QStringLiteral("Luis"));
        QVERIFY(!model.value(99, 0).isValid());
        QVERIFY(model.widthHint(1) > 40);
    }

    void sortingFilteringSearching()
    {
        Database db;
        open(db);
        GridModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        model.sortBy(2);
        QCOMPARE(model.sortColumn(), 2);
        QVERIFY(!model.sortDescending());
        QCOMPARE(column(model, 1).mid(1), (QStringList{QStringLiteral("Óscar"), QStringLiteral("Ana"), QStringLiteral("Luis"), QStringLiteral("50% Ana_")}));
        model.sortBy(2);
        QVERIFY(model.sortDescending());
        QCOMPARE(model.text(0, 1), QStringLiteral("50% Ana_"));
        model.sortBy(2);
        QCOMPARE(model.sortColumn(), -1);
        QCOMPARE(model.text(0, 1), QStringLiteral("Ana"));

        model.setFilter(QStringLiteral("edad"), QStringLiteral(">=30"));
        QVERIFY(model.filtered());
        QCOMPARE(model.count(), 3);
        QCOMPARE(model.total(), 5);
        model.setFilter(QStringLiteral("nombre"), QStringLiteral("ana"));
        QCOMPARE(column(model, 1), (QStringList{QStringLiteral("Ana"), QStringLiteral("50% Ana_")}));
        model.setFilter(QStringLiteral("edad"), QString());
        QCOMPARE(model.count(), 2);
        model.clearFilters();
        QCOMPARE(model.count(), 5);
        model.setFilter(QStringLiteral("edad"), QStringLiteral("null"));
        QCOMPARE(column(model, 1), QStringList{QStringLiteral("Marta")});
        // A filter on a column that is not there, or one that tries something, changes nothing but the rows shown.
        model.setFilters({{QStringLiteral("no_existe\" OR 1=1 --"), QStringLiteral("x")}, {QStringLiteral("nombre"), QStringLiteral("'; DROP TABLE gente; --")}});
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.count(), 0);
        QVERIFY(db.exists(QStringLiteral("gente")));
        model.clearFilters();

        model.setSearch(QStringLiteral("vigo"));
        QCOMPARE(model.count(), 0);
        model.setSearch(QStringLiteral("2026-0"));
        QCOMPARE(model.count(), 2);
        // % and _ are what they are, not wildcards.
        model.setSearch(QStringLiteral("50%"));
        QCOMPARE(column(model, 1), QStringList{QStringLiteral("50% Ana_")});
        model.setSearch(QStringLiteral("a_"));
        QCOMPARE(column(model, 1), QStringList{QStringLiteral("50% Ana_")});
        model.setSearch(QStringLiteral("' OR '1'='1"));
        QCOMPARE(model.count(), 0);
        QVERIFY(model.error().isEmpty());
        model.setSearch(QString());
        QCOMPARE(model.count(), 5);

        QCOMPARE(model.find(QStringLiteral("mart"), 0), 2);
        QCOMPARE(model.find(QStringLiteral("ana"), 1), 4);
        QCOMPARE(model.find(QStringLiteral("ana"), 3, true), 0);
        QCOMPARE(model.find(QStringLiteral("zzz"), 0), -1);
    }

    void chainedConditions()
    {
        Database db;
        open(db);
        GridModel model;
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        const auto chain = [](std::initializer_list<QStringList> links) {
            QVariantList list;
            for (const QStringList &l : links) {
                list.append(QVariantMap{{QStringLiteral("column"), l.at(0)}, {QStringLiteral("op"), l.at(1)}, {QStringLiteral("value"), l.value(2)}});
            }
            return list;
        };
        QVERIFY(!model.filtered());
        model.setConditions(chain({{QStringLiteral("edad"), QStringLiteral("ge"), QStringLiteral("30")}}));
        QVERIFY(model.filtered());
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Ana"), QStringLiteral("Luis"), QStringLiteral("50% Ana_")}));
        // Every link must hold.
        model.setConditions(chain({{QStringLiteral("edad"), QStringLiteral("ge"), QStringLiteral("30")}, {QStringLiteral("nombre"), QStringLiteral("contains"), QStringLiteral("ana")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Ana"), QStringLiteral("50% Ana_")}));
        // What is typed is text, never a pattern nor SQL.
        model.setConditions(chain({{QStringLiteral("nombre"), QStringLiteral("contains"), QStringLiteral("%")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("50% Ana_")}));
        model.setConditions(chain({{QStringLiteral("nombre"), QStringLiteral("eq"), QStringLiteral("x' OR '1'='1")}}));
        QCOMPARE(model.count(), 0);
        model.setConditions(chain({{QStringLiteral("nombre"), QStringLiteral("starts"), QStringLiteral("L")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Luis")}));
        model.setConditions(chain({{QStringLiteral("nombre"), QStringLiteral("ends"), QStringLiteral("a")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Ana"), QStringLiteral("Marta")}));
        model.setConditions(chain({{QStringLiteral("edad"), QStringLiteral("null")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Marta")}));
        model.setConditions(chain({{QStringLiteral("edad"), QStringLiteral("notnull")}, {QStringLiteral("edad"), QStringLiteral("lt"), QStringLiteral("31")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Ana"), QStringLiteral("Óscar")}));
        // «Is not» keeps what holds nothing.
        model.setConditions(chain({{QStringLiteral("ciudad_id"), QStringLiteral("ne"), QStringLiteral("1")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Luis"), QStringLiteral("Óscar"), QStringLiteral("50% Ana_")}));
        model.setConditions(chain({{QStringLiteral("nombre"), QStringLiteral("excludes"), QStringLiteral("a")}}));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Luis")}));
        // A column that is not there, or an operator that is not one, filters nothing.
        model.setConditions(chain({{QStringLiteral("nada"), QStringLiteral("eq"), QStringLiteral("1")}, {QStringLiteral("edad"), QStringLiteral("drop"), QStringLiteral("1")}}));
        QCOMPARE(model.count(), 5);
        QVERIFY(!model.filtered());

        // The SQL of the view gives the same rows, alongside the other filters.
        model.setConditions(chain({{QStringLiteral("edad"), QStringLiteral("gt"), QStringLiteral("25")}, {QStringLiteral("nombre"), QStringLiteral("excludes"), QStringLiteral("50%")}}));
        model.setSearch(QStringLiteral("a"));
        QCOMPARE(column(model, 1), QStringList({QStringLiteral("Ana")}));
        const Result result = db.query(model.viewSql());
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.rows.size(), 1);
        model.clearFilters();
        QCOMPARE(model.count(), 5);
        QVERIFY(model.conditions().isEmpty());
        QVERIFY(GridModel::operators().size() >= 12);
    }

    void board()
    {
        Database db;
        open(db);
        GridModel model;
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        QVariantList groups = model.groups(QStringLiteral("ciudad_id"));
        QCOMPARE(groups.size(), 3);
        // A foreign key reads as what it refers to; what has none comes last.
        QCOMPARE(groups.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Madrid"));
        QCOMPARE(groups.at(0).toMap().value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(groups.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Vigo"));
        QVERIFY(groups.at(2).toMap().value(QStringLiteral("empty")).toBool());
        QCOMPARE(groups.at(2).toMap().value(QStringLiteral("rows")).toList().size(), 1);
        const QVariantMap card = groups.at(2).toMap().value(QStringLiteral("rows")).toList().first().toMap();
        QCOMPARE(card.value(QStringLiteral("record")).toMap().value(QStringLiteral("nombre")).toString(), QStringLiteral("Óscar"));

        // Moving a card is writing the column, and can be undone.
        QVERIFY(model.store(card.value(QStringLiteral("key")).toList(), QStringLiteral("ciudad_id"), 2));
        groups = model.groups(QStringLiteral("ciudad_id"));
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(1).toMap().value(QStringLiteral("count")).toInt(), 3);
        QVERIFY(db.undo());
        QCOMPARE(model.groups(QStringLiteral("ciudad_id")).size(), 3);
        // What the database refuses is refused.
        QVERIFY(!model.store(card.value(QStringLiteral("key")).toList(), QStringLiteral("ciudad_id"), 99));
        QVERIFY(!model.error().isEmpty());
        QVERIFY(!model.store({}, QStringLiteral("ciudad_id"), 1));
        QVERIFY(!model.store(card.value(QStringLiteral("key")).toList(), QStringLiteral("nada"), 1));

        // The board shows what the filters leave, and no more cards than asked.
        model.setSearch(QStringLiteral("ana"));
        groups = model.groups(QStringLiteral("ciudad_id"));
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).toMap().value(QStringLiteral("count")).toInt(), 1);
        model.setSearch(QString());
        groups = model.groups(QStringLiteral("ciudad_id"), 1);
        QCOMPARE(groups.at(0).toMap().value(QStringLiteral("rows")).toList().size(), 1);
        QCOMPARE(groups.at(0).toMap().value(QStringLiteral("count")).toInt(), 2);
        QVERIFY(model.groups(QStringLiteral("nada")).isEmpty());
        QCOMPARE(model.keyAt(0), QVariantList({1}));

        // A view groups as well, and has no keys to write with.
        model.setTable(QStringLiteral("mayores"));
        groups = model.groups(QStringLiteral("edad"));
        QCOMPARE(groups.size(), 2);
        QVERIFY(!model.store({1}, QStringLiteral("edad"), 3));
    }

    void viewSqlRunsTheSame()
    {
        Database db;
        open(db);
        GridModel model;
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        model.setFilter(QStringLiteral("nombre"), QStringLiteral("a"));
        model.setFilter(QStringLiteral("edad"), QStringLiteral("20..50"));
        model.setSearch(QStringLiteral("O'x%_"));
        QVERIFY(db.query(model.viewSql()).ok);
        model.setSearch(QStringLiteral("an"));
        model.sortBy(1);
        const Result result = db.query(model.viewSql());
        QVERIFY2(result.ok, qPrintable(result.error + model.viewSql()));
        QCOMPARE(result.rows.size(), model.count());
        QCOMPARE(result.rows.size(), 1);
        QCOMPARE(result.rows.first().at(1).toString(), QStringLiteral("Ana"));
    }

    void editing()
    {
        Database db;
        open(db);
        GridModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        QVERIFY(model.setValue(0, 2, QStringLiteral("31")));
        QCOMPARE(model.value(0, 2), QVariant(qlonglong(31)));
        QVERIFY(changed.size() >= 1);
        QVERIFY(model.setValue(0, 4, QStringLiteral("6/1/2026")));
        QCOMPARE(model.text(0, 4), QStringLiteral("2026-01-06"));
        QVERIFY(model.setValue(0, 5, false));
        QCOMPARE(model.value(0, 5).toInt(), 0);
        QVERIFY(model.setNull(0, 2));
        QVERIFY(model.value(0, 2).isNull());
        // What the table refuses stays as it was, with the reason.
        QVERIFY(!model.setNull(0, 1));
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(model.text(0, 1), QStringLiteral("Ana"));
        QVERIFY(!model.setValue(0, 3, QStringLiteral("99")));
        QCOMPARE(model.value(0, 3).toInt(), 1);
        QVERIFY(model.setValue(0, 1, QStringLiteral("Ana María")));
        QVERIFY(model.error().isEmpty());

        // Undo and redo come through the database and the grid follows.
        QVERIFY(db.undo());
        QCOMPARE(model.text(0, 1), QStringLiteral("Ana"));
        QVERIFY(db.redo());
        QCOMPARE(model.text(0, 1), QStringLiteral("Ana María"));

        const int at = model.insert({{QStringLiteral("nombre"), QStringLiteral("Hugo")}, {QStringLiteral("edad"), QStringLiteral("19")}});
        QCOMPARE(at, 5);
        QCOMPARE(model.count(), 6);
        QCOMPARE(model.total(), 6);
        QCOMPARE(model.text(5, 1), QStringLiteral("Hugo"));
        QCOMPARE(model.value(5, 5).toInt(), 1);
        QCOMPARE(model.insert({}), -2);
        QCOMPARE(model.count(), 6);

        // A new row the filter hides was added all the same.
        model.setFilter(QStringLiteral("edad"), QStringLiteral(">40"));
        QCOMPARE(model.insert({{QStringLiteral("nombre"), QStringLiteral("Eva")}, {QStringLiteral("edad"), 5}}), -1);
        QCOMPARE(model.total(), 7);
        model.clearFilters();

        QVERIFY(model.remove({1, 5}));
        QCOMPARE(model.count(), 5);
        QVERIFY(!column(model, 1).contains(QStringLiteral("Luis")));
        QVERIFY(!column(model, 1).contains(QStringLiteral("Hugo")));
        QVERIFY(db.undo());
        QCOMPARE(model.count(), 7);
        QVERIFY(column(model, 1).contains(QStringLiteral("Luis")));
    }

    void viewsAndQueries()
    {
        Database db;
        open(db);
        GridModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(&db);
        model.setTable(QStringLiteral("mayores"));
        QCOMPARE(model.count(), 2);
        QVERIFY(!model.editable());
        QVERIFY(!model.setValue(0, 0, QStringLiteral("x")));
        QCOMPARE(model.insert({{QStringLiteral("nombre"), QStringLiteral("x")}}), -2);

        model.setSql(QStringLiteral("SELECT nombre, edad * 2 AS doble FROM gente WHERE edad > :min ORDER BY edad;"));
        model.setParameters({{QStringLiteral("min"), 25}});
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.total(), -1);
        QCOMPARE(model.columnCount(), 2);
        QCOMPARE(column(model, 1), (QStringList{QStringLiteral("60"), QStringLiteral("90"), QStringLiteral("122")}));
        QVERIFY(!model.editable());
        // Sorting and filtering work on what the query gives.
        model.sortBy(1);
        model.sortBy(1);
        QCOMPARE(model.text(0, 1), QStringLiteral("122"));
        model.setFilter(QStringLiteral("doble"), QStringLiteral("<100"));
        QCOMPARE(model.count(), 2);
        model.clearFilters();

        model.setSql(QStringLiteral("PRAGMA table_info(gente)"));
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.count(), 6);
        model.setSql(QStringLiteral("SELEC nada"));
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(model.count(), 0);
        // A grid shows rows; it does not run what changes them.
        for (const QString &writes : {QStringLiteral("DELETE FROM gente"), QStringLiteral("DROP TABLE gente"), QStringLiteral("PRAGMA user_version = 7"),
                                      QStringLiteral("PRAGMA user_version(7)"), QStringLiteral("PRAGMA foreign_keys = OFF"), QStringLiteral("INSERT INTO gente (nombre) VALUES ('x') RETURNING id")}) {
            model.setSql(writes);
            QVERIFY2(!model.error().isEmpty(), qPrintable(writes));
            QCOMPARE(model.count(), 0);
        }
        QCOMPARE(db.rowCount(QStringLiteral("gente")), 5);
        // Bases marks its own files with 1.
        QCOMPARE(db.query(QStringLiteral("PRAGMA user_version")).rows.first().first().toInt(), 1);
        QCOMPARE(db.query(QStringLiteral("PRAGMA foreign_keys")).rows.first().first().toInt(), 1);
        model.setSql(QStringLiteral("EXPLAIN QUERY PLAN SELECT * FROM gente"));
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QVERIFY(model.count() > 0);
        model.setSql(QStringLiteral("pragma foreign_keys;"));
        QVERIFY2(model.error().isEmpty(), qPrintable(model.error()));
        QCOMPARE(model.text(0, 0), QStringLiteral("1"));
    }

    void subform()
    {
        Database db;
        open(db);
        GridModel model;
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        model.setLinkColumn(QStringLiteral("ciudad_id"));
        model.setLinkValue(2);
        QCOMPARE(column(model, 1), (QStringList{QStringLiteral("Luis"), QStringLiteral("50% Ana_")}));
        // A row added in a subform belongs to the record above it.
        QVERIFY(model.insert({{QStringLiteral("nombre"), QStringLiteral("Nuevo")}}) >= 0);
        QCOMPARE(db.query(QStringLiteral("SELECT ciudad_id FROM gente WHERE nombre = 'Nuevo'")).rows.first().first().toInt(), 2);
        model.setLinkValue(1);
        QCOMPARE(model.count(), 2);
        model.setLinkValue(QVariant());
        QCOMPARE(column(model, 1), QStringList{QStringLiteral("Óscar")});
    }

    void follows()
    {
        Database db;
        open(db);
        GridModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setDatabase(&db);
        model.setTable(QStringLiteral("gente"));
        // A script ran somewhere else.
        QVERIFY(db.run(QStringLiteral("DELETE FROM gente WHERE edad IS NULL")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.count(), 4);
        QVERIFY(db.run(QStringLiteral("ALTER TABLE gente ADD COLUMN nota TEXT")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.columnCount(), 7);
        QVERIFY(db.run(QStringLiteral("DROP VIEW mayores; DROP TABLE gente")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(model.count(), 0);
        db.close();
        QCOMPARE(model.count(), 0);
        QVERIFY(!model.editable());
    }

    void large()
    {
        Database db;
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("grande.db"))));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE filas (id INTEGER PRIMARY KEY, texto TEXT, n INTEGER)")));
        QVERIFY(db.exec(QStringLiteral("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM c WHERE x < 300000) INSERT INTO filas SELECT x, 'fila ' || x, x % 1000 FROM c")));
        db.touch(true);
        GridModel model;
        model.setDatabase(&db);
        QElapsedTimer timer;
        timer.start();
        model.setTable(QStringLiteral("filas"));
        QCOMPARE(model.count(), 300000);
        QCOMPARE(model.text(0, 1), QStringLiteral("fila 1"));
        QCOMPARE(model.text(299999, 1), QStringLiteral("fila 300000"));
        QCOMPARE(model.text(150000, 0), QStringLiteral("150001"));
        // Jumping about reads pages, never the table.
        for (int i = 0; i < 40; ++i) {
            const int row = (i * 7919 * 37) % 300000;
            QCOMPARE(model.value(row, 0).toInt(), row + 1);
        }
        const qint64 unsorted = timer.elapsed();
        model.setFilter(QStringLiteral("n"), QStringLiteral("=7"));
        QCOMPARE(model.count(), 300);
        model.clearFilters();
        model.sortBy(2);
        model.sortBy(2);
        QCOMPARE(model.value(0, 2).toInt(), 999);
        QVERIFY(model.setValue(0, 1, QStringLiteral("cambiada")));
        qInfo("300000 rows: open and 40 jumps in %lld ms, the whole test in %lld ms", unsorted, timer.elapsed());
        QVERIFY2(unsorted < 3000, "paging by rowid should not scan the table");
    }
};

QTEST_GUILESS_MAIN(TestGridModel)
#include "tst_gridmodel.moc"
