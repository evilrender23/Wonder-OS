#include "describe.h"
#include "docx.h"
#include "editor.h"
#include "zip.h"

#include <QDomDocument>
#include <QTemporaryDir>
#include <QTest>

class TestDocx : public QObject
{
    Q_OBJECT

    static QByteArray wordFile(const QByteArray &body, const QByteArray &numbering = QByteArray())
    {
        ZipWriter zip;
        zip.add(QStringLiteral("[Content_Types].xml"),
                "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>");
        zip.add(QStringLiteral("_rels/.rels"),
                "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
        zip.add(QStringLiteral("word/_rels/document.xml.rels"),
                "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                "<Relationship Id=\"rId7\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"https://example.org/\" TargetMode=\"External\"/>"
                "</Relationships>");
        if (!numbering.isEmpty()) {
            zip.add(QStringLiteral("word/numbering.xml"), numbering);
        }
        zip.add(QStringLiteral("word/document.xml"),
                "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><w:body>" + body + "</w:body></w:document>");
        return zip.finish();
    }

private Q_SLOTS:
    void roundTripsTheSample()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QByteArray bytes = Docx::toBytes(editor.document(), editor.setup());
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QString error;
        QVERIFY2(Docx::fromBytes(bytes, &back, &setup, &error), qPrintable(error));
        COMPARE_LINES(describe(&back), describe(editor.document()));
        QCOMPARE(setup, editor.setup());
        // And again: nothing drifts.
        QCOMPARE(Docx::toBytes(&back, setup).size(), bytes.size());
    }

    void roundTripsTheTemplatesAndTheSetup()
    {
        Editor editor;
        for (const QString &kind : {QStringLiteral("blank"), QStringLiteral("letter"), QStringLiteral("resume"), QStringLiteral("report")}) {
            editor.newDocument(kind);
            editor.setSetup({{QStringLiteral("paper"), QStringLiteral("Carta")}, {QStringLiteral("landscape"), true}, {QStringLiteral("left"), 3.0},
                             {QStringLiteral("top"), 1.5}, {QStringLiteral("header"), QStringLiteral("Cabecera & <más>")}, {QStringLiteral("footer"), QStringLiteral("Pie")}});
            QTextDocument back;
            Doc::prepare(&back);
            Doc::PageSetup setup;
            QVERIFY(Docx::fromBytes(Docx::toBytes(editor.document(), editor.setup()), &back, &setup));
            COMPARE_LINES(describe(&back), describe(editor.document()));
            QCOMPARE(setup, editor.setup());
        }
    }

    void roundTripsParagraphFormatsAndNestedLists()
    {
        Editor editor;
        editor.insertText(QStringLiteral("uno\ndos\ntres\ncuatro\ncinco"));
        editor.selectAll();
        editor.setList(2);
        editor.goTo(5);
        editor.indentMore();
        editor.goTo(9);
        editor.indentMore();
        editor.indentMore();
        editor.goTo(21);
        editor.setList(0);
        editor.setAlignment(QStringLiteral("right"));
        editor.setLineHeight(200);
        editor.setSpacing(12, 24);
        editor.setIndents(2, 1, -0.5);
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QVERIFY(Docx::fromBytes(Docx::toBytes(editor.document(), editor.setup()), &back, &setup));
        const QStringList lines = describe(&back);
        COMPARE_LINES(lines, describe(editor.document()));
        QVERIFY2(lines.at(1).contains(QStringLiteral("number2#1")), qPrintable(lines.at(1)));
        QVERIFY2(lines.at(2).contains(QStringLiteral("number3#1")), qPrintable(lines.at(2)));
        QVERIFY2(lines.at(3).contains(QStringLiteral("number1#2")), qPrintable(lines.at(3)));
        const QTextBlockFormat format = back.lastBlock().blockFormat();
        QCOMPARE(qRound(format.lineHeight()), 200);
        QCOMPARE(qRound(format.topMargin() / Doc::pxPerPt), 12);
        QCOMPARE(qRound(format.bottomMargin() / Doc::pxPerPt), 24);
        QCOMPARE(qRound(format.leftMargin() / Doc::pxPerCm * 10), 20);
        QCOMPARE(qRound(format.rightMargin() / Doc::pxPerCm * 10), 10);
        QCOMPARE(qRound(format.textIndent() / Doc::pxPerCm * 10), -5);
    }

