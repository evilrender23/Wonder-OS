#include "latex.h"

#include <QtTest>

class TestLatex : public QObject
{
    Q_OBJECT

    static NodePtr only(const Parsed &parsed)
    {
        NodePtr node = parsed.root;
        while (node && node->type == Node::Row && node->kids.size() == 1) {
            node = node->kids.first();
        }
        return node;
    }

private Q_SLOTS:
    void empty()
    {
        const Parsed parsed = Latex::parse(QString());
        QVERIFY(parsed.ok());
        QVERIFY(parsed.root);
        QCOMPARE(parsed.root->type, Node::Row);
    }

    void fraction()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\frac{a}{b}"));
        QVERIFY(parsed.ok());
        const NodePtr frac = only(parsed);
        QCOMPARE(frac->type, Node::Frac);
        QCOMPARE(frac->kids.size(), 2);
        QVERIFY(frac->rule);
        QCOMPARE(frac->start, 0);
        QCOMPARE(frac->end, 11);
    }

    void binomial()
    {
        const NodePtr binom = only(Latex::parse(QStringLiteral("\\binom{n}{k}")));
        QCOMPARE(binom->type, Node::Frac);
        QVERIFY(!binom->rule);
        QCOMPARE(binom->left, QStringLiteral("("));
        QCOMPARE(binom->right, QStringLiteral(")"));
    }

    void root()
    {
        const NodePtr plain = only(Latex::parse(QStringLiteral("\\sqrt{x}")));
        QCOMPARE(plain->type, Node::Sqrt);
        QVERIFY(plain->kids.size() < 2 || !plain->kids.at(1));
        const NodePtr nth = only(Latex::parse(QStringLiteral("\\sqrt[3]{x}")));
        QCOMPARE(nth->type, Node::Sqrt);
        QCOMPARE(nth->kids.size(), 2);
        QVERIFY(nth->kids.at(1));
    }

    void scripts()
    {
        const NodePtr both = only(Latex::parse(QStringLiteral("x_i^2")));
        QCOMPARE(both->type, Node::Script);
        QCOMPARE(both->kids.size(), 3);
        QVERIFY(both->kids.at(0) && both->kids.at(1) && both->kids.at(2));
        // The order they are written in does not matter.
        const NodePtr swapped = only(Latex::parse(QStringLiteral("x^2_i")));
        QCOMPARE(swapped->type, Node::Script);
        QVERIFY(swapped->kids.at(1) && swapped->kids.at(2));
        const NodePtr sup = only(Latex::parse(QStringLiteral("e^{-x^2}")));
        QCOMPARE(sup->type, Node::Script);
        QVERIFY(!sup->kids.at(1));
        QVERIFY(sup->kids.at(2));
    }

    void doubleScriptIsAnError()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("x^2^3"));
        QVERIFY(!parsed.ok());
        QCOMPARE(parsed.errors.first().position, 3);
    }

    void primes()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("f''(x)"));
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.root->kids.first()->type, Node::Script);
    }

    void bigOperators()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\sum_{i=1}^{n} i"));
        QVERIFY(parsed.ok());
        const NodePtr script = parsed.root->kids.first();
        QCOMPARE(script->type, Node::Script);
        const NodePtr sum = script->kids.first();
        QCOMPARE(sum->type, Node::Symbol);
        QVERIFY(sum->big);
        QVERIFY(sum->movable);
        QCOMPARE(sum->atom, Atom::Op);
        const NodePtr integral = Latex::parse(QStringLiteral("\\int_a^b")).root->kids.first()->kids.first();
        QVERIFY(integral->big);
        QVERIFY(!integral->movable);
        const Parsed limits = Latex::parse(QStringLiteral("\\int\\limits_a^b"));
        QVERIFY(limits.ok());
        // The scripts after `\limits` are still the integral's.
        QCOMPARE(limits.root->kids.size(), 1);
        QCOMPARE(limits.root->kids.first()->type, Node::Script);
        const NodePtr forced = limits.root->kids.first()->kids.first();
        QVERIFY(forced);
        QCOMPARE(forced->limits, 1);
        QVERIFY(!Latex::parse(QStringLiteral("\\limits x")).ok());
        const NodePtr lim = Latex::parse(QStringLiteral("\\lim_{x \\to 0} f")).root->kids.first()->kids.first();
        QVERIFY(lim->movable);
        QVERIFY(lim->upright);
    }

    void delimiters()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\left( \\frac{a}{b} \\right]"));
        QVERIFY(parsed.ok());
        const NodePtr delim = only(parsed);
        QCOMPARE(delim->type, Node::Delim);
        QCOMPARE(delim->left, QStringLiteral("("));
        QCOMPARE(delim->right, QStringLiteral("]"));
        const NodePtr open = only(Latex::parse(QStringLiteral("\\left. x \\right|")));
        QCOMPARE(open->type, Node::Delim);
        QVERIFY(open->left.isEmpty());
        QVERIFY(Latex::parse(QStringLiteral("\\left\\{ a \\middle| b \\right\\}")).ok());
        QVERIFY(Latex::parse(QStringLiteral("\\left\\langle a \\right\\rangle")).ok());
    }

    void unclosedDelimiter()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\left( x + y"));
        QVERIFY(!parsed.ok());
        // What is valid is still there to draw.
        QVERIFY(parsed.root);
        QVERIFY(!parsed.root->kids.isEmpty());
    }

    void matrices_data()
    {
        QTest::addColumn<QString>("environment");
        QTest::addColumn<QString>("left");
        QTest::addColumn<QString>("right");
        QTest::newRow("matrix") << "matrix" << "" << "";
        QTest::newRow("pmatrix") << "pmatrix" << "(" << ")";
        QTest::newRow("bmatrix") << "bmatrix" << "[" << "]";
        QTest::newRow("Bmatrix") << "Bmatrix" << "{" << "}";
        QTest::newRow("vmatrix") << "vmatrix" << "|" << "|";
        QTest::newRow("Vmatrix") << "Vmatrix" << QString::fromUtf8("‖") << QString::fromUtf8("‖");
        QTest::newRow("cases") << "cases" << "{" << "";
    }
    void matrices()
    {
        QFETCH(QString, environment);
        QFETCH(QString, left);
        QFETCH(QString, right);
        const Parsed parsed = Latex::parse(QStringLiteral("\\begin{%1} a & b \\\\ c & d \\end{%1}").arg(environment));
        QVERIFY2(parsed.ok(), qPrintable(parsed.errors.value(0).message));
        const NodePtr matrix = only(parsed);
        QCOMPARE(matrix->type, Node::Matrix);
        QCOMPARE(matrix->text, environment);
        QCOMPARE(matrix->cells.size(), 2);
        QCOMPARE(matrix->cells.at(0).size(), 2);
        QCOMPARE(matrix->cells.at(1).size(), 2);
        QCOMPARE(matrix->left, left);
        QCOMPARE(matrix->right, right);
    }

    void aligned()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\begin{aligned} a &= b + c \\\\ &= d \\end{aligned}"));
        QVERIFY(parsed.ok());
        const NodePtr matrix = only(parsed);
        QCOMPARE(matrix->type, Node::Matrix);
        QCOMPARE(matrix->cells.size(), 2);
        QCOMPARE(matrix->cells.at(0).size(), 2);
    }

    void environmentErrors()
    {
        QVERIFY(!Latex::parse(QStringLiteral("\\begin{pmatrix} a & b")).ok());
        QVERIFY(!Latex::parse(QStringLiteral("\\begin{pmatrix} a \\end{bmatrix}")).ok());
        QVERIFY(!Latex::parse(QStringLiteral("\\begin{nothing} a \\end{nothing}")).ok());
    }

    void greekAndSymbols()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\alpha + \\Omega \\leq \\infty \\to \\pm"));
        QVERIFY(parsed.ok());
        const QList<NodePtr> kids = parsed.root->kids;
        QCOMPARE(kids.size(), 7);
        QCOMPARE(kids.at(0)->text, QString::fromUtf8("α"));
        QCOMPARE(kids.at(0)->atom, Atom::Ord);
        QCOMPARE(kids.at(1)->atom, Atom::Bin);
        QCOMPARE(kids.at(2)->text, QString::fromUtf8("Ω"));
        QCOMPARE(kids.at(3)->text, QString::fromUtf8("≤"));
        QCOMPARE(kids.at(3)->atom, Atom::Rel);
        QCOMPARE(kids.at(4)->text, QString::fromUtf8("∞"));
        QCOMPARE(kids.at(5)->text, QString::fromUtf8("→"));
        QCOMPARE(kids.at(5)->atom, Atom::Rel);
        QCOMPARE(kids.at(6)->atom, Atom::Bin);
        QCOMPARE(kids.at(0)->command, QStringLiteral("alpha"));
    }

    void fonts_data()
    {
        QTest::addColumn<QString>("source");
        for (const char *command : {"mathbb", "mathbf", "mathrm", "mathcal", "mathit", "mathsf", "mathtt", "mathfrak", "boldsymbol"}) {
            QTest::newRow(command) << QStringLiteral("\\%1{R}").arg(QLatin1String(command));
        }
    }
    void fonts()
    {
        QFETCH(QString, source);
        const Parsed parsed = Latex::parse(source);
        QVERIFY2(parsed.ok(), qPrintable(parsed.errors.value(0).message));
        QCOMPARE(only(parsed)->type, Node::Style);
    }

    void namedOperator()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("\\operatorname{sen} x"));
        QVERIFY(parsed.ok());
        const NodePtr name = parsed.root->kids.first();
        QCOMPARE(name->type, Node::Symbol);
        QCOMPARE(name->text, QStringLiteral("sen"));
        QCOMPARE(name->atom, Atom::Op);
        QVERIFY(name->upright);
    }

    void text()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("x \\text{ si } y"));
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.root->kids.size(), 3);
        QCOMPARE(parsed.root->kids.at(1)->type, Node::Text);
        // The spaces of a text count.
        QCOMPARE(parsed.root->kids.at(1)->text, QStringLiteral(" si "));
    }

    void accents_data()
    {
        QTest::addColumn<QString>("command");
        for (const char *command : {"hat", "bar", "vec", "dot", "ddot", "tilde", "overline", "underline", "widehat", "widetilde", "overrightarrow", "overbrace", "underbrace"}) {
            QTest::newRow(command) << QString::fromLatin1(command);
        }
    }
    void accents()
    {
        QFETCH(QString, command);
        const Parsed parsed = Latex::parse(QStringLiteral("\\%1{x}").arg(command));
        QVERIFY2(parsed.ok(), qPrintable(parsed.errors.value(0).message));
        const NodePtr accent = only(parsed);
        QCOMPARE(accent->type, Node::Accent);
        QCOMPARE(accent->text, command);
        QCOMPARE(accent->kids.size(), 1);
    }

    void spaces()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("a\\,b\\;c\\quad d\\qquad e\\!f"));
        QVERIFY(parsed.ok());
        QList<qreal> widths;
        for (const NodePtr &kid : parsed.root->kids) {
            if (kid->type == Node::Space) {
                widths << kid->width;
            }
        }
        QCOMPARE(widths, (QList<qreal>{3, 5, 18, 36, -3}));
    }

    void color()
    {
        const Parsed braced = Latex::parse(QStringLiteral("\\color{red}{x} + y"));
        QVERIFY(braced.ok());
        const NodePtr style = braced.root->kids.first();
        QCOMPARE(style->type, Node::Style);
        QCOMPARE(style->text, QStringLiteral("color"));
        QVERIFY(Latex::parse(QStringLiteral("\\textcolor{blue}{x}")).ok());
        QVERIFY(Latex::parse(QStringLiteral("\\color{#ff8800}{x}")).ok());
        QCOMPARE(QColor(Latex::colorName(QStringLiteral("red"))), QColor(Qt::red));
        QVERIFY(QColor(Latex::colorName(QStringLiteral("ForestGreen"))).isValid());
        QVERIFY(Latex::colorName(QStringLiteral("nocolour")).isEmpty());
        QVERIFY(!Latex::parse(QStringLiteral("\\color{nocolour}{x}")).ok());
    }

    void unknownCommand()
    {
        const Parsed parsed = Latex::parse(QStringLiteral("a + \\nosuchthing + b"));
        QCOMPARE(parsed.errors.size(), 1);
        QCOMPARE(parsed.errors.first().position, 4);
        QCOMPARE(parsed.errors.first().length, 12);
        QVERIFY(!parsed.errors.first().message.isEmpty());
        // The rest is drawn, with the mistake as it was written.
        QCOMPARE(parsed.root->kids.size(), 5);
        QCOMPARE(parsed.root->kids.at(2)->type, Node::Error);
    }

    void braceErrors()
    {
        const Parsed open = Latex::parse(QStringLiteral("\\frac{a}{b"));
        QVERIFY(!open.ok());
        QVERIFY(open.root);
        const Parsed close = Latex::parse(QStringLiteral("a } b"));
        QVERIFY(!close.ok());
        QCOMPARE(close.errors.first().position, 2);
        const Parsed missing = Latex::parse(QStringLiteral("\\frac{a}"));
        QVERIFY(!missing.ok());
    }

    void holes()
    {
        // An empty argument is not a mistake: it is drawn as a box to fill.
        const Parsed parsed = Latex::parse(QStringLiteral("\\frac{}{}"));
        QVERIFY(parsed.ok());
        const NodePtr frac = only(parsed);
        QVERIFY(frac->kids.at(0)->hole);
        QVERIFY(frac->kids.at(1)->hole);
    }

    void positions()
    {
        const QString source = QStringLiteral("x + \\sqrt{y}");
        const Parsed parsed = Latex::parse(source);
        QCOMPARE(parsed.root->kids.size(), 3);
        QCOMPARE(parsed.root->kids.at(0)->start, 0);
        QCOMPARE(parsed.root->kids.at(0)->end, 1);
        QCOMPARE(parsed.root->kids.at(1)->start, 2);
        QCOMPARE(parsed.root->kids.at(2)->start, 4);
        QCOMPARE(parsed.root->kids.at(2)->end, source.size());
    }

    void matchingBrace()
    {
        const QString source = QStringLiteral("\\frac{a{b}}{c} \\{ }");
        QCOMPARE(Latex::matchingBrace(source, 5), 10);
        QCOMPARE(Latex::matchingBrace(source, 10), 5);
        QCOMPARE(Latex::matchingBrace(source, 7), 9);
        QCOMPARE(Latex::matchingBrace(source, 11), 13);
        // An escaped brace is a character, not a group.
        QCOMPARE(Latex::matchingBrace(source, 16), -1);
        QCOMPARE(Latex::matchingBrace(source, 18), -1);
        QCOMPARE(Latex::matchingBrace(source, 0), -1);
        QCOMPARE(Latex::matchingBrace(source, 99), -1);
    }

    void neverCrashes_data()
    {
        QTest::addColumn<QString>("source");
        const QStringList sources = {
            QStringLiteral("\\"), QStringLiteral("{"), QStringLiteral("}"), QStringLiteral("^"), QStringLiteral("_"), QStringLiteral("&"),
            QStringLiteral("\\\\"), QStringLiteral("\\frac"), QStringLiteral("\\sqrt["), QStringLiteral("\\left"), QStringLiteral("\\right)"),
            QStringLiteral("\\begin"), QStringLiteral("\\begin{"), QStringLiteral("\\end{pmatrix}"), QStringLiteral("\\color"), QStringLiteral("\\text{"),
            QStringLiteral("x^"), QStringLiteral("x_{"), QStringLiteral("\\hat"), QStringLiteral("\\middle|"), QStringLiteral("a & b \\\\ c"),
            QStringLiteral("{{{{{{"), QStringLiteral("\\left(\\left(\\left("), QStringLiteral("% a comment"), QStringLiteral("$x$"), QStringLiteral("ñ á € 😀"),
        };
        for (const QString &source : sources) {
            QTest::newRow(qPrintable(source)) << source;
        }
    }
    void neverCrashes()
    {
        QFETCH(QString, source);
        const Parsed parsed = Latex::parse(source);
        QVERIFY(parsed.root);
        for (const ParseError &error : parsed.errors) {
            QVERIFY(error.position >= 0);
            QVERIFY(error.position <= source.size());
            QVERIFY(!error.message.isEmpty());
        }
    }
};

QTEST_APPLESS_MAIN(TestLatex)
#include "tst_latex.moc"
