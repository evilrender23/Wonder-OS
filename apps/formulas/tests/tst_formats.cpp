#include "formats.h"
#include "latex.h"
#include "zip.h"

#include <QDomDocument>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

class TestFormats : public QObject
{
    Q_OBJECT

    static FormulaDoc sample()
    {
        FormulaDoc doc;
        doc.uid = QStringLiteral("abc123");
        doc.title = QStringLiteral("Cinemática");
        doc.equations = {QStringLiteral("x = x_0 + v t"), QStringLiteral("v^2 = v_0^2 + 2 a \\Delta x"), QStringLiteral("\\frac{a}{b} \\leq \\sqrt{2}")};
        doc.color = QStringLiteral("#5b5bd6");
        doc.size = 24;
        doc.font = QStringLiteral("STIX Two Math");
        return doc;
    }
    /// LaTeX → MathML → LaTeX, compared as trees would be: without spaces or optional braces.
    static QString there(const QString &latex)
    {
        QString error;
        const QStringList back = Formats::fromMathML(Formats::toMathML(latex, false), &error);
        return back.value(0);
    }
    static QString squeeze(QString text)
    {
        return text.remove(QLatin1Char(' ')).remove(QLatin1Char('{')).remove(QLatin1Char('}'));
    }

private Q_SLOTS:
    void kinds()
    {
        QCOMPARE(Formats::kindOf(QStringLiteral("/a/b.wform")), QStringLiteral("wform"));
        QCOMPARE(Formats::kindOf(QStringLiteral("/a/B.ODF")), QStringLiteral("odf"));
        QCOMPARE(Formats::kindOf(QStringLiteral("x.mml")), QStringLiteral("mml"));
        QCOMPARE(Formats::kindOf(QStringLiteral("x.tex")), QStringLiteral("tex"));
    }

    void wformRoundTrip()
    {
        const FormulaDoc doc = sample();
        const QByteArray bytes = Formats::toWform(doc);
        QVERIFY(QJsonDocument::fromJson(bytes).isObject());
        FormulaDoc back;
        QString error;
        QVERIFY2(Formats::fromWform(bytes, &back, &error), qPrintable(error));
        QCOMPARE(back, doc);
    }

    void wformDefaults()
    {
        FormulaDoc doc;
        QString error;
        QVERIFY(Formats::fromWform("{\"format\":\"wonderland-formula\",\"equations\":[\"x\"]}", &doc, &error));
        QCOMPARE(doc.equations, QStringList{QStringLiteral("x")});
        QCOMPARE(doc.size, 20.0);
        QVERIFY(doc.color.isEmpty());
        QVERIFY(doc.uid.isEmpty());
    }

    void wformRejectsRubbish()
    {
        FormulaDoc doc;
        QString error;
        QVERIFY(!Formats::fromWform("not json", &doc, &error));
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(!Formats::fromWform("[1,2]", &doc, &error));
        QVERIFY(!error.isEmpty());
    }

    void texRoundTrip()
    {
        const FormulaDoc doc = sample();
        const QString tex = Formats::toTex(doc);
        QCOMPARE(tex.count(QStringLiteral("\\[")), 3);
        QCOMPARE(tex.count(QStringLiteral("\\]")), 3);
        QCOMPARE(Formats::fromTex(tex).equations, doc.equations);
    }

