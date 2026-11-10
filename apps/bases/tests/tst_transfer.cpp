#include "database.h"
#include "transfer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class TestTransfer : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_next = 0;

    QString fresh(const QString &suffix = QStringLiteral(".db"))
    {
        return m_dir.filePath(QStringLiteral("f%1%2").arg(++m_next).arg(suffix));
    }

    QString write(const QByteArray &bytes, const QString &suffix = QStringLiteral(".csv"))
    {
        const QString path = fresh(suffix);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(bytes);
        }
        return path;
    }

    static QByteArray read(const QString &path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

private Q_SLOTS:
    void parsing()
    {
        QCOMPARE(Transfer::parseCsv(QStringLiteral("a,b\n1,2\n"), QLatin1Char(',')), (QList<QStringList>{{QStringLiteral("a"), QStringLiteral("b")}, {QStringLiteral("1"), QStringLiteral("2")}}));
        QCOMPARE(Transfer::parseCsv(QStringLiteral("a,b\r\n1,2"), QLatin1Char(',')).size(), 2);
        const QList<QStringList> rows = Transfer::parseCsv(QStringLiteral("\"uno, dos\",\"dijo \"\"hola\"\"\",\"dos\nlíneas\"\n,,\n"), QLatin1Char(','));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.first(), (QStringList{QStringLiteral("uno, dos"), QStringLiteral("dijo \"hola\""), QStringLiteral("dos\nlíneas")}));
        QCOMPARE(rows.last(), (QStringList{QString(), QString(), QString()}));
        QCOMPARE(Transfer::parseCsv(QStringLiteral("a;b;c\n1;2;3\n4;5;6"), QLatin1Char(';'), 2).size(), 2);
        QVERIFY(Transfer::parseCsv(QString(), QLatin1Char(',')).isEmpty());
        // An unfinished quote ends with the text.
        QCOMPARE(Transfer::parseCsv(QStringLiteral("a,\"b"), QLatin1Char(',')).size(), 1);
    }

    void detecting()
    {
        QCOMPARE(Transfer::detectSeparator(QStringLiteral("a;b;c\n1;2,5;3\n")), QLatin1Char(';'));
        QCOMPARE(Transfer::detectSeparator(QStringLiteral("a,b\n1,2\n")), QLatin1Char(','));
        QCOMPARE(Transfer::detectSeparator(QStringLiteral("a\tb\n1\t2\n")), QLatin1Char('\t'));
        QCOMPARE(Transfer::detectSeparator(QStringLiteral("a|b\n1|2\n")), QLatin1Char('|'));
        QCOMPARE(Transfer::detectType({QStringLiteral("1"), QStringLiteral(""), QStringLiteral("-3")}, false), QStringLiteral("INTEGER"));
        QCOMPARE(Transfer::detectType({QStringLiteral("1"), QStringLiteral("2.5")}, false), QStringLiteral("REAL"));
        QCOMPARE(Transfer::detectType({QStringLiteral("1,5"), QStringLiteral("2")}, true), QStringLiteral("REAL"));
        QCOMPARE(Transfer::detectType({QStringLiteral("1,5"), QStringLiteral("2")}, false), QStringLiteral("TEXT"));
        QCOMPARE(Transfer::detectType({QStringLiteral("2026-11-10"), QStringLiteral("10/11/2026")}, false), QStringLiteral("DATE"));
        QCOMPARE(Transfer::detectType({QStringLiteral("2026-11-10 08:30:00")}, false), QStringLiteral("DATETIME"));
        QCOMPARE(Transfer::detectType({QStringLiteral("sí"), QStringLiteral("no")}, false), QStringLiteral("BOOLEAN"));
        QCOMPARE(Transfer::detectType({QStringLiteral("a"), QStringLiteral("1")}, false), QStringLiteral("TEXT"));
        QCOMPARE(Transfer::detectType({}, false), QStringLiteral("TEXT"));
        // Postcodes and phone numbers keep their zeros.
        QCOMPARE(Transfer::detectType({QStringLiteral("08001"), QStringLiteral("28013")}, false), QStringLiteral("TEXT"));
    }

    void encodings()
    {
        QCOMPARE(Transfer::readText(write("ni\xC3\xB1o")), QStringLiteral("niño"));
        QCOMPARE(Transfer::readText(write("\xEF\xBB\xBFni\xC3\xB1o")), QStringLiteral("niño"));
        QCOMPARE(Transfer::readText(write("ni\xF1o")), QStringLiteral("niño"));
        QCOMPARE(Transfer::readText(write(QByteArray("\xFF\xFEn\0i\0\xF1\0o\0", 10))), QStringLiteral("niño"));
        QString error;
        QVERIFY(Transfer::readText(m_dir.filePath(QStringLiteral("no.csv")), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void importing()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        const QString path = write("nombre;alta;saldo;activo;cp;nota\nAna;01/02/2026;12,5;sí;08001;\"a; b\"\nO'Brien;2026-03-04;7;no;28013;\n;;;;;\n");
        const QVariantMap preview = Transfer::previewCsv(path);
        QVERIFY2(preview.value(QStringLiteral("ok")).toBool(), qPrintable(preview.value(QStringLiteral("error")).toString()));
        QCOMPARE(preview.value(QStringLiteral("separator")).toString(), QStringLiteral(";"));
        QCOMPARE(preview.value(QStringLiteral("header")).toInt(), 1);
        QStringList types;
        for (const QVariant &column : preview.value(QStringLiteral("columns")).toList()) {
            types.append(column.toMap().value(QStringLiteral("type")).toString());
        }
        QCOMPARE(types, (QStringList{QStringLiteral("TEXT"), QStringLiteral("DATE"), QStringLiteral("REAL"), QStringLiteral("BOOLEAN"), QStringLiteral("TEXT"), QStringLiteral("TEXT")}));

        const QVariantMap result = Transfer::importCsv(db, path, QStringLiteral("gente \"nueva\""));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString table = result.value(QStringLiteral("table")).toString();
        QCOMPARE(table, QStringLiteral("gente \"nueva\""));
        QCOMPARE(result.value(QStringLiteral("rows")).toInt(), 2);
        QCOMPARE(db.rowCount(table), 2);
        const Result rows = db.query(QStringLiteral("SELECT nombre, alta, saldo, activo, cp, nota, typeof(saldo) FROM \"gente \"\"nueva\"\"\" ORDER BY rowid"));
        QVERIFY2(rows.ok, qPrintable(rows.error));
        QCOMPARE(rows.rows.at(0).at(1).toString(), QStringLiteral("2026-02-01"));
        QCOMPARE(rows.rows.at(0).at(2).toDouble(), 12.5);
        QCOMPARE(rows.rows.at(0).at(3).toInt(), 1);
        QCOMPARE(rows.rows.at(0).at(4).toString(), QStringLiteral("08001"));
        QCOMPARE(rows.rows.at(0).at(5).toString(), QStringLiteral("a; b"));
        QCOMPARE(rows.rows.at(0).at(6).toString(), QStringLiteral("real"));
        QCOMPARE(rows.rows.at(1).at(0).toString(), QStringLiteral("O'Brien"));
        QVERIFY(rows.rows.at(1).at(5).isNull());

        // A new table of a name that is taken is refused.
        QVERIFY(!Transfer::importCsv(db, path, table).value(QStringLiteral("ok")).toBool());
        QCOMPARE(db.rowCount(table), 2);

        // Appending: by name when there is a header, whatever the order.
        QVariantMap more = Transfer::importCsvText(db, QStringLiteral("saldo,nombre\n3,Luis\n"), table, {{QStringLiteral("append"), true}});
        QVERIFY2(more.value(QStringLiteral("ok")).toBool(), qPrintable(more.value(QStringLiteral("error")).toString()));
        QCOMPARE(db.rowCount(table), 3);
        QCOMPARE(db.query(QStringLiteral("SELECT saldo FROM \"gente \"\"nueva\"\"\" WHERE nombre = 'Luis'")).rows.first().first().toInt(), 3);
        // A column the table does not have is left out, and the result says which.
        more = Transfer::importCsvText(db, QStringLiteral("nombre,inventada\nEva,1\n"), table, {{QStringLiteral("append"), true}});
        QVERIFY(more.value(QStringLiteral("ok")).toBool());
        QCOMPARE(more.value(QStringLiteral("skipped")).toStringList(), QStringList{QStringLiteral("inventada")});
        QCOMPARE(db.rowCount(table), 4);
        // When none matches there is nothing to import.
        more = Transfer::importCsvText(db, QStringLiteral("x,y\n1,2\n"), table, {{QStringLiteral("append"), true}, {QStringLiteral("header"), 1}});
        QVERIFY(!more.value(QStringLiteral("ok")).toBool());
        QCOMPARE(db.rowCount(table), 4);
        QVERIFY(!Transfer::importCsvText(db, QString(), QStringLiteral("vacia")).value(QStringLiteral("ok")).toBool());
        QVERIFY(!db.exists(QStringLiteral("vacia")));
    }

    void importingWithoutHeaderAndBadRows()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        QVariantMap result = Transfer::importCsvText(db, QStringLiteral("1,Ana\n2,Luis\n3,Eva\n"), QStringLiteral("t"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(db.rowCount(QStringLiteral("t")), 3);
        QCOMPARE(db.columnNames(QStringLiteral("t")).size(), 2);
        // Header names that repeat, are empty or try something are made usable.
        result = Transfer::importCsvText(db, QStringLiteral("a,a,,\"x\"\"); DROP TABLE t; --\"\nuno,dos,tres,cuatro\n"), QStringLiteral("raros"), {{QStringLiteral("header"), 1}});
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QStringList names = db.columnNames(QStringLiteral("raros"));
        QCOMPARE(names.size(), 4);
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), 4);
        QVERIFY(db.exists(QStringLiteral("t")));
        // A NOT NULL that a row breaks rolls the whole import back.
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE estricta (a TEXT NOT NULL, b INTEGER)")));
        db.touch(true);
        result = Transfer::importCsvText(db, QStringLiteral("a,b\nx,1\n,2\n"), QStringLiteral("estricta"), {{QStringLiteral("append"), true}});
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(db.rowCount(QStringLiteral("estricta")), 0);
    }

    void exporting()
    {
        Database db;
        QVERIFY(db.create(fresh()));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE \"mi tabla\" (id INTEGER PRIMARY KEY, texto TEXT, n REAL, b BLOB)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO \"mi tabla\" VALUES (1, 'uno, \"dos\"', 1.5, X'00ff'), (2, NULL, NULL, NULL), (3, 'línea\nnueva', 2, NULL), (4, 'O''Brien', -0.1, NULL)")));
        QVERIFY(db.exec(QStringLiteral("CREATE INDEX idx_texto ON \"mi tabla\" (texto)")));
        QVERIFY(db.exec(QStringLiteral("CREATE VIEW vista AS SELECT id, texto FROM \"mi tabla\" WHERE n > 1")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE otra (x)")));
        db.touch(true);
        QString error;

        // CSV: what was written reads back the same.
        const QString csv = fresh(QStringLiteral(".csv"));
        QVERIFY2(Transfer::exportCsv(db, QStringLiteral("mi tabla"), csv, QLatin1Char(','), &error), qPrintable(error));
        const QList<QStringList> rows = Transfer::parseCsv(Transfer::readText(csv), QLatin1Char(','));
        QCOMPARE(rows.size(), 5);
        QCOMPARE(rows.at(0), (QStringList{QStringLiteral("id"), QStringLiteral("texto"), QStringLiteral("n"), QStringLiteral("b")}));
        QCOMPARE(rows.at(1).at(1), QStringLiteral("uno, \"dos\""));
        QCOMPARE(rows.at(1).at(2), QStringLiteral("1.5"));
        QCOMPARE(rows.at(2).at(1), QString());
        QCOMPARE(rows.at(3).at(1), QStringLiteral("línea\nnueva"));
        const QString query = fresh(QStringLiteral(".csv"));
        QVERIFY(Transfer::exportCsv(db, QStringLiteral("SELECT id FROM \"mi tabla\" WHERE id > 2"), query, QLatin1Char(';'), &error));
        QCOMPARE(Transfer::parseCsv(Transfer::readText(query), QLatin1Char(';')).size(), 3);
        QVERIFY(!Transfer::exportCsv(db, QStringLiteral("DELETE FROM otra"), fresh(QStringLiteral(".csv")), QLatin1Char(','), &error));
        QVERIFY(!Transfer::exportCsv(db, QStringLiteral("SELECT * FROM no_existe"), fresh(QStringLiteral(".csv")), QLatin1Char(','), &error));

        // JSON.
        const QString json = fresh(QStringLiteral(".json"));
        QVERIFY2(Transfer::exportJson(db, QStringLiteral("mi tabla"), json, &error), qPrintable(error));
        QJsonParseError parse;
        const QJsonArray array = QJsonDocument::fromJson(read(json), &parse).array();
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QCOMPARE(array.size(), 4);
        QCOMPARE(array.at(0).toObject().value(QStringLiteral("id")).toInt(), 1);
        QCOMPARE(array.at(0).toObject().value(QStringLiteral("n")).toDouble(), 1.5);
        QCOMPARE(array.at(0).toObject().value(QStringLiteral("b")).toString(), QStringLiteral("AP8="));
        QVERIFY(array.at(1).toObject().value(QStringLiteral("texto")).isNull());
        QCOMPARE(array.at(2).toObject().value(QStringLiteral("texto")).toString(), QStringLiteral("línea\nnueva"));

        // SQL: the dump makes the same database again.
        const QString dump = fresh(QStringLiteral(".sql"));
        QVERIFY2(Transfer::exportSql(db, {}, dump, &error), qPrintable(error));
        Database copy;
        QVERIFY(copy.create(fresh()));
        const QVariantMap ran = copy.run(QString::fromUtf8(read(dump)));
        QVERIFY2(ran.value(QStringLiteral("ok")).toBool(), qPrintable(ran.value(QStringLiteral("error")).toString() + ran.value(QStringLiteral("statement")).toString()));
        QCOMPARE(copy.tableNames(true), db.tableNames(true));
        const QString all = QStringLiteral("SELECT id, texto, n, hex(b), typeof(n) FROM \"mi tabla\" ORDER BY id");
        QCOMPARE(copy.query(all).rows, db.query(all).rows);
        QCOMPARE(copy.indexes(QStringLiteral("mi tabla")).size(), 1);
        QCOMPARE(copy.query(QStringLiteral("SELECT count(*) FROM vista")).rows.first().first().toInt(), 2);

        // Only the tables asked for.
        const QString one = fresh(QStringLiteral(".sql"));
        QVERIFY(Transfer::exportSql(db, {QStringLiteral("otra")}, one, &error));
        QVERIFY(!read(one).contains("mi tabla"));
        QVERIFY(read(one).contains("CREATE TABLE otra"));

        const QString inserts = fresh(QStringLiteral(".sql"));
        QVERIFY(Transfer::exportQuerySql(db, QStringLiteral("SELECT id, texto FROM \"mi tabla\" WHERE id = 4"), QStringLiteral("destino"), inserts, &error));
        QVERIFY2(read(inserts).contains("INSERT INTO \"destino\" (\"id\", \"texto\") VALUES (4, 'O''Brien');"), read(inserts).constData());
    }

    void access()
    {
        // mdb-tools decides whether the option exists at all.
        Database db;
        QVERIFY(db.create(fresh()));
        const QString path = write("no es un archivo de Access", QStringLiteral(".mdb"));
        const QVariantMap result = Transfer::importMdb(db, path);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(db.tableNames().isEmpty());
        if (!Transfer::hasMdbTools()) {
            QSKIP("mdb-tools is not installed: reading a real Access file is not tested here.");
        }
    }
};

QTEST_GUILESS_MAIN(TestTransfer)
#include "tst_transfer.moc"
