#include "describe.h"
#include "editor.h"
#include "odt.h"
#include "zip.h"

#include <QDomDocument>
#include <QTemporaryDir>
#include <QTest>

class TestOdt : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTripsTheSample()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QByteArray bytes = Odt::toBytes(editor.document(), editor.setup());
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QString error;
        QVERIFY2(Odt::fromBytes(bytes, &back, &setup, &error), qPrintable(error));
        COMPARE_LINES(describe(&back), describe(editor.document()));
        QCOMPARE(setup, editor.setup());
        QCOMPARE(Odt::toBytes(&back, setup).size(), bytes.size());
    }

    void roundTripsTheTemplatesAndTheSetup()
    {
        Editor editor;
        for (const QString &kind : {QStringLiteral("blank"), QStringLiteral("letter"), QStringLiteral("resume"), QStringLiteral("report")}) {
            editor.newDocument(kind);
            editor.setSetup({{QStringLiteral("paper"), QStringLiteral("Legal")}, {QStringLiteral("landscape"), true}, {QStringLiteral("right"), 3.0},
                             {QStringLiteral("bottom"), 1.5}, {QStringLiteral("header"), QStringLiteral("Cabecera & <más>")}, {QStringLiteral("footer"), QStringLiteral("Pie")}});
            QTextDocument back;
            Doc::prepare(&back);
            Doc::PageSetup setup;
            QVERIFY(Odt::fromBytes(Odt::toBytes(editor.document(), editor.setup()), &back, &setup));
            COMPARE_LINES(describe(&back), describe(editor.document()));
            QCOMPARE(setup, editor.setup());
        }
    }

    void roundTripsParagraphFormatsAndNestedLists()
    {
        Editor editor;
        editor.insertText(QStringLiteral("uno\ndos\ntres\ncuatro\ncinco"));
        editor.selectAll();
        editor.setList(1);
        editor.goTo(5);
        editor.indentMore();
        editor.goTo(9);
        editor.indentMore();
        editor.indentMore();
        editor.goTo(21);
        editor.setList(0);
        editor.setAlignment(QStringLiteral("center"));
        editor.setLineHeight(150);
        editor.setSpacing(12, 24);
        editor.setIndents(2, 1, 0.5);
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QVERIFY(Odt::fromBytes(Odt::toBytes(editor.document(), editor.setup()), &back, &setup));
        const QStringList lines = describe(&back);
        COMPARE_LINES(lines, describe(editor.document()));
        QVERIFY2(lines.at(2).contains(QStringLiteral("bullet3#1")), qPrintable(lines.at(2)));
        QVERIFY2(lines.at(3).contains(QStringLiteral("bullet1#2")), qPrintable(lines.at(3)));
        const QTextBlockFormat format = back.lastBlock().blockFormat();
        QCOMPARE(qRound(format.lineHeight()), 150);
        QCOMPARE(qRound(format.topMargin() / Doc::pxPerPt), 12);
        QCOMPARE(qRound(format.bottomMargin() / Doc::pxPerPt), 24);
        QCOMPARE(qRound(format.leftMargin() / Doc::pxPerCm * 10), 20);
        QCOMPARE(qRound(format.rightMargin() / Doc::pxPerCm * 10), 10);
        QCOMPARE(qRound(format.textIndent() / Doc::pxPerCm * 10), 5);
    }

    void isAPackage()
    {
        Editor editor;
        editor.newDocument(QStringLiteral("sample"));
        const QByteArray bytes = Odt::toBytes(editor.document(), editor.setup());
        // The mimetype comes first and is not compressed: it can be read at a fixed place.
        QCOMPARE(bytes.mid(30, 8), QByteArray("mimetype"));
        QCOMPARE(bytes.mid(38, 39), QByteArray("application/vnd.oasis.opendocument.text"));
        const ZipReader zip(bytes);
        QVERIFY(zip.isValid());
        for (const char *part : {"content.xml", "styles.xml", "meta.xml", "META-INF/manifest.xml"}) {
            QVERIFY2(zip.contains(QLatin1String(part)), part);
            QDomDocument dom;
            const auto result = dom.setContent(zip.file(QLatin1String(part)), QDomDocument::ParseOption::UseNamespaceProcessing);
            QVERIFY2(bool(result), qPrintable(QLatin1String(part) + QStringLiteral(": ") + result.errorMessage));
        }
        // Everything in the archive is in the manifest.
        const QByteArray manifest = zip.file(QStringLiteral("META-INF/manifest.xml"));
        for (const QString &name : zip.names()) {
            if (name != QLatin1String("mimetype") && name != QLatin1String("META-INF/manifest.xml")) {
                QVERIFY2(manifest.contains("manifest:full-path=\"" + name.toUtf8() + "\""), qPrintable(name));
            }
        }
    }

    void rejectsWhatIsNotOdt()
    {
        QTextDocument document;
        Doc::prepare(&document);
        Doc::PageSetup setup;
        QString error;
        QVERIFY(!Odt::fromBytes("no es un zip", &document, &setup, &error));
        QVERIFY(!error.isEmpty());
        ZipWriter zip;
        zip.add(QStringLiteral("content.xml"), "<office:document-content>");
        QVERIFY(!Odt::fromBytes(zip.finish(), &document, &setup, &error));
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
        const QString ours = dir.filePath(QStringLiteral("muestra.odt"));
        QVERIFY(Odt::write(ours, editor.document(), editor.setup()));

        const QString text = sofficeConvert(ours, QStringLiteral("txt:Text (encoded):UTF8"), dir.path());
        QVERIFY2(!text.isEmpty(), "LibreOffice did not convert the document");
        QFile file(text);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString read = QString::fromUtf8(file.readAll());
        for (const char *wanted : {"El jardín de Wonderland", "negrita", "example.org", "Rosas blancas, pintadas de rojo", "Podar antes del té", "Empieza por el principio",
                                   "Primavera", "pintar(carta, \"rojo\")", "Segunda página"}) {
            QVERIFY2(read.contains(QString::fromUtf8(wanted)), wanted);
        }

        QDir().mkpath(dir.filePath(QStringLiteral("vuelta")));
        const QString theirs = sofficeConvert(ours, QStringLiteral("odt:writer8"), dir.filePath(QStringLiteral("vuelta")));
        QVERIFY2(!theirs.isEmpty(), "LibreOffice did not write an .odt");
        QTextDocument back;
        Doc::prepare(&back);
        Doc::PageSetup setup;
        QString error;
        QVERIFY2(Odt::read(theirs, &back, &setup, &error), qPrintable(error));
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

QTEST_MAIN(TestOdt)
#include "tst_odt.moc"
