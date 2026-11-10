#include "describe.h"
#include "editor.h"
#include "formats.h"

#include <QTemporaryDir>
#include <QTest>

class TestFormats : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kinds()
    {
        QCOMPARE(Formats::kindOf(QStringLiteral("/a/b.DOCX")), Formats::Kind::Docx);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.odt")), Formats::Kind::Odt);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.md")), Formats::Kind::Markdown);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.markdown")), Formats::Kind::Markdown);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.htm")), Formats::Kind::Html);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.rtf")), Formats::Kind::Rtf);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.pdf")), Formats::Kind::Pdf);
        QCOMPARE(Formats::kindOf(QStringLiteral("b.txt")), Formats::Kind::Text);
        QCOMPARE(Formats::kindOf(QStringLiteral("LEEME")), Formats::Kind::Unknown);
        QVERIFY(Formats::keepsFormatting(Formats::Kind::Docx));
        QVERIFY(Formats::keepsFormatting(Formats::Kind::Odt));
        QVERIFY(Formats::keepsFormatting(Formats::Kind::Rtf));
        QVERIFY(!Formats::keepsFormatting(Formats::Kind::Text));
        QVERIFY(!Formats::keepsFormatting(Formats::Kind::Markdown));
    }

    void plainText()
    {
        QTextDocument document;
        Doc::prepare(&document);
        Formats::fromPlainText(QStringLiteral("uno\r\ndos\n\ncuatro\n"), &document);
        QCOMPARE(document.blockCount(), 4);
        QCOMPARE(Doc::styleOf(document.firstBlock()), QStringLiteral("body"));
        QCOMPARE(Formats::toPlainText(&document), QStringLiteral("uno\ndos\n\ncuatro"));
        QCOMPARE(describe(&document, false).first(), QStringLiteral("body left | [uno ----0 #1d1d1f]"));

        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QString text = Formats::toPlainText(editor.document());
        QVERIFY(text.startsWith(QStringLiteral("El jardín de Wonderland\nUn documento de muestra con negrita, cursiva")));
        QVERIFY(text.contains(QStringLiteral("Planta\tCantidad\tEstación\nRosal\t12\tPrimavera")));
        QVERIFY(!text.contains(QChar::ObjectReplacementCharacter));
    }

    void markdown()
    {
        const QString source = QStringLiteral(
            "# Título\n\nUn párrafo con **negrita**, *cursiva*, ~~tachado~~, `código` y un [enlace](https://example.org/).\n\n"
            "## Sección\n\n- uno\n- dos\n    - dentro\n\n1. primero\n2. segundo\n\n> Una cita.\n\n```\nlínea 1\n  línea 2\n```\n\n"
            "| A | B |\n|---|---|\n| 1 | 2 |\n\n### Final\n");
        QTextDocument document;
        Doc::prepare(&document);
        Formats::fromMarkdown(source, &document);
        const QStringList lines = describe(&document, false);
        const QString all = lines.join(QLatin1Char('\n'));
        for (const char *wanted : {"h1 left | [Título b---0", "[negrita b---0 #1d1d1f]", "[cursiva -i--0 #1d1d1f]", "[tachado ---s0 #1d1d1f]", "->https://example.org/]",
                                   "h2 left | [Sección", "bullet1#1 | [uno", "bullet1#2 | [dos", "bullet2#1 | [dentro", "number1#2 | [segundo", "quote left | [Una cita.",
                                   "code left | [línea 1", "code left | [  línea 2", "cell0,0of2x2 | [A", "cell1,1of2x2 | [2", "h3 left | [Final"}) {
            QVERIFY2(all.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + all));
        }
        QCOMPARE(Doc::lookOf(document.firstBlock().begin().fragment().charFormat()).size, 20.0);
        // The inline code is in the code family.
        bool mono = false;
        for (auto it = document.findBlockByNumber(1).begin(); !it.atEnd(); ++it) {
            mono = mono || (it.fragment().text() == QStringLiteral("código") && Doc::lookOf(it.fragment().charFormat()).family == Doc::monoFamily());
        }
        QVERIFY(mono);

        // Written and read again, it is the same document.
        const QString written = Formats::toMarkdown(&document);
        QVERIFY2(written.startsWith(QStringLiteral("# Título\n\nUn párrafo con **negrita**, *cursiva*, ~~tachado~~, `código` y un [enlace](https://example.org/).\n")), qPrintable(written));
        QTextDocument again;
        Doc::prepare(&again);
        Formats::fromMarkdown(written, &again);
        COMPARE_LINES(describe(&again), describe(&document));
        QCOMPARE(Formats::toMarkdown(&again), written);
    }

    void markdownOfTheSample()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QString written = Formats::toMarkdown(editor.document());
        for (const char *wanted : {"# El jardín de Wonderland\n", "**negrita**", "*cursiva*", "~~tachado~~", "[example.org](https://example.org/)", "\n## Lo que crece\n",
                                   "\n- Rosas blancas, pintadas de rojo\n", "\n1. Regar al amanecer\n2. Podar antes del té\n", "\n> «Empieza por el principio",
                                   "| Planta | Cantidad | Estación |", "| Rosal | 12 | Primavera |", "```\nfor (carta in baraja) {\n    pintar(carta, \"rojo\")\n}\n```", "![](data:image/png;base64,"}) {
            QVERIFY2(written.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + written.left(3000)));
        }
        QTextDocument back;
        Doc::prepare(&back);
        Formats::fromMarkdown(written, &back);
        const QString all = describe(&back, false).join(QLatin1Char('\n'));
        for (const char *wanted : {"title left | [El jardín de Wonderland", "h1 left | [Lo que crece", "bullet1#3", "number1#3", "cell2,2of3x3 | [Otoño", "[image 360x140 of 360x140]", "code left | [    pintar"}) {
            QVERIFY2(all.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + all));
        }
    }

    void html()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QString written = Formats::toHtml(editor.document());
        QVERIFY(written.startsWith(QStringLiteral("<!DOCTYPE html>")));
        QVERIFY(written.contains(QStringLiteral("<meta charset=\"utf-8\"")));
        QVERIFY(written.contains(QStringLiteral("<h1")));
        QVERIFY(written.contains(QStringLiteral("<table")));
        QVERIFY(written.contains(QStringLiteral("href=\"https://example.org/\"")));
        QVERIFY(written.contains(QStringLiteral("src=\"data:image/png;base64,")));
        QTextDocument back;
        Doc::prepare(&back);
        Formats::fromHtml(written, &back);
        const QString all = describe(&back, false).join(QLatin1Char('\n'));
        for (const char *wanted : {"title left | [El jardín de Wonderland b---0", "[negrita b---0", "[cursiva -i--0", "[subrayado --u-0", "[tachado ---s0", "[de color ----0 #d70015",
                                   "bg#ffe066", "[2 ----1", "[2 ----2", "->https://example.org/]", "h1 left | [Lo que crece", "bullet1#3 | [Un árbol con puerta",
                                   "h2 left | [Orden de las tareas", "number1#3 | [Contar las cartas", "quote left | [«Empieza", "cell0,0of3x3 | [Planta b---0", "cell2,2of3x3 | [Otoño ----0",
                                   "h3 left | [Una figura", "[image 360x140 of 360x140]", "code left | [    pintar", "body justify | [Este párrafo empieza"}) {
            QVERIFY2(all.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + all));
        }

        // A page from somewhere else.
        QTextDocument foreign;
        Doc::prepare(&foreign);
        Formats::fromHtml(QStringLiteral("<html><head><title>x</title><style>p{color:red}</style></head><body><h2>Hola</h2><p>Un <b>texto</b> y <a href=\"https://example.org/\">más</a>.</p>"
                                         "<ul><li>uno</li><li>dos</li></ul><blockquote>cita</blockquote><pre>a\n  b</pre></body></html>"), &foreign);
        const QString other = describe(&foreign, false).join(QLatin1Char('\n'));
        for (const char *wanted : {"h2 left | [Hola", "[texto b---0", "->https://example.org/]", "bullet1#2 | [dos", "quote left | [cita", "code left | [  b"}) {
            QVERIFY2(other.contains(QString::fromUtf8(wanted)), qPrintable(QString::fromUtf8(wanted) + QStringLiteral("\n---\n") + other));
        }
    }

    void rtf()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QByteArray written = Formats::toRtf(editor.document(), editor.setup());
        QVERIFY(written.startsWith("{\\rtf1\\ansi"));
        QVERIFY(written.endsWith("}\n"));
        // Seven bits only: nothing depends on a code page.
        for (const char c : written) {
            QVERIFY(uchar(c) < 0x80);
        }
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QVERIFY(Formats::fromRtf(written, &back, &setup));
        COMPARE_LINES(describe(&back), describe(editor.document()));
        QCOMPARE(setup, editor.setup());
        QCOMPARE(Formats::toRtf(&back, setup), written);

        QVERIFY(!Formats::fromRtf("hola", &back, &setup));
    }

    void rtfFromElsewhere()
    {
        const QByteArray source =
            "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss\\fcharset0 Helvetica;}{\\f1\\fmodern Courier New;}}"
            "{\\colortbl ;\\red255\\green0\\blue0;}{\\stylesheet{\\s0 Normal;}}{\\info{\\title No se ve}}"
            "\\paperw12240\\paperh15840\\margl1800\\margr1800\\margt1440\\margb1440\n"
            "\\pard\\qc\\outlinelevel0\\b\\fs40 Un t\\'edtulo\\b0\\par\n"
            "\\pard\\fs22 Texto \\b fuerte\\b0  y \\i cursiva\\i0 , \\cf1 rojo\\cf0 , \\u241?and\\u250? \\{llaves\\}.\\par\n"
            "{\\pntext\\'b7\\tab}{\\*\\pn\\pnlvlblt{\\pntxtb\\'b7}}\\li720\\fi-360 vi\\'f1eta\\par\n"
            "{\\*\\desconocido no se ve}\\pard dos\\line l\\'edneas\\par\n"
            "\\trowd\\cellx3000\\cellx6000\\pard\\intbl a\\cell b\\cell\\row\\trowd\\cellx3000\\cellx6000\\pard\\intbl c\\cell d\\cell\\row\n"
            "\\pard\\page final\\par}";
        QTextDocument document;
        Doc::prepare(&document);
        Doc::PageSetup setup;
        QVERIFY(Formats::fromRtf(source, &document, &setup));
        const QStringList lines = describe(&document, false);
        const QString all = lines.join(QLatin1Char('\n'));
        QVERIFY2(lines.at(0).startsWith(QStringLiteral("h1 center | [Un título b---0")), qPrintable(all));
        QCOMPARE(lines.at(1), QStringLiteral("body left | [Texto  ----0 #1d1d1f] [fuerte b---0 #1d1d1f] [ y  ----0 #1d1d1f] [cursiva -i--0 #1d1d1f] [,  ----0 #1d1d1f] [rojo ----0 #ff0000] [, ñandú {llaves}. ----0 #1d1d1f]"));
        QVERIFY2(lines.at(2).startsWith(QStringLiteral("body left bullet1#1 | [viñeta")), qPrintable(all));
        QVERIFY2(lines.at(3).startsWith(QStringLiteral("body left | [dos\u2028líneas")), qPrintable(all));
        QVERIFY2(lines.at(4).contains(QStringLiteral("cell0,0of2x2 | [a")), qPrintable(all));
        QVERIFY2(lines.at(7).contains(QStringLiteral("cell1,1of2x2 | [d")), qPrintable(all));
        QVERIFY2(lines.last().startsWith(QStringLiteral("body left break | [final")), qPrintable(all));
        QVERIFY(!all.contains(QStringLiteral("No se ve")) && !all.contains(QStringLiteral("no se ve")) && !all.contains(QStringLiteral("Normal")));
        QCOMPARE(setup.paper, QStringLiteral("Carta"));
        QCOMPARE(setup.left, 3.18);
    }

    void filesByExtension()
    {
        QTemporaryDir dir;
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QStringList before = describe(editor.document());
        for (const char *suffix : {"docx", "odt", "rtf", "html", "md", "txt"}) {
            const QString path = dir.filePath(QStringLiteral("doc.") + QLatin1String(suffix));
            QString error;
            QVERIFY2(Formats::save(path, editor.document(), editor.setup(), &error), qPrintable(error));
            QTextDocument back;
            Doc::PageSetup setup;
            QVERIFY2(Formats::load(path, &back, &setup, &error), qPrintable(error));
            QVERIFY(Formats::toPlainText(&back).contains(QStringLiteral("Rosas blancas, pintadas de rojo")));
            if (Formats::keepsFormatting(Formats::kindOf(path))) {
                COMPARE_LINES(describe(&back), before);
            }
        }
        QString error;
        QTextDocument back;
        Doc::PageSetup setup;
        QVERIFY(!Formats::load(dir.filePath(QStringLiteral("no-está.txt")), &back, &setup, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!Formats::load(dir.filePath(QStringLiteral("doc.docx")).replace(QStringLiteral(".docx"), QStringLiteral(".pdf")), &back, &setup, &error));
        QVERIFY(!Formats::save(dir.filePath(QStringLiteral("doc.xyz")), editor.document(), editor.setup(), &error));

        // A .docx that is really something else is said to be so, not shown as noise.
        QFile fake(dir.filePath(QStringLiteral("falso.docx")));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("esto no es un zip");
        fake.close();
        QVERIFY(!Formats::load(fake.fileName(), &back, &setup, &error));

        // Text in Latin-1 from an old program is still read as letters.
        QFile latin(dir.filePath(QStringLiteral("viejo.txt")));
        QVERIFY(latin.open(QIODevice::WriteOnly));
        latin.write("a\xf1o y camino\xb4s");
        latin.close();
        QVERIFY(Formats::load(latin.fileName(), &back, &setup, &error));
        QVERIFY(Formats::toPlainText(&back).startsWith(QStringLiteral("año y camino")));
    }

    void pdf()
    {
        QTemporaryDir dir;
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QString path = dir.filePath(QStringLiteral("muestra.pdf"));
        QString error;
        QVERIFY2(Formats::writePdf(path, editor.document(), editor.setup(), &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("%PDF-"));
        QVERIFY(!QFileInfo::exists(path + QStringLiteral(".parcial")));
        // The same number of sheets the window shows.
        QCOMPARE(Formats::pageCount(editor.document()), 2);

        const QString pdfinfo = QStandardPaths::findExecutable(QStringLiteral("pdfinfo"));
        const QString pdftotext = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
        if (pdfinfo.isEmpty() || pdftotext.isEmpty()) {
            QSKIP("poppler's tools are not installed: the PDF was not read back");
        }
        QProcess info;
        info.start(pdfinfo, {path});
        QVERIFY(info.waitForFinished(30000));
        const QString said = QString::fromUtf8(info.readAllStandardOutput());
        QVERIFY2(said.contains(QRegularExpression(QStringLiteral("Pages:\\s+2\\b"))), qPrintable(said));
        QVERIFY2(said.contains(QRegularExpression(QStringLiteral("Page size:\\s+59[45](\\.\\d+)? x 84[12](\\.\\d+)? pts"))), qPrintable(said));
        QProcess text;
        text.start(pdftotext, {QStringLiteral("-layout"), path, QStringLiteral("-")});
        QVERIFY(text.waitForFinished(30000));
        const QString words = QString::fromUtf8(text.readAllStandardOutput());
        for (const char *wanted : {"El jardín de Wonderland", "Rosas blancas, pintadas de rojo", "Primavera", "Segunda página"}) {
            QVERIFY2(words.contains(QString::fromUtf8(wanted)), wanted);
        }
        // The second sheet starts with its heading, and carries its number.
        const QStringList sheets = words.split(QLatin1Char('\f'));
        QVERIFY(sheets.size() >= 2);
        QVERIFY(sheets.at(1).contains(QStringLiteral("Segunda página")));
        QVERIFY(!sheets.at(0).contains(QStringLiteral("Segunda página")));
        QVERIFY(sheets.at(1).trimmed().endsWith(QLatin1Char('2')));

        // Landscape Letter comes out that size.
        editor.setSetup({{QStringLiteral("paper"), QStringLiteral("Carta")}, {QStringLiteral("landscape"), true}});
        QVERIFY(Formats::writePdf(path, editor.document(), editor.setup(), &error));
        info.start(pdfinfo, {path});
        QVERIFY(info.waitForFinished(30000));
        const QString wide = QString::fromUtf8(info.readAllStandardOutput());
        QVERIFY2(wide.contains(QRegularExpression(QStringLiteral("Page size:\\s+79[12](\\.\\d+)? x 61[12](\\.\\d+)? pts"))), qPrintable(wide));
    }
};

QTEST_MAIN(TestFormats)
#include "tst_formats.moc"
