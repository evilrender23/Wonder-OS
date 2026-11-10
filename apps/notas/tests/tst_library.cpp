#include "library.h"
#include "store.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

static Doc words(const QString &text)
{
    return Doc::fromText(text);
}

static QStringList titles(const QVariantList &rows)
{
    QStringList list;
    for (const QVariant &row : rows) {
        const QVariantMap map = row.toMap();
        list << QString(map.value(QStringLiteral("depth")).toInt(), QLatin1Char('>')) + map.value(QStringLiteral("title")).toString();
    }
    return list;
}

class TestLibrary : public QObject
{
    Q_OBJECT

    int minute = 0;
    /// Each call a minute later: what is done later is later.
    void tick()
    {
        qputenv("WONDERLAND_NOTAS_NOW", QDateTime(QDate(2026, 3, 10), QTime(9, 0)).addSecs(60 * ++minute).toString(Qt::ISODate).toUtf8());
    }

private Q_SLOTS:
    void init()
    {
        minute = 0;
        tick();
    }

    void startsWithANotebook()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QVariantList notebooks = library.notebooks();
        QCOMPARE(notebooks.size(), 1);
        QCOMPARE(notebooks.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Mi cuaderno"));
        const QVariantList sections = notebooks.first().toMap().value(QStringLiteral("sections")).toList();
        QCOMPARE(sections.size(), 1);
        QCOMPARE(library.firstSection(), sections.first().toMap().value(QStringLiteral("id")).toString());
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("library.json"))));
        // And only once.
        Library again(dir.path());
        QCOMPARE(again.notebooks().size(), 1);
        QCOMPARE(again.firstSection(), library.firstSection());
    }

    void pagesPersist()
    {
        QTemporaryDir dir;
        QString id;
        QString section;
        {
            Library library(dir.path());
            section = library.firstSection();
            QSignalSpy spy(&library, &Library::changed);
            id = library.addPage(section, QString(), QStringLiteral("  Lista   de la compra "));
            QCOMPARE(spy.count(), 1);
            QVERIFY(library.setDoc(id, words(QStringLiteral("Leche\nPan"))));
            // The same again writes nothing.
            QVERIFY(!library.setDoc(id, words(QStringLiteral("Leche\nPan"))));
            QCOMPARE(spy.count(), 2);
            library.setTags(id, {QStringLiteral("Casa"), QStringLiteral("casa"), QStringLiteral(" compras ")});
            library.setPinned(id, true);
            QVERIFY(!library.setDoc(QStringLiteral("nope"), Doc()));
        }
        // One file per page, in its own directory; nothing half written beside it.
        const QDir pageDir(dir.filePath(QStringLiteral("pages/") + id));
        QCOMPARE(pageDir.entryList(QDir::Files), QStringList{QStringLiteral("page.json")});
        const QJsonObject onDisk = Store::read(pageDir.filePath(QStringLiteral("page.json")));
        QCOMPARE(onDisk.value(QLatin1String("title")).toString(), QStringLiteral("Lista de la compra"));
        QCOMPARE(onDisk.value(QLatin1String("modified")).toString(), QStringLiteral("2026-03-10T09:01:00"));

        Library library(dir.path());
        const QVariantMap page = library.page(id);
        QCOMPARE(page.value(QStringLiteral("title")).toString(), QStringLiteral("Lista de la compra"));
        QCOMPARE(page.value(QStringLiteral("tags")).toStringList(), QStringList({QStringLiteral("casa"), QStringLiteral("compras")}));
        QCOMPARE(page.value(QStringLiteral("pinned")).toBool(), true);
        QCOMPARE(page.value(QStringLiteral("section")).toString(), section);
        QCOMPARE(library.doc(id).toText(), QStringLiteral("Leche\nPan"));
        const QVariantMap row = library.pages(section).first().toMap();
        QCOMPARE(row.value(QStringLiteral("excerpt")).toString(), QStringLiteral("Leche Pan"));
        QCOMPARE(row.value(QStringLiteral("date")).toString(), QStringLiteral("09:01"));
        QVERIFY(library.page(QStringLiteral("nope")).isEmpty());
    }

    void brokenPagesAreSkipped()
    {
        QTemporaryDir dir;
        QString id;
        {
            Library library(dir.path());
            id = library.addPage(library.firstSection(), QString(), QStringLiteral("Buena"));
        }
        QDir(dir.path()).mkpath(QStringLiteral("pages/rota"));
        QFile broken(dir.filePath(QStringLiteral("pages/rota/page.json")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ this is not json");
        broken.close();
        QDir(dir.path()).mkpath(QStringLiteral("pages/vacia"));
        Library library(dir.path());
        QCOMPARE(titles(library.pages(library.firstSection())), QStringList{QStringLiteral("Buena")});
        // And they are left alone.
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("pages/rota/page.json"))));
    }

    void order()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString section = library.firstSection();
        const QString a = library.addPage(section, QString(), QStringLiteral("A"));
        tick();
        const QString b = library.addPage(section, QString(), QStringLiteral("B"));
        tick();
        const QString c = library.addPage(section, QString(), QStringLiteral("C"));
        tick();
        const QString a1 = library.addPage(section, a, QStringLiteral("A1"));
        tick();
        const QString a2 = library.addPage(section, a, QStringLiteral("A2"));
        tick();
        const QString a11 = library.addPage(section, a1, QStringLiteral("A11"));
        // The latest first; subpages under their page, in the order they were made.
        QCOMPARE(titles(library.pages(section)), QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A"), QStringLiteral(">A1"), QStringLiteral(">>A11"), QStringLiteral(">A2")}));
        tick();
        library.setDoc(b, words(QStringLiteral("cambio")));
        QCOMPARE(titles(library.pages(section)).first(), QStringLiteral("B"));
        library.setPinned(a, true);
        QCOMPARE(titles(library.pages(section)).first(), QStringLiteral("A"));
        QCOMPARE(library.pages(section).first().toMap().value(QStringLiteral("children")).toInt(), 2);
        // No deeper than two levels: a fourth level lands as a third.
        const QString deep = library.addPage(section, a11, QStringLiteral("A111"));
        QCOMPARE(library.page(deep).value(QStringLiteral("parent")).toString(), QString());

        // Hanging and unhanging.
        QVERIFY(library.setParent(c, b));
        QCOMPARE(library.page(c).value(QStringLiteral("depth")).toInt(), 1);
        QVERIFY(!library.setParent(a, a11));
        QVERIFY(!library.setParent(a, a));
        QVERIFY(!library.setParent(c, QStringLiteral("nope")));
        QVERIFY(library.setParent(c, QString()));
        QVERIFY(!library.setParent(c, QString()));
        Q_UNUSED(a2)
    }

    void sectionsAndNotebooks()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString first = library.firstSection();
        const QString notebook = library.section(first).value(QStringLiteral("notebook")).toString();
        const QString work = library.addSection(notebook, QStringLiteral(" Trabajo "), QStringLiteral("#0a84ff"));
        QCOMPARE(library.section(work).value(QStringLiteral("name")).toString(), QStringLiteral("Trabajo"));
        QCOMPARE(library.section(work).value(QStringLiteral("color")).toString(), QStringLiteral("#0a84ff"));
        // A colour that is none gets one of the palette.
        const QString odd = library.addSection(notebook, QStringLiteral("Rara"), QStringLiteral("no-color"));
        QVERIFY(Library::sectionColors().contains(library.section(odd).value(QStringLiteral("color")).toString()));
        library.renameSection(work, QStringLiteral("Oficina"));
        library.setSectionColor(work, QStringLiteral("#34c759"));
        library.renameNotebook(notebook, QStringLiteral("Personal"));
        QCOMPARE(library.addSection(QStringLiteral("nope"), QStringLiteral("x")), QString());

        const QString page = library.addPage(work, QString(), QStringLiteral("Acta"));
        const QString child = library.addPage(work, page, QStringLiteral("Anexo"));
        library.movePage(page, first);
        QCOMPARE(titles(library.pages(first)), QStringList({QStringLiteral("Acta"), QStringLiteral(">Anexo")}));
        QCOMPARE(library.pages(work).size(), 0);
        QCOMPARE(library.page(child).value(QStringLiteral("section")).toString(), first);

        // Removing a section sends its pages to the trash; restoring finds them a home.
        library.movePage(page, work);
        QVERIFY(library.removeSection(work));
        QVERIFY(library.section(work).isEmpty());
        QCOMPARE(library.trashCount(), 2);
        library.restorePage(page);
        QCOMPARE(library.trashCount(), 0);
        QCOMPARE(titles(library.pages(first)), QStringList({QStringLiteral("Acta"), QStringLiteral(">Anexo")}));

        // The last notebook stays.
        QVERIFY(!library.removeNotebook(notebook));
        const QString second = library.addNotebook(QStringLiteral("Otro"));
        const QString inSecond = library.addSection(second, QStringLiteral("S"));
        library.addPage(inSecond, QString(), QStringLiteral("P"));
        QVERIFY(library.removeNotebook(second));
        QCOMPARE(library.notebooks().size(), 1);
        QCOMPARE(library.trashCount(), 1);

        Library again(dir.path());
        QCOMPARE(again.notebooks().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Personal"));
        QCOMPARE(again.notebooks().first().toMap().value(QStringLiteral("sections")).toList().size(), 2);
        QCOMPARE(again.trashCount(), 1);
        QCOMPARE(titles(again.pages(first)), QStringList({QStringLiteral("Acta"), QStringLiteral(">Anexo")}));
    }

    void trash()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString section = library.firstSection();
        const QString a = library.addPage(section, QString(), QStringLiteral("A"));
        tick();
        const QString a1 = library.addPage(section, a, QStringLiteral("A1"));
        tick();
        const QString b = library.addPage(section, QString(), QStringLiteral("B"));
        library.setPinned(a, true);
        // The subpage alone first: it comes back alone, under its page.
        library.trashPage(a1);
        QCOMPARE(library.pages(section).size(), 2);
        QCOMPARE(library.trashCount(), 1);
        library.restorePage(a1);
        QCOMPARE(titles(library.pages(section)), QStringList({QStringLiteral("A"), QStringLiteral(">A1"), QStringLiteral("B")}));
        // A page takes its subpages; what was already there is not brought back with it.
        const QString a2 = library.addPage(section, a, QStringLiteral("A2"));
        library.trashPage(a2);
        tick();
        library.trashPage(a);
        QCOMPARE(titles(library.pages(section)), QStringList{QStringLiteral("B")});
        QCOMPARE(titles(library.trash()).size(), 3);
        QCOMPARE(library.notebooks().first().toMap().value(QStringLiteral("sections")).toList().first().toMap().value(QStringLiteral("count")).toInt(), 1);
        QVERIFY(library.page(a).value(QStringLiteral("trashed")).toBool());
        library.restorePage(a);
        // What comes back from the trash is no longer pinned.
        QCOMPARE(titles(library.pages(section)), QStringList({QStringLiteral("B"), QStringLiteral("A"), QStringLiteral(">A1")}));
        QCOMPARE(library.trashCount(), 1);
        // A subpage restored while its page is in the trash is a page.
        library.trashPage(a);
        library.restorePage(a1);
        QCOMPARE(library.page(a1).value(QStringLiteral("parent")).toString(), QString());
        // For good: the directory goes too.
        QVERIFY(QDir(library.pageDir(a)).exists());
        library.emptyTrash();
        QCOMPARE(library.trashCount(), 0);
        QVERIFY(!QDir(library.pageDir(a)).exists());
        QVERIFY(!QDir(library.pageDir(a2)).exists());
        QVERIFY(QDir(library.pageDir(b)).exists());
        QVERIFY(!library.hasPage(a));
        Library again(dir.path());
        QCOMPARE(titles(again.pages(section)).size(), 2);
    }

    void searchAndTags()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString section = library.firstSection();
        const QString a = library.addPage(section, QString(), QStringLiteral("Reunión de planificación"));
        library.setDoc(a, words(QStringLiteral("Hablamos del presupuesto y del café.")));
        tick();
        const QString b = library.addPage(section, QString(), QStringLiteral("Recetas"));
        library.setDoc(b, words(QStringLiteral("Tarta de CAFÉ con una reunion de sabores")));
        tick();
        const QString c = library.addPage(section, QString(), QStringLiteral("Borrada"));
        library.setDoc(c, words(QStringLiteral("café")));
        library.trashPage(c);
        library.setTags(a, {QStringLiteral("trabajo")});
        library.setTags(b, {QStringLiteral("cocina"), QStringLiteral("trabajo")});

        QVariantList found = library.search(QStringLiteral("cafe"));
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.first().toMap().value(QStringLiteral("id")).toString(), b);
        QVERIFY(found.first().toMap().value(QStringLiteral("excerpt")).toString().contains(QStringLiteral("<b>CAFÉ</b>")));
        // In the title counts for more.
        found = library.search(QStringLiteral("REUNION"));
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.first().toMap().value(QStringLiteral("id")).toString(), a);
        QCOMPARE(found.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("<b>Reunión</b> de planificación"));
        // Words from the title and the text together.
        QCOMPARE(library.search(QStringLiteral("planificacion presupuesto")).size(), 1);
        QCOMPARE(library.search(QStringLiteral("cafe"), QStringLiteral("cocina")).size(), 1);
        QCOMPARE(library.search(QStringLiteral("nada de esto")).size(), 0);
        QCOMPARE(library.search(QStringLiteral("  ")).size(), 0);

        const QVariantList tags = library.tags();
        QCOMPARE(tags.size(), 2);
        QCOMPARE(tags.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("cocina"));
        QCOMPARE(tags.at(1).toMap().value(QStringLiteral("count")).toInt(), 2);
        QCOMPARE(library.pages(QString(), QStringLiteral("trabajo")).size(), 2);
        QCOMPARE(library.pages(section, QStringLiteral("cocina")).size(), 1);
        QCOMPARE(Library::parseTags(QStringLiteral("#Uno, dos;  Tres  cuatro,uno")), QStringList({QStringLiteral("uno"), QStringLiteral("dos"), QStringLiteral("tres cuatro")}));
    }

    void wikiLinks()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString section = library.firstSection();
        const QString target = library.addPage(section, QString(), QStringLiteral("Lista de la Compra"));
        const QString from = library.addPage(section, QString(), QStringLiteral("Semana"));
        Doc doc = words(QStringLiteral("ver [[lista de la compra]] hoy"));
        doc.linkWikiText();
        library.setDoc(from, doc);
        QCOMPARE(library.pageByTitle(QStringLiteral("lista de la compra")), target);
        QCOMPARE(library.pageByTitle(QStringLiteral("  LISTA de la cómpra ")), target);
        QCOMPARE(library.pageByTitle(QStringLiteral("no existe")), QString());
        const QVariantList back = library.backlinks(target);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back.first().toMap().value(QStringLiteral("id")).toString(), from);
        QCOMPARE(library.backlinks(from).size(), 0);
        library.trashPage(target);
        QCOMPARE(library.pageByTitle(QStringLiteral("lista de la compra")), QString());
    }

    void files()
    {
        QTemporaryDir dir;
        Library library(dir.path());
        const QString id = library.addPage(library.firstSection(), QString(), QStringLiteral("Con archivos"));
        QCOMPARE(library.addFile(id, QStringLiteral("foto.png"), "uno"), QStringLiteral("files/foto.png"));
        // The same again is the same file; another of the same name is another.
        QCOMPARE(library.addFile(id, QStringLiteral("foto.png"), "uno"), QStringLiteral("files/foto.png"));
        QCOMPARE(library.addFile(id, QStringLiteral("foto.png"), "dos"), QStringLiteral("files/foto-2.png"));
        // Names that would leave the directory, or hide.
        QCOMPARE(library.addFile(id, QStringLiteral("../../fuera.txt"), "x"), QStringLiteral("files/fuera.txt"));
        QCOMPARE(library.addFile(id, QStringLiteral(".oculto"), "x"), QStringLiteral("files/oculto"));
        QCOMPARE(library.addFile(id, QStringLiteral("a:b?.txt"), "x"), QStringLiteral("files/a_b_.txt"));
        QCOMPARE(library.addFile(QStringLiteral("nope"), QStringLiteral("a.txt"), "x"), QString());
        QFile source(dir.filePath(QStringLiteral("informe final.pdf")));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("%PDF");
        source.close();
        QCOMPARE(library.addFile(id, source.fileName()), QStringLiteral("files/informe final.pdf"));
        QCOMPARE(library.addFile(id, dir.filePath(QStringLiteral("no-existe"))), QString());
        QCOMPARE(QDir(library.pageDir(id) + QStringLiteral("/files")).entryList(QDir::Files).size(), 6);

        // A copy of the page has its own copies of the files.
        const QString copy = library.duplicatePage(id);
        QCOMPARE(library.page(copy).value(QStringLiteral("title")).toString(), QStringLiteral("Con archivos (copia)"));
        QCOMPARE(QDir(library.pageDir(copy) + QStringLiteral("/files")).entryList(QDir::Files).size(), 6);
    }

    void templates()
    {
        const QDate day(2026, 3, 10);
        QCOMPARE(Library::templateTitle(QStringLiteral("reunion"), day), QStringLiteral("Reunión del 10 de marzo"));
        QCOMPARE(Library::templateTitle(QStringLiteral("diario"), day), QStringLiteral("Martes, 10 de marzo de 2026"));
        QCOMPARE(Library::templateTitle(QStringLiteral("tareas"), day), QStringLiteral("Tareas"));
        QCOMPARE(Library::templateTitle(QString(), day), QString());
        const Doc meeting = Library::templateDoc(QStringLiteral("reunion"), day);
        QVERIFY(meeting.toText().contains(QStringLiteral("Fecha: 10 de marzo de 2026")));
        QVERIFY(meeting.toText().contains(QStringLiteral("Orden del día")));
        QCOMPARE(meeting.blocks.last().type, Block::Task);
        QCOMPARE(Library::templateDoc(QStringLiteral("tareas"), day).blocks.at(1).type, Block::Task);
        QCOMPARE(Library::templateDoc(QString(), day).blocks.size(), 1);

        QTemporaryDir dir;
        Library library(dir.path());
        const QString id = library.addPage(library.firstSection(), QString(), QString(), QStringLiteral("diario"));
        QCOMPARE(library.page(id).value(QStringLiteral("title")).toString(), QStringLiteral("Martes, 10 de marzo de 2026"));
        QVERIFY(library.doc(id) == Library::templateDoc(QStringLiteral("diario"), day));
        // The quick notes have their section, made once.
        const QString quick = library.quickSection();
        QCOMPARE(library.section(quick).value(QStringLiteral("name")).toString(), QStringLiteral("Notas rápidas"));
        QCOMPARE(library.quickSection(), quick);
    }

    void dates()
    {
        const QDateTime now(QDate(2026, 3, 10), QTime(12, 0));
        QCOMPARE(Library::dateText(now.addSecs(-3600), now), QStringLiteral("11:00"));
        QCOMPARE(Library::dateText(now.addDays(-1), now), QStringLiteral("Ayer"));
        QCOMPARE(Library::dateText(now.addDays(-3), now), QStringLiteral("sábado"));
        QCOMPARE(Library::dateText(now.addDays(-30), now), QStringLiteral("8 feb"));
        QCOMPARE(Library::dateText(now.addYears(-1), now), QStringLiteral("10/3/25"));
        QCOMPARE(Library::dateText(QDateTime(), now), QString());
    }
};

QTEST_GUILESS_MAIN(TestLibrary)
#include "tst_library.moc"