    void isAPackageWordCanOpen()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const ZipReader zip(Docx::toBytes(editor.document(), editor.setup()));
        QVERIFY(zip.isValid());
        QCOMPARE(zip.names().first(), QStringLiteral("[Content_Types].xml"));
        for (const char *part : {"_rels/.rels", "word/document.xml", "word/styles.xml", "word/numbering.xml", "word/_rels/document.xml.rels"}) {
            QVERIFY2(zip.contains(QLatin1String(part)), part);
        }
        // Every part is well-formed XML, and every relationship has its target.
        for (const QString &name : zip.names()) {
            if (name.endsWith(QLatin1String(".xml")) || name.endsWith(QLatin1String(".rels"))) {
                QDomDocument dom;
                const auto result = dom.setContent(zip.file(name), QDomDocument::ParseOption::UseNamespaceProcessing);
                QVERIFY2(bool(result), qPrintable(name + QStringLiteral(": ") + result.errorMessage));
            }
        }
        QDomDocument rels;
        rels.setContent(zip.file(QStringLiteral("word/_rels/document.xml.rels")));
        const QDomNodeList list = rels.elementsByTagName(QStringLiteral("Relationship"));
        QVERIFY(list.size() >= 4);
        for (int i = 0; i < list.size(); ++i) {
            const QDomElement relationship = list.at(i).toElement();
            if (relationship.attribute(QStringLiteral("TargetMode")) != QLatin1String("External")) {
                QVERIFY2(zip.contains(QStringLiteral("word/") + relationship.attribute(QStringLiteral("Target"))), qPrintable(relationship.attribute(QStringLiteral("Target"))));
            }
        }
        const QByteArray body = zip.file(QStringLiteral("word/document.xml"));
        QVERIFY(body.contains("<w:pStyle w:val=\"Heading1\"/>"));
        QVERIFY(body.contains("<w:tbl>"));
        QVERIFY(body.contains("<w:hyperlink "));
        QVERIFY(body.contains("<w:drawing>"));
        QVERIFY(body.contains("<w:sectPr>"));
    }

    void readsWhatWordWrites()
    {
        // The vocabulary Word uses, which Textos does not write the same way.
        const QByteArray numbering =
            "<?xml version=\"1.0\"?><w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\xef\x82\xb7\"/></w:lvl>"
            "<w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"o\"/></w:lvl></w:abstractNum>"
            "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\"><w:start w:val=\"1\"/><w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%1.\"/></w:lvl></w:abstractNum>"
            "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num><w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num></w:numbering>";
        const QByteArray body =
            "<w:p><w:pPr><w:pStyle w:val=\"Title\"/><w:jc w:val=\"center\"/></w:pPr><w:r><w:t>Un t\xc3\xadtulo</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:pStyle w:val=\"Heading2\"/></w:pPr><w:r><w:t>Secci\xc3\xb3n</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:jc w:val=\"both\"/></w:pPr><w:r><w:t xml:space=\"preserve\">Texto </w:t></w:r>"
            "<w:r><w:rPr><w:b/><w:i/><w:color w:val=\"FF0000\"/><w:sz w:val=\"28\"/></w:rPr><w:t>fuerte</w:t></w:r>"
            "<w:r><w:rPr><w:b w:val=\"0\"/><w:u w:val=\"single\"/><w:strike/><w:highlight w:val=\"yellow\"/></w:rPr><w:t xml:space=\"preserve\"> raro</w:t></w:r>"
            "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:t>2</w:t></w:r>"
            "<w:r><w:tab/><w:t>tras</w:t><w:br/><w:t>salto</w:t></w:r>"
            "<w:hyperlink r:id=\"rId7\"><w:r><w:rPr><w:rStyle w:val=\"Hyperlink\"/></w:rPr><w:t>enlace</w:t></w:r></w:hyperlink></w:p>"
            "<w:p><w:pPr><w:pStyle w:val=\"ListParagraph\"/><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>vi\xc3\xb1" "eta</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>dentro</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr></w:pPr><w:r><w:t>primero</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr></w:pPr><w:r><w:t>segundo</w:t></w:r></w:p>"
            "<w:tbl><w:tblPr><w:tblW w:w=\"0\" w:type=\"auto\"/></w:tblPr><w:tblGrid><w:gridCol w:w=\"4000\"/><w:gridCol w:w=\"4000\"/></w:tblGrid>"
            "<w:tr><w:tc><w:p><w:r><w:t>a</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc></w:tr>"
            "<w:tr><w:tc><w:p><w:r><w:t>c</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>d</w:t></w:r></w:p><w:p><w:r><w:t>e</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
            "<w:p><w:r><w:br w:type=\"page\"/></w:r><w:r><w:t>otra hoja</w:t></w:r></w:p>"
            "<w:sectPr><w:pgSz w:w=\"12240\" w:h=\"15840\"/><w:pgMar w:top=\"1440\" w:right=\"1800\" w:bottom=\"1440\" w:left=\"1800\"/></w:sectPr>";
        QTextDocument document;
        Doc::prepare(&document);
        Doc::PageSetup setup;
        QString error;
        QVERIFY2(Docx::fromBytes(wordFile(body, numbering), &document, &setup, &error), qPrintable(error));
        const QStringList lines = describe(&document, false);
        const QString all = lines.join(QLatin1Char('\n'));
        QVERIFY2(lines.at(0).startsWith(QStringLiteral("title center | [Un título")), qPrintable(all));
        QVERIFY2(lines.at(1).startsWith(QStringLiteral("h2 left | [Sección")), qPrintable(all));
        QVERIFY2(lines.at(2).startsWith(QStringLiteral("body justify | [Texto  ----0 #1d1d1f] [fuerte bi--0 #ff0000] [ raro --us0 #1d1d1f bg#ffff00] [2 ----1 #1d1d1f] [\ttras\u2028salto ----0 #1d1d1f] [enlace ")), qPrintable(all));
        QVERIFY2(lines.at(2).endsWith(QStringLiteral("->https://example.org/]")), qPrintable(all));
        QVERIFY2(lines.at(3).contains(QStringLiteral("bullet1#1 | [viñeta")), qPrintable(all));
        QVERIFY2(lines.at(4).contains(QStringLiteral("bullet2#1 | [dentro")), qPrintable(all));
        QVERIFY2(lines.at(5).contains(QStringLiteral("number1#1 | [primero")), qPrintable(all));
        QVERIFY2(lines.at(6).contains(QStringLiteral("number1#2 | [segundo")), qPrintable(all));
        QVERIFY2(lines.at(7).contains(QStringLiteral("cell0,0of2x2 | [a")), qPrintable(all));
        QVERIFY2(lines.at(10).contains(QStringLiteral("cell1,1of2x2 | [d")), qPrintable(all));
        QVERIFY2(lines.at(11).contains(QStringLiteral("cell1,1of2x2 | [e")), qPrintable(all));
        QVERIFY2(lines.last().contains(QStringLiteral(" break | [otra hoja")), qPrintable(all));
        QCOMPARE(Doc::lookOf(document.findBlockByNumber(2).begin().fragment().charFormat()).size, 11.0);
        QCOMPARE(setup.paper, QStringLiteral("Carta"));
        QVERIFY(!setup.landscape);
        QCOMPARE(setup.top, 2.54);
        QCOMPARE(setup.left, 3.18);
    }

    void rejectsWhatIsNotWord()
    {
        QTextDocument document;
        Doc::prepare(&document);
        Doc::PageSetup setup;
        QString error;
        QVERIFY(!Docx::fromBytes("no es un zip", &document, &setup, &error));
        QVERIFY(!error.isEmpty());
        ZipWriter zip;
        zip.add(QStringLiteral("hola.txt"), "hola");
        QVERIFY(!Docx::fromBytes(zip.finish(), &document, &setup, &error));
        ZipWriter broken;
        broken.add(QStringLiteral("word/document.xml"), "<w:document><w:body>");
        QVERIFY(!Docx::fromBytes(broken.finish(), &document, &setup, &error));
        QVERIFY(document.isEmpty());
    }

    void libreOfficeReadsItAndWritesWhatTextosReads()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("soffice")).isEmpty()) {
            QSKIP("LibreOffice is not installed");
        }
        QTemporaryDir dir;
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QString ours = dir.filePath(QStringLiteral("muestra.docx"));
        QVERIFY(Docx::write(ours, editor.document(), editor.setup()));

        // What LibreOffice makes of it, as text…
        const QString text = sofficeConvert(ours, QStringLiteral("txt:Text (encoded):UTF8"), dir.path());
        QVERIFY2(!text.isEmpty(), "LibreOffice did not convert the document");
        QFile file(text);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString read = QString::fromUtf8(file.readAll());
        for (const char *wanted : {"El jardín de Wonderland", "negrita", "example.org", "Rosas blancas, pintadas de rojo", "Podar antes del té", "Empieza por el principio",
                                   "Primavera", "pintar(carta, \"rojo\")", "Segunda página"}) {
            QVERIFY2(read.contains(QString::fromUtf8(wanted)), wanted);
        }

        // …and written back by it as a .docx of its own, which Textos reads.
        QDir().mkpath(dir.filePath(QStringLiteral("vuelta")));
        const QString theirs = sofficeConvert(ours, QStringLiteral("docx:MS Word 2007 XML"), dir.filePath(QStringLiteral("vuelta")));
        QVERIFY2(!theirs.isEmpty(), "LibreOffice did not write a .docx");
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QString error;
        QVERIFY2(Docx::read(theirs, &back, &setup, &error), qPrintable(error));
        const QStringList lines = describe(&back, false);
        const QString all = lines.join(QLatin1Char('\n'));
        for (const char *wanted : {"title left | [El jardín de Wonderland b---0", "[negrita b---0", "[cursiva -i--0", "[subrayado --u-0", "[tachado ---s0", "[de color ----0 #d70015",
                                   "[resaltado ----0 #1d1d1f bg#ffe066", "[2 ----1", "[2 ----2", "->https://example.org/]", "h1 left | [Lo que crece", "bullet1#3 | [Un árbol con puerta",
                                   "h2 left | [Orden de las tareas", "number1#3 | [Contar las cartas de la baraja", "cell0,0of3x3 | [Planta b---0", "cell2,2of3x3 | [Otoño ----0",
                                   "h3 left | [Una figura", "[image 360x140 of 360x140]", "h1 left break | [Segunda página", "body justify | [Este párrafo empieza"}) {
            QVERIFY2(all.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + all));
        }
        QCOMPARE(setup.paper, QStringLiteral("A4"));
        QCOMPARE(setup.left, 2.5);
        QCOMPARE(setup.header, QStringLiteral("El jardín de Wonderland"));
        QVERIFY(setup.pageNumbers);
    }
};

QTEST_MAIN(TestDocx)
#include "tst_docx.moc"
