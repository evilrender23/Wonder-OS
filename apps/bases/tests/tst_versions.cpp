#include "database.h"
#include "versions.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class TestVersions : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static qint64 people(Database &db)
    {
        return db.query(QStringLiteral("SELECT count(*) FROM gente")).rows.value(0).value(0).toLongLong();
    }

private Q_SLOTS:
    void directoryPerDocument()
    {
        const QString a = Versions::directory(QStringLiteral("/datos"), QStringLiteral("/a/tienda.db"));
        const QString b = Versions::directory(QStringLiteral("/datos"), QStringLiteral("/b/tienda.db"));
        QVERIFY(a.startsWith(QStringLiteral("/datos/versions/")));
        QVERIFY(a != b);
        QCOMPARE(a, Versions::directory(QStringLiteral("/datos"), QStringLiteral("/a/../a/tienda.db")).isEmpty() ? QString() : a);
    }

    void saveListDescribe()
    {
        Database db;
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("uno.db"))));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE gente (id INTEGER PRIMARY KEY, nombre TEXT)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES (?), (?)"), {QStringLiteral("Ana"), QStringLiteral("O'Brien \"x\"")}));
        const QString dir = m_dir.filePath(QStringLiteral("v1"));
        QVERIFY(Versions::list(dir).isEmpty());

        QString error;
        const QString first = Versions::save(db, dir, &error);
        QVERIFY2(!first.isEmpty(), qPrintable(error));
        QVERIFY(QFile::exists(first));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES ('Luis')")));
        const QString second = Versions::save(db, dir, &error);
        QVERIFY2(!second.isEmpty(), qPrintable(error));
        QVERIFY(first != second);

        const QVariantList list = Versions::list(dir);
        QCOMPARE(list.size(), 2);
        // The latest first.
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("path")).toString(), second);
        QVERIFY(list.at(0).toMap().value(QStringLiteral("label")).toString().startsWith(QStringLiteral("Hoy, ")));
        QVERIFY(list.at(0).toMap().value(QStringLiteral("size")).toLongLong() > 0);

        const QVariantMap old = Versions::describe(first);
        QVERIFY(old.value(QStringLiteral("ok")).toBool());
        QCOMPARE(old.value(QStringLiteral("rows")).toLongLong(), 2);
        QCOMPARE(old.value(QStringLiteral("tables")).toList().size(), 1);
        QCOMPARE(old.value(QStringLiteral("tables")).toList().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("gente"));
        QCOMPARE(Versions::describe(second).value(QStringLiteral("rows")).toLongLong(), 3);
        // The database goes on working after a copy.
        QCOMPARE(people(db), 3);
    }

    void describeRejectsWhatIsNotADatabase()
    {
        const QString path = m_dir.filePath(QStringLiteral("nada.db"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("esto no es una base de datos, ni lo parece por ningun lado");
        file.close();
        QVERIFY(!Versions::describe(path).value(QStringLiteral("ok")).toBool());
        QVERIFY(!Versions::describe(m_dir.filePath(QStringLiteral("no-existe.db"))).value(QStringLiteral("ok")).toBool());
    }

    void keepsNoMoreThanTheLimit()
    {
        Database db;
        QVERIFY(db.create(m_dir.filePath(QStringLiteral("dos.db"))));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE gente (id INTEGER PRIMARY KEY, nombre TEXT)")));
        const QString dir = m_dir.filePath(QStringLiteral("v2"));
        QString last;
        for (int i = 0; i < Versions::limit + 5; ++i) {
            QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES (?)"), {QStringLiteral("n%1").arg(i)}));
            last = Versions::save(db, dir);
            QVERIFY(!last.isEmpty());
        }
        const QVariantList list = Versions::list(dir);
        QCOMPARE(list.size(), Versions::limit);
        // The newest stay.
        QCOMPARE(list.first().toMap().value(QStringLiteral("path")).toString(), last);
        QCOMPARE(Versions::describe(last).value(QStringLiteral("rows")).toLongLong(), Versions::limit + 5);
        QCOMPARE(Versions::describe(list.last().toMap().value(QStringLiteral("path")).toString()).value(QStringLiteral("rows")).toLongLong(), 6);
    }

    void restoreGoesBackAndCanBeUndone()
    {
        Database db;
        const QString path = m_dir.filePath(QStringLiteral("tres.db"));
        QVERIFY(db.create(path));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE gente (id INTEGER PRIMARY KEY, nombre TEXT)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES ('Ana')")));
        const QString dir = m_dir.filePath(QStringLiteral("v3"));
        const QString version = Versions::save(db, dir);
        QVERIFY(!version.isEmpty());
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES ('Luis'), ('Eva')")));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE otra (x)")));
        QCOMPARE(people(db), 3);

        QString error;
        QVERIFY2(Versions::restore(db, dir, version, &error), qPrintable(error));
        QVERIFY(db.isOpen());
        QCOMPARE(db.path(), path);
        QCOMPARE(people(db), 1);
        QVERIFY(!db.exists(QStringLiteral("otra")));
        QVERIFY(!QFile::exists(path + QStringLiteral(".restaurando")));
        // It can still be written to.
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES ('Zoe')")));

        // What was there before going back was kept: the latest but none.
        const QVariantList list = Versions::list(dir);
        QCOMPARE(list.size(), 2);
        const QString before = list.first().toMap().value(QStringLiteral("path")).toString();
        QCOMPARE(Versions::describe(before).value(QStringLiteral("rows")).toLongLong(), 3);
        QVERIFY2(Versions::restore(db, dir, before, &error), qPrintable(error));
        QCOMPARE(people(db), 3);
        QVERIFY(db.exists(QStringLiteral("otra")));
    }

    void restoreRefusesRubbish()
    {
        Database db;
        const QString path = m_dir.filePath(QStringLiteral("cuatro.db"));
        QVERIFY(db.create(path));
        QVERIFY(db.exec(QStringLiteral("CREATE TABLE gente (id INTEGER PRIMARY KEY, nombre TEXT)")));
        QVERIFY(db.exec(QStringLiteral("INSERT INTO gente (nombre) VALUES ('Ana')")));
        QString error;
        QVERIFY(!Versions::restore(db, m_dir.filePath(QStringLiteral("v4")), m_dir.filePath(QStringLiteral("nada.db")), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(db.isOpen());
        QCOMPARE(people(db), 1);
    }
};

QTEST_GUILESS_MAIN(TestVersions)
#include "tst_versions.moc"