    void texFromOutside_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("equations");
        QTest::newRow("bare") << "a^2 + b^2 = c^2\n" << QStringList{QStringLiteral("a^2 + b^2 = c^2")};
        QTest::newRow("dollars") << "Text $$x = 1$$ more $$y = 2$$" << QStringList{QStringLiteral("x = 1"), QStringLiteral("y = 2")};
                QTest::newRow("brackets") << "\\[ x \\]\n\\[ y \\]" << QStringList{QStringLiteral("x"), QStringLiteral("y")};
        QTest::newRow("equation") << "\\begin{equation}\n  E = mc^2\n\\end{equation}" << QStringList{QStringLiteral("E = mc^2")};
        QTest::newRow("document") << "\\documentclass{article}\n\\begin{document}\nHello\n\\begin{equation*}\na = b\n\\end{equation*}\n\\end{document}\n" << QStringList{QStringLiteral("a = b")};
        QTest::newRow("comment") << "% a note\n\\[ x \\]" << QStringList{QStringLiteral("x")};
    }
    void texFromOutside()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, equations);
        QCOMPARE(Formats::fromTex(text).equations, equations);
    }

    void mathmlIsWellFormed_data()
    {
        QTest::addColumn<QString>("latex");
        QTest::addColumn<QStringList>("elements");
        QTest::newRow("fraction") << "\\frac{a}{b}" << QStringList{QStringLiteral("mfrac")};
        QTest::newRow("root") << "\\sqrt{x} + \\sqrt[3]{y}" << QStringList{QStringLiteral("msqrt"), QStringLiteral("mroot")};
        QTest::newRow("scripts") << "x^2 + y_i + z_i^2" << QStringList{QStringLiteral("msup"), QStringLiteral("msub"), QStringLiteral("msubsup")};
        QTest::newRow("sum") << "\\sum_{i=1}^{n} i" << QStringList{QStringLiteral("munderover")};
        QTest::newRow("limit") << "\\lim_{x \\to 0} f(x)" << QStringList{QStringLiteral("munder")};
        QTest::newRow("matrix") << "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}" << QStringList{QStringLiteral("mtable"), QStringLiteral("mtr"), QStringLiteral("mtd")};
        QTest::newRow("text") << "x \\text{ si } y" << QStringList{QStringLiteral("mtext")};
        QTest::newRow("accent") << "\\hat{x} + \\overline{y}" << QStringList{QStringLiteral("mover")};
        QTest::newRow("under") << "\\underline{x}" << QStringList{QStringLiteral("munder")};
        QTest::newRow("numbers") << "3.14 + 2" << QStringList{QStringLiteral("mn"), QStringLiteral("mo")};
        QTest::newRow("colour") << "\\color{red}{x}" << QStringList{QStringLiteral("mstyle")};
        QTest::newRow("space") << "a \\quad b" << QStringList{QStringLiteral("mspace")};
        QTest::newRow("delimiters") << "\\left( \\frac{a}{b} \\right)" << QStringList{QStringLiteral("mrow"), QStringLiteral("mo")};
    }
    void mathmlIsWellFormed()
    {
        QFETCH(QString, latex);
        QFETCH(QStringList, elements);
        const QString xml = Formats::toMathML(latex);
        QDomDocument dom;
        const auto result = dom.setContent(xml, QDomDocument::ParseOption::UseNamespaceProcessing);
        QVERIFY2(bool(result), qPrintable(result.errorMessage + QLatin1Char('\n') + xml));
        const QDomElement math = dom.documentElement();
        QCOMPARE(math.localName(), QStringLiteral("math"));
        QCOMPARE(math.namespaceURI(), QStringLiteral("http://www.w3.org/1998/Math/MathML"));
        for (const QString &element : elements) {
            QVERIFY2(xml.contains(QLatin1Char('<') + element), qPrintable(element + QLatin1Char('\n') + xml));
        }
        // The LaTeX rides along, for whoever reads it back.
        const QDomNodeList notes = dom.elementsByTagNameNS(QStringLiteral("http://www.w3.org/1998/Math/MathML"), QStringLiteral("annotation"));
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.at(0).toElement().text().trimmed(), latex);
        QVERIFY(!Formats::toMathML(latex, false).contains(QStringLiteral("annotation")));
    }

    void mathmlItalicsAndIdentifiers()
    {
        const QString xml = Formats::toMathML(QStringLiteral("\\sin x + \\alpha + \\mathbb{R}"), false);
        QVERIFY(xml.contains(QStringLiteral("<mi>sin</mi>")));
        QVERIFY(xml.contains(QStringLiteral("<mi>x</mi>")));
        QVERIFY(xml.contains(QString::fromUtf8("<mi>α</mi>")));
        QVERIFY(xml.contains(QString::fromUtf8("ℝ")) || xml.contains(QStringLiteral("double-struck")));
    }

    void mathmlComesBack_data()
    {
        QTest::addColumn<QString>("latex");
        const QStringList all = {
            QStringLiteral("x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}"),
            QStringLiteral("\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}"),
            QStringLiteral("\\int_{a}^{b} f(x) \\, dx"),
            QStringLiteral("\\lim_{x \\to \\infty} \\frac{1}{x} = 0"),
            QStringLiteral("\\sqrt[3]{x + 1}"),
            QStringLiteral("x_i^2 + y_{jk}"),
            QStringLiteral("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}"),
            QStringLiteral("\\alpha \\leq \\beta \\to \\gamma"),
            QStringLiteral("\\hat{x} + \\vec{v}"),
            QStringLiteral("\\binom{n}{k}"),
        };
        for (const QString &latex : all) {
            QTest::newRow(qPrintable(latex)) << latex;
        }
    }
    void mathmlComesBack()
    {
        QFETCH(QString, latex);
        // Without the annotation, the LaTeX is rebuilt from the elements: the same formula, whatever the spelling.
        const QString back = there(latex);
        QVERIFY2(!back.isEmpty(), "nothing came back");
        const Parsed parsed = Latex::parse(back);
        QVERIFY2(parsed.ok(), qPrintable(back + QStringLiteral(" → ") + parsed.errors.value(0).message));
        QCOMPARE(squeeze(Formats::toMathML(back, false)), squeeze(Formats::toMathML(latex, false)));
    }

    void mathmlFromOutside()
    {
        // As a browser or LibreOffice would write it, no annotation.
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\"?><math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><mrow>"
            "<mi>E</mi><mo>=</mo><mi>m</mi><msup><mi>c</mi><mn>2</mn></msup><mo>+</mo>"
            "<mfrac><mn>1</mn><mrow><mi>x</mi><mo>+</mo><mn>1</mn></mrow></mfrac><mo>+</mo><msqrt><mi>y</mi></msqrt>"
            "<mo>+</mo><mi>&#x3B1;</mi></mrow></math>");
        QString error;
        const QStringList back = Formats::fromMathML(xml, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.size(), 1);
        QCOMPARE(squeeze(back.first()), QStringLiteral("E=mc^2+\\frac1x+1+\\sqrty+\\alpha"));
        QVERIFY(Latex::parse(back.first()).ok());
        // What is not MathML says so.
        error.clear();
        QVERIFY(Formats::fromMathML(QStringLiteral("<html><body/></html>"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(Formats::fromMathML(QStringLiteral("<math><mi>x</math>"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void starMath_data()
    {
        QTest::addColumn<QString>("latex");
        QTest::addColumn<QString>("starmath");
        QTest::newRow("fraction") << "\\frac{a}{b}" << "{{a} over {b}}";
        QTest::newRow("root") << "\\sqrt{x}" << "sqrt{x}";
        QTest::newRow("nroot") << "\\sqrt[3]{x}" << "nroot{3}{x}";
        QTest::newRow("scripts") << "x_i^2" << "x_{i}^{2}";
        QTest::newRow("greek") << "\\alpha + \\Omega" << "%ialpha + %OMEGA";
        QTest::newRow("sum") << "\\sum_{i=1}^{n} i" << "sum from{i = 1} to{n} i";
        QTest::newRow("integral") << "\\int_{a}^{b} x" << "int from{a} to{b} x";
        QTest::newRow("relation") << "a \\leq b \\neq c" << "a <= b <> c";
        QTest::newRow("parens") << "\\left( x \\right)" << "left( x right)";
        QTest::newRow("matrix") << "\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}" << "left( matrix{{a} # {b} ## {c} # {d}} right)";
        QTest::newRow("times") << "a \\times b \\cdot c" << "a times b cdot c";
        QTest::newRow("infinity") << "\\infty" << "infinity";
    }
    void starMath()
    {
        QFETCH(QString, latex);
        QFETCH(QString, starmath);
        const auto loose = [](QString text) {
            return text.simplified().remove(QLatin1Char(' '));
        };
        QCOMPARE(loose(Formats::toStarMath(latex)), loose(starmath));
    }

    void odfIsAnOpenDocumentFormula()
    {
        const QByteArray bytes = Formats::toOdf(sample());
        // The mimetype comes first and is stored, as the standard asks.
        QCOMPARE(bytes.mid(30, 8), QByteArray("mimetype"));
        QCOMPARE(bytes.mid(38, 42), QByteArray("application/vnd.oasis.opendocument.formula"));
        const ZipReader zip(bytes);
        QVERIFY(zip.isValid());
        QCOMPARE(zip.names().first(), QStringLiteral("mimetype"));
        for (const char *name : {"content.xml", "settings.xml", "META-INF/manifest.xml"}) {
            QVERIFY2(zip.contains(QLatin1String(name)), name);
        }
        QDomDocument content;
        QVERIFY(bool(content.setContent(zip.file(QStringLiteral("content.xml")), QDomDocument::ParseOption::UseNamespaceProcessing)));
        QCOMPARE(content.documentElement().localName(), QStringLiteral("math"));
        const QDomNodeList notes = content.elementsByTagNameNS(QStringLiteral("http://www.w3.org/1998/Math/MathML"), QStringLiteral("annotation"));
        QCOMPARE(notes.size(), 1);
        QCOMPARE(notes.at(0).toElement().attribute(QStringLiteral("encoding")), QStringLiteral("StarMath 5.0"));
        QVERIFY(notes.at(0).toElement().text().contains(QStringLiteral("newline")));
        QDomDocument manifest;
        QVERIFY(bool(manifest.setContent(zip.file(QStringLiteral("META-INF/manifest.xml")), QDomDocument::ParseOption::UseNamespaceProcessing)));
        QVERIFY(zip.file(QStringLiteral("META-INF/manifest.xml")).contains("application/vnd.oasis.opendocument.formula"));
    }

    void odfRoundTrip()
    {
        const FormulaDoc doc = sample();
        FormulaDoc back;
        QString error;
        QVERIFY2(Formats::fromOdf(Formats::toOdf(doc), &back, &error), qPrintable(error));
        // Our own come back as they were written.
        QCOMPARE(back.equations, doc.equations);
        QCOMPARE(back.title, doc.title);
        QCOMPARE(back.size, doc.size);
        QCOMPARE(back.color, doc.color);
    }

    void odfRejectsRubbish()
    {
        FormulaDoc doc;
        QString error;
        QVERIFY(!Formats::fromOdf("not a zip", &doc, &error));
        QVERIFY(!error.isEmpty());
        ZipWriter zip;
        zip.add(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text", true);
        error.clear();
        QVERIFY(!Formats::fromOdf(zip.finish(), &doc, &error));
        QVERIFY(!error.isEmpty());
    }

    void files()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const FormulaDoc doc = sample();
        QString error;
        for (const char *suffix : {"wform", "tex", "odf"}) {
            const QString path = dir.filePath(QStringLiteral("doc.") + QLatin1String(suffix));
            QVERIFY2(Formats::write(path, doc, &error), qPrintable(error));
            FormulaDoc back;
            QVERIFY2(Formats::read(path, &back, &error), qPrintable(error));
            QCOMPARE(back.equations, doc.equations);
        }
        // One formula per file in these.
        const QString mml = dir.filePath(QStringLiteral("doc.mml"));
        QVERIFY(Formats::write(mml, doc, &error, 1));
        FormulaDoc one;
        QVERIFY(Formats::read(mml, &one, &error));
        QCOMPARE(one.equations.size(), 1);
        QCOMPARE(squeeze(one.equations.first()), squeeze(doc.equations.at(1)));
        // A file that is not there says so.
        FormulaDoc none;
        error.clear();
        QVERIFY(!Formats::read(dir.filePath(QStringLiteral("nothing.wform")), &none, &error));
        QVERIFY(!error.isEmpty());
        // Nor can it be written where there is no folder.
        error.clear();
        QVERIFY(!Formats::write(dir.filePath(QStringLiteral("no/such/folder/doc.wform")), doc, &error));
        QVERIFY(!error.isEmpty());
    }

    void pictures()
    {
        QTemporaryDir dir;
        const FormulaDoc doc = sample();
        QString error;
        const QString png = dir.filePath(QStringLiteral("f.png"));
        QVERIFY2(Formats::write(png, doc, &error, 2, 1), qPrintable(error));
        const QImage one(png);
        QVERIFY(!one.isNull());
        QVERIFY(one.hasAlphaChannel());
        QCOMPARE(one.pixelColor(0, 0).alpha(), 0);
        QVERIFY(Formats::write(png, doc, &error, 2, 3));
        const QImage three(png);
        QVERIFY(qAbs(three.width() - 3 * one.width()) <= 3);
        // In the colour of the document.
        bool tinted = false;
        for (int y = 0; y < one.height() && !tinted; ++y) {
            for (int x = 0; x < one.width() && !tinted; ++x) {
                const QColor c = one.pixelColor(x, y);
                tinted = c.alpha() == 255 && c.rgb() == QColor(QStringLiteral("#5b5bd6")).rgb();
            }
        }
        QVERIFY(tinted);
        QCOMPARE(Formats::ink(doc), QColor(QStringLiteral("#5b5bd6")));
        QCOMPARE(Formats::ink(FormulaDoc()), QColor(Qt::black));
        QCOMPARE(Formats::toImage(doc, 0, 1, Qt::white).pixelColor(0, 0), QColor(Qt::white));
        // The size is in points: 24 pt are 32 px.
        QCOMPARE(Formats::pixels(24), 32.0);

        const QString svg = dir.filePath(QStringLiteral("f.svg"));
        QVERIFY(Formats::write(svg, doc, &error, 0));
        QFile file(svg);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QDomDocument dom;
        QVERIFY(bool(dom.setContent(bytes)));
        QCOMPARE(dom.documentElement().tagName(), QStringLiteral("svg"));
        QVERIFY(bytes.contains("#5b5bd6"));
        QCOMPARE(bytes, Formats::toSvg(doc, 0));
    }

    void pdf()
    {
        QTemporaryDir dir;
        FormulaDoc doc = sample();
        const QString path = dir.filePath(QStringLiteral("doc.pdf"));
        QVERIFY(Formats::toPdf(doc, path));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray one = file.readAll();
        file.close();
        QVERIFY(one.startsWith("%PDF-"));
        QCOMPARE(one.count("/Type /Page\n") + one.count("/Type /Page "), 1);
        // Enough of them go on to more pages.
        for (int i = 0; i < 60; ++i) {
            doc.equations << QStringLiteral("\\frac{a_{%1}}{b} + \\int_0^1 x \\, dx").arg(i);
        }
        QVERIFY(Formats::toPdf(doc, path));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray many = file.readAll();
        QVERIFY(many.count("/Type /Page\n") + many.count("/Type /Page ") > 1);
    }

    void zip()
    {
        ZipWriter writer;
        const QByteArray big = QByteArray(20000, 'a') + QByteArray("ñandú");
        writer.add(QStringLiteral("mimetype"), "text/plain", true);
        writer.add(QStringLiteral("dir/big.txt"), big);
        writer.add(QStringLiteral("empty"), QByteArray());
        const QByteArray archive = writer.finish();
        QVERIFY(archive.size() < 2000);
        const ZipReader reader(archive);
        QVERIFY(reader.isValid());
        QCOMPARE(reader.names(), (QStringList{QStringLiteral("mimetype"), QStringLiteral("dir/big.txt"), QStringLiteral("empty")}));
        QCOMPARE(reader.file(QStringLiteral("mimetype")), QByteArray("text/plain"));
        QCOMPARE(reader.file(QStringLiteral("dir/big.txt")), big);
        QVERIFY(reader.file(QStringLiteral("empty")).isEmpty());
        QVERIFY(reader.file(QStringLiteral("absent")).isNull());
        QVERIFY(!ZipReader("PK rubbish").isValid());
        QCOMPARE(Zip::crc32("123456789"), quint32(0xcbf43926));
        QCOMPARE(Zip::inflateRaw(Zip::deflateRaw(big), big.size()), big);
        // A broken entry gives nothing rather than rubbish.
        QByteArray broken = archive;
        const int at = broken.indexOf("dir/big.txt") + 15;
        broken[at] = char(broken[at] ^ 0xff);
        QVERIFY(ZipReader(broken).file(QStringLiteral("dir/big.txt")).isNull());
    }
};

QTEST_MAIN(TestFormats)
#include "tst_formats.moc"
