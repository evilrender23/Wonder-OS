#include "exchange.h"
#include "ink.h"
#include "library.h"
#include "zip.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

static QString slurp(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

static void spit(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

static QByteArray png()
{
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

static QString types(const Doc &doc)
{
    QStringList names;
    for (const Block &block : doc.blocks) {
        names << Block::typeName(block.type) + (block.indent ? QString::number(block.indent) : QString()) + (block.done ? QStringLiteral("!") : QString());
    }
    return names.join(QLatin1Char(' '));
}

/// A page with a bit of everything; answers its id.
static QString rich(Library &library, const QString &section)
{
    const QString id = library.addPage(section, QString(), QStringLiteral("Plan de viaje"));
    const QString picture = library.addFile(id, QStringLiteral("foto.png"), png());
    const QString attached = library.addFile(id, QStringLiteral("billetes.txt"), "AB123");
    Doc doc;
    auto add = [&doc](Block::Type type, const Runs &runs, int indent = 0, bool done = false) {
        Block block;
        block.type = type;
        block.runs = runs;
        block.indent = indent;
        block.done = done;
        doc.blocks << block;
    };
    add(Block::Heading1, {Run{.text = QStringLiteral("Preparativos")}});
    add(Block::Paragraph, {Run{.text = QStringLiteral("Salimos el ")}, Run{.text = QStringLiteral("viernes"), .bold = true}, Run{.text = QStringLiteral(" por la ")},
                           Run{.text = QStringLiteral("mañana"), .italic = true}, Run{.text = QStringLiteral(".")}});
    add(Block::Bullet, {Run{.text = QStringLiteral("Maleta")}});
    add(Block::Bullet, {Run{.text = QStringLiteral("Cargador")}}, 1);
    add(Block::Number, {Run{.text = QStringLiteral("Tren")}});
    add(Block::Number, {Run{.text = QStringLiteral("Hotel")}});
    add(Block::Task, {Run{.text = QStringLiteral("Comprar billetes")}}, 0, true);
    add(Block::Task, {Run{.text = QStringLiteral("Reservar cena")}});
    add(Block::Quote, {Run{.text = QStringLiteral("Viajar es vivir")}});
    add(Block::Code, {Run{.text = QStringLiteral("ssh hotel")}});
    add(Block::Code, {Run{.text = QStringLiteral("  ls -la")}});
    Block table;
    table.type = Block::Table;
    table.rows << QList<Runs>{{Run{.text = QStringLiteral("Día")}}, {Run{.text = QStringLiteral("Plan")}}};
    table.rows << QList<Runs>{{Run{.text = QStringLiteral("Sábado")}}, {Run{.text = QStringLiteral("Museo")}}};
    doc.blocks << table;
    add(Block::Paragraph, {Run{.text = QStringLiteral("Web"), .href = QStringLiteral("https://example.org/viaje")}, Run{.text = QStringLiteral(" y ")},
                           Run{.text = QStringLiteral("Presupuesto"), .href = Doc::wikiHref(QStringLiteral("Presupuesto"))}});
    add(Block::Paragraph, {Run{.image = picture, .width = 40, .height = 20}});
    add(Block::Paragraph, {Run{.text = QStringLiteral("billetes.txt"), .href = attached}});
    library.setDoc(id, doc);
    return id;
}

static const QByteArray badManifest = R"({"format":"wonderland-notas","v":1,"name":"X","sections":[{"id":"s","name":"S","color":"#ff3b30"}],"pages":["p","../q","r"]})";
static const QByteArray badPage = R"({"id":"p","section":"s","title":"P","doc":{"blocks":[{"t":"p","runs":[{"img":"files/../../../fuera.png","w":10,"h":10},{"s":"adj","href":"files/../../library.json"}]}]}})";
static const QByteArray strayPage = R"({"id":"r","section":"no-such","title":"R","doc":{}})";

class TestExchange : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qputenv("WONDERLAND_NOTAS_NOW", "2026-03-10T09:00:00");
    }

    void formats()
    {
        QCOMPARE(Exchange::formatOf(QStringLiteral("/a/b/Nota.MD")), QStringLiteral("md"));
        QCOMPARE(Exchange::formatOf(QStringLiteral("x.markdown")), QStringLiteral("md"));
        QCOMPARE(Exchange::formatOf(QStringLiteral("x.htm")), QStringLiteral("html"));
        QCOMPARE(Exchange::formatOf(QStringLiteral("x.txt")), QStringLiteral("txt"));
        QCOMPARE(Exchange::formatOf(QStringLiteral("x.pdf")), QStringLiteral("pdf"));
        QCOMPARE(Exchange::formatOf(QStringLiteral("x.docx")), QString());
        QCOMPARE(Exchange::fileName(QStringLiteral("  a/b: c?  ")), QStringLiteral("a-b- c-"));
        QCOMPARE(Exchange::fileName(QStringLiteral("..")), QStringLiteral("Sin título"));
    }

    void readsMarkdown()
    {
        const Exchange::Read read = Exchange::fromMarkdown(QStringLiteral(
            "---\ntitle: x\n---\n# Receta\r\n\r\nUn **párrafo** con *cursiva*, `código`, ~~tachado~~ y [enlace](https://example.org).\n\n"
            "## Ingredientes\n\n- harina\n  - integral\n- agua\n\n1. mezclar\n2. hornear\n\n- [x] comprar\n- [ ] probar\n\n"
            "> una cita\n\n```\nint x;\n  y();\n```\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\nVer [[Otra página]].\n"));
        QVERIFY(read.ok);
        QCOMPARE(read.title, QStringLiteral("Receta"));
        QCOMPARE(types(read.doc), QStringLiteral("p h1 bullet bullet1 bullet number number task! task quote code code table p"));
        const Runs runs = read.doc.blocks.first().runs;
        QVERIFY(runs.at(1).bold);
        QCOMPARE(runs.at(1).text, QStringLiteral("párrafo"));
        QVERIFY(runs.at(3).italic);
        QVERIFY(runs.at(5).mono);
        QVERIFY(runs.at(7).strike);
        QCOMPARE(runs.at(9).href, QStringLiteral("https://example.org"));
        QCOMPARE(read.doc.blocks.at(11).runs.first().text, QStringLiteral("  y();"));
        QCOMPARE(read.doc.blocks.at(12).rows.at(1).at(1).first().text, QStringLiteral("2"));
        QCOMPARE(Doc::wikiTitle(read.doc.blocks.last().runs.at(1).href), QStringLiteral("Otra página"));
        // No title: none is made up.
        const Exchange::Read plain = Exchange::fromMarkdown(QStringLiteral("solo texto\n\n## Y un apartado\n"));
        QCOMPARE(plain.title, QString());
        QCOMPARE(types(plain.doc), QStringLiteral("p h2"));
    }

    void readsHtmlAndText()
    {
        const Exchange::Read html = Exchange::fromHtml(QStringLiteral("<html><head><title>Del título</title></head><body><p>Hola <b>mundo</b></p><ul><li>uno</li></ul></body></html>"));
        QCOMPARE(html.title, QStringLiteral("Del título"));
        QCOMPARE(types(html.doc), QStringLiteral("p bullet"));
        const Exchange::Read headed = Exchange::fromHtml(QStringLiteral("<h1>Arriba</h1><h2>Dentro</h2><p>x</p>"));
        QCOMPARE(headed.title, QStringLiteral("Arriba"));
        QCOMPARE(types(headed.doc), QStringLiteral("h1 p"));
        const Exchange::Read text = Exchange::fromText(QStringLiteral("\n\nTítulo de la nota\n\nlínea uno\nlínea dos\n"));
        QCOMPARE(text.title, QStringLiteral("Título de la nota"));
        QCOMPARE(text.doc.toText().trimmed(), QStringLiteral("línea uno\nlínea dos"));
        QCOMPARE(Exchange::fromText(QString()).title, QString());
    }

    void markdownThereAndBack()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString id = rich(library, library.firstSection());
        const QString path = out.filePath(QStringLiteral("Plan de viaje.md"));
        QVERIFY(Exchange::exportPage(&library, id, path));
        const QString md = slurp(path);
        QVERIFY(md.startsWith(QStringLiteral("# Plan de viaje\n\n## Preparativos\n\nSalimos el **viernes** por la *mañana*.\n")));
        QVERIFY(md.contains(QStringLiteral("- [x] Comprar billetes\n- [ ] Reservar cena\n")));
        QVERIFY(md.contains(QStringLiteral("![imagen](Plan%20de%20viaje_archivos/foto.png)")));
        QVERIFY(md.contains(QStringLiteral("[[Presupuesto]]")));
        QCOMPARE(slurp(out.filePath(QStringLiteral("Plan de viaje_archivos/billetes.txt"))), QStringLiteral("AB123"));
        QVERIFY(QFile::exists(out.filePath(QStringLiteral("Plan de viaje_archivos/foto.png"))));

        const QString back = Exchange::importFile(&library, path, library.firstSection());
        QVERIFY(!back.isEmpty());
        QCOMPARE(library.info(back).title, QStringLiteral("Plan de viaje"));
        const Doc was = library.doc(id);
        const Doc is = library.doc(back);
        QCOMPARE(types(is), types(was));
        QCOMPARE(is.toText(), was.toText());
        for (int i = 0; i < was.blocks.size(); ++i) {
            QVERIFY2(is.blocks.at(i) == was.blocks.at(i), qPrintable(QStringLiteral("block %1 (%2)").arg(i).arg(Block::typeName(was.blocks.at(i).type))));
        }
        // The files came into the new page.
        QCOMPARE(slurp(library.pageDir(back) + QStringLiteral("/files/billetes.txt")), QStringLiteral("AB123"));
        QVERIFY(QFile::exists(library.pageDir(back) + QStringLiteral("/files/foto.png")));
    }

    void htmlThereAndBack()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString id = rich(library, library.firstSection());
        const QString path = out.filePath(QStringLiteral("plan.html"));
        QVERIFY(Exchange::exportPage(&library, id, path));
        const QString html = slurp(path);
        QVERIFY(html.contains(QStringLiteral("<h1>Plan de viaje</h1>")));
        QVERIFY(html.contains(QStringLiteral("<img src=\"plan_archivos/foto.png\"")));
        const QString back = Exchange::importFile(&library, path, library.firstSection());
        QVERIFY(!back.isEmpty());
        QCOMPARE(library.info(back).title, QStringLiteral("Plan de viaje"));
        const Doc was = library.doc(id);
        const Doc is = library.doc(back);
        QCOMPARE(types(is), types(was));
        QCOMPARE(is.toText(), was.toText());
        QCOMPARE(is.files(), was.files());
    }

    void text()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString id = rich(library, library.firstSection());
        const QString path = out.filePath(QStringLiteral("plan.txt"));
        QVERIFY(Exchange::exportPage(&library, id, path));
        const QString text = slurp(path);
        QVERIFY(text.startsWith(QStringLiteral("Plan de viaje\n\nPreparativos\nSalimos el viernes por la mañana.\n• Maleta\n")));
        const QString back = Exchange::importFile(&library, path, library.firstSection());
        QCOMPARE(library.info(back).title, QStringLiteral("Plan de viaje"));
        QVERIFY(library.doc(back).toText().startsWith(QStringLiteral("Preparativos\nSalimos")));
        // What it cannot read, it says.
        QCOMPARE(Exchange::importFile(&library, out.filePath(QStringLiteral("no-existe.md")), library.firstSection()), QString());
        spit(out.filePath(QStringLiteral("x.docx")), "PK");
        QCOMPARE(Exchange::importFile(&library, out.filePath(QStringLiteral("x.docx")), library.firstSection()), QString());
        QVERIFY(!Exchange::exportPage(&library, QStringLiteral("nope"), out.filePath(QStringLiteral("y.md"))));
        QVERIFY(!Exchange::exportPage(&library, id, out.filePath(QStringLiteral("y.docx"))));
    }

    void importKeepsOutOfTheWay()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        spit(out.filePath(QStringLiteral("secreto.txt")), "no");
        spit(out.filePath(QStringLiteral("notas/dentro.png")), png());
        spit(out.filePath(QStringLiteral("notas/n.md")), "# N\n\n![](dentro.png) ![b](https://example.org/x.png) ![c](falta.png)\n\n[fuera](../secreto.txt) [web](https://example.org) [nada](no-esta.pdf)\n");
        const QString id = Exchange::importFile(&library, out.filePath(QStringLiteral("notas/n.md")), library.firstSection());
        const Doc doc = library.doc(id);
        // The picture beside it comes in, with its size; the remote and the missing ones are dropped.
        QStringList images;
        QStringList links;
        for (const Block &block : doc.blocks) {
            for (const Run &run : block.runs) {
                if (run.isImage()) {
                    images << QStringLiteral("%1 %2x%3").arg(run.image).arg(run.width).arg(run.height);
                } else if (!run.href.isEmpty()) {
                    links << run.href;
                }
            }
        }
        QCOMPARE(images, QStringList{QStringLiteral("files/dentro.png 40x20")});
        // A file it links to is copied in, wherever it was; the web stays the web.
        QCOMPARE(links, QStringList({QStringLiteral("files/secreto.txt"), QStringLiteral("https://example.org"), QStringLiteral("no-esta.pdf")}));
        // Nothing was written outside the page.
        QCOMPARE(QDir(library.pageDir(id) + QStringLiteral("/files")).entryList(QDir::Files, QDir::Name), QStringList({QStringLiteral("dentro.png"), QStringLiteral("secreto.txt")}));
    }

    void folder()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString notebook = library.notebooks().first().toMap().value(QStringLiteral("id")).toString();
        spit(out.filePath(QStringLiteral("Recetas/b.md")), "# Bizcocho\n\n- harina\n");
        spit(out.filePath(QStringLiteral("Recetas/a.md")), "Sin título propio\n");
        spit(out.filePath(QStringLiteral("Recetas/c.markdown")), "# Crema\n");
        spit(out.filePath(QStringLiteral("Recetas/otro.txt")), "no es markdown");
        int count = 0;
        const QString section = Exchange::importFolder(&library, out.filePath(QStringLiteral("Recetas")), notebook, &count);
        QVERIFY(!section.isEmpty());
        QCOMPARE(count, 3);
        QCOMPARE(library.section(section).value(QStringLiteral("name")).toString(), QStringLiteral("Recetas"));
        QStringList titles;
        for (const QString &id : library.pageIds(section)) {
            titles << library.info(id).title;
        }
        titles.sort();
        QCOMPARE(titles, QStringList({QStringLiteral("Bizcocho"), QStringLiteral("Crema"), QStringLiteral("a")}));
        QCOMPARE(Exchange::importFolder(&library, out.filePath(QStringLiteral("vacía")), notebook, &count), QString());
        QCOMPARE(count, 0);
    }

    void pdf()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString section = library.firstSection();
        const QString id = rich(library, section);
        const QString long1 = library.addPage(section, QString(), QStringLiteral("Larga"));
        library.setDoc(long1, Doc::fromText(QStringLiteral("línea de texto\n").repeated(140)));
        const QString one = out.filePath(QStringLiteral("una.pdf"));
        QVERIFY(Exchange::exportPage(&library, id, one));
        const QString all = out.filePath(QStringLiteral("sección.pdf"));
        QCOMPARE(Exchange::exportSection(&library, section, all, QStringLiteral("pdf")), 2);
        auto sheets = [](const QString &path) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                return -1;
            }
            const QByteArray bytes = file.readAll();
            if (!bytes.startsWith("%PDF-")) {
                return -2;
            }
            // Page objects, not the «/Pages» tree.
            int count = 0;
            for (qsizetype at = bytes.indexOf("/Type /Page"); at >= 0; at = bytes.indexOf("/Type /Page", at + 1)) {
                if (bytes.at(at + 11) != 's') {
                    ++count;
                }
            }
            return count;
        };
        QCOMPARE(sheets(one), 1);
        // The long page takes more than a sheet; each page starts on its own.
        QVERIFY2(sheets(all) >= 4, qPrintable(QString::number(sheets(all))));
        QVERIFY(!QFile::exists(all + QStringLiteral(".part")));
    }

    void sectionAsFiles()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        Library library(data.path());
        const QString section = library.firstSection();
        rich(library, section);
        library.addPage(section, QString(), QStringLiteral("Dos/Puntos"));
        library.addPage(section, QString(), QStringLiteral("dos-puntos"));
        const QString trashed = library.addPage(section, QString(), QStringLiteral("Borrada"));
        library.trashPage(trashed);
        const QString dir = out.filePath(QStringLiteral("salida"));
        QCOMPARE(Exchange::exportSection(&library, section, dir, QStringLiteral("md")), 3);
        QStringList names = QDir(dir).entryList(QDir::Files, QDir::Name);
        QCOMPARE(names.size(), 3);
        QVERIFY(names.contains(QStringLiteral("Plan de viaje.md")));
        QVERIFY(QDir(dir + QStringLiteral("/Plan de viaje_archivos")).exists());
        QCOMPARE(Exchange::exportSection(&library, section, dir, QStringLiteral("docx")), 0);
        QCOMPARE(Exchange::exportSection(&library, QStringLiteral("nope"), dir, QStringLiteral("md")), 0);
    }

    void notebookArchive()
    {
        QTemporaryDir data;
        QTemporaryDir other;
        QTemporaryDir out;
        const QString path = out.filePath(QStringLiteral("cuaderno.wnotas"));
        QString id;
        Doc doc;
        {
            Library library(data.path());
            const QString notebook = library.notebooks().first().toMap().value(QStringLiteral("id")).toString();
            library.renameNotebook(notebook, QStringLiteral("Viajes"));
            const QString first = library.firstSection();
            const QString second = library.addSection(notebook, QStringLiteral("Ideas"), QStringLiteral("#ff9f0a"));
            id = rich(library, first);
            library.setTags(id, {QStringLiteral("viaje")});
            library.setPinned(id, true);
            const QString child = library.addPage(first, id, QStringLiteral("Anexo"));
            library.setDoc(child, Doc::fromText(QStringLiteral("texto del anexo")));
            library.addPage(second, QString(), QStringLiteral("Idea"));
            const QString gone = library.addPage(second, QString(), QStringLiteral("Borrada"));
            library.trashPage(gone);
            // A drawing, and a file the page no longer uses.
            Drawing drawing;
            drawing.strokes << Stroke{QStringLiteral("pen"), QStringLiteral("#ff3b30"), 3, {QPointF(10, 10), QPointF(80, 60)}};
            library.addFile(id, QStringLiteral("suelto.bin"), "nadie lo usa");
            doc = library.doc(id);
            Block block;
            block.runs << Run{.image = library.addFile(id, QStringLiteral("dibujo.svg"), drawing.toSvg()), .width = 720, .height = 320};
            doc.blocks << block;
            library.setDoc(id, doc);
            QVERIFY(Exchange::exportNotebook(&library, notebook, path));
            QVERIFY(!Exchange::exportNotebook(&library, QStringLiteral("nope"), path));
        }
        const ZipReader zip = ZipReader::open(path);
        QVERIFY(zip.valid());
        QCOMPARE(zip.names().first(), QStringLiteral("mimetype"));
        QCOMPARE(zip.file(QStringLiteral("mimetype")), QByteArray("application/x-wonderland-notas"));
        QVERIFY(zip.contains(QStringLiteral("pages/%1/files/dibujo.svg").arg(id)));
        QVERIFY(!zip.contains(QStringLiteral("pages/%1/files/suelto.bin").arg(id)));
        QCOMPARE(zip.names().filter(QStringLiteral("page.json")).size(), 3);

        qputenv("WONDERLAND_NOTAS_NOW", "2026-06-01T12:00:00");
        Library library(other.path());
        int count = 0;
        const QString notebook = Exchange::importNotebook(&library, path, &count);
        qputenv("WONDERLAND_NOTAS_NOW", "2026-03-10T09:00:00");
        QVERIFY(!notebook.isEmpty());
        QCOMPARE(count, 3);
        QCOMPARE(library.notebooks().size(), 2);
        const QVariantMap imported = library.notebook(notebook);
        QCOMPARE(imported.value(QStringLiteral("name")).toString(), QStringLiteral("Viajes"));
        const QVariantList sections = library.notebooks().last().toMap().value(QStringLiteral("sections")).toList();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.last().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Ideas"));
        QCOMPARE(sections.last().toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#ff9f0a"));
        const QVariantList rows = library.pages(sections.first().toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(rows.size(), 2);
        const QVariantMap top = rows.first().toMap();
        QCOMPARE(top.value(QStringLiteral("title")).toString(), QStringLiteral("Plan de viaje"));
        QCOMPARE(top.value(QStringLiteral("pinned")).toBool(), true);
        QCOMPARE(top.value(QStringLiteral("tags")).toStringList(), QStringList{QStringLiteral("viaje")});
        QCOMPARE(rows.last().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Anexo"));
        QCOMPARE(rows.last().toMap().value(QStringLiteral("depth")).toInt(), 1);
        const QString newId = top.value(QStringLiteral("id")).toString();
        QVERIFY(newId != id);
        QVERIFY(library.doc(newId) == doc);
        // The date it had, not the date it arrived.
        QCOMPARE(library.info(newId).modified, QDateTime(QDate(2026, 3, 10), QTime(9, 0)));
        bool ok = false;
        QFile svg(library.pageDir(newId) + QStringLiteral("/files/dibujo.svg"));
        QVERIFY(svg.open(QIODevice::ReadOnly));
        QCOMPARE(Drawing::fromSvg(svg.readAll(), &ok).strokes.size(), 1);
        QVERIFY(ok);

        // Not an archive of ours.
        ZipWriter stranger;
        stranger.add(QStringLiteral("notebook.json"), "{\"format\":\"otra-cosa\"}");
        QVERIFY(stranger.save(out.filePath(QStringLiteral("otro.wnotas"))));
        QCOMPARE(Exchange::importNotebook(&library, out.filePath(QStringLiteral("otro.wnotas"))), QString());
        spit(out.filePath(QStringLiteral("roto.wnotas")), "no es un zip");
        QCOMPARE(Exchange::importNotebook(&library, out.filePath(QStringLiteral("roto.wnotas"))), QString());
        QCOMPARE(library.notebooks().size(), 2);
    }

    void archiveCannotEscape()
    {
        QTemporaryDir data;
        QTemporaryDir out;
        // A page whose picture claims to live outside its directory.
        ZipWriter zip;
        zip.add(QStringLiteral("mimetype"), "application/x-wonderland-notas");
        zip.add(QStringLiteral("notebook.json"), badManifest);
        zip.add(QStringLiteral("pages/p/page.json"),
                badPage);
        zip.add(QStringLiteral("pages/p/files/../../../fuera.png"), png());
        zip.add(QStringLiteral("pages/r/page.json"), strayPage);
        const QString path = out.filePath(QStringLiteral("malo.wnotas"));
        QVERIFY(zip.save(path));
        Library library(data.path());
        int count = 0;
        const QString notebook = Exchange::importNotebook(&library, path, &count);
        QVERIFY(!notebook.isEmpty());
        QCOMPARE(count, 1);
        const QString section = library.notebooks().last().toMap().value(QStringLiteral("sections")).toList().first().toMap().value(QStringLiteral("id")).toString();
        const QString id = library.pageIds(section).first();
        // The picture is dropped; nothing lands outside the page.
        QVERIFY(library.doc(id).files().filter(QStringLiteral("fuera")).isEmpty());
        QVERIFY(!QFile::exists(data.filePath(QStringLiteral("fuera.png"))));
        QVERIFY(!QFile::exists(data.filePath(QStringLiteral("pages/fuera.png"))));
        QVERIFY(!QDir(library.pageDir(id) + QStringLiteral("/files")).exists() || QDir(library.pageDir(id) + QStringLiteral("/files")).entryList(QDir::Files).isEmpty());
    }
};

QTEST_MAIN(TestExchange)
#include "tst_exchange.moc"
