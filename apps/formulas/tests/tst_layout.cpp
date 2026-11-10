#include "layout.h"
#include "mathfont.h"

#include <QtTest>

class TestLayout : public QObject
{
    Q_OBJECT

    static Box box(const QString &latex, qreal size = 40, bool display = true)
    {
        LayoutOptions options;
        options.size = size;
        options.display = display;
        return Formula::layout(latex, options).box;
    }
    /// The ink of what comes from within [from, to) of the source; `exact` for that very range (a delimiter's is its whole group).
    static QRectF ink(const Box &box, int from, int to, bool exact = false)
    {
        QRectF out;
        for (const Item &item : box.items) {
            if (exact ? (item.start == from && item.end == to) : (item.start >= from && item.end <= to)) {
                out |= item.path.boundingRect();
            }
        }
        return out;
    }

private Q_SLOTS:
    void font()
    {
        const MathFont *font = MathFont::get();
        QVERIFY(font);
        QVERIFY(!font->family().isEmpty());
        QVERIFY(font->glyph(U'x') != 0);
        QVERIFY(font->axis() > 0.1 && font->axis() < 0.5);
        QVERIFY(font->ruleThickness() > 0.01 && font->ruleThickness() < 0.2);
        // An unknown family gives the default, never nothing.
        QCOMPARE(MathFont::get(QStringLiteral("No Such Family 123"))->family(), font->family());
        for (const QString &family : MathFont::available()) {
            QVERIFY2(MathFont::get(family)->hasMath(), qPrintable(family));
        }
    }

    void emptyIsEmpty()
    {
        const Box empty = box(QString());
        QVERIFY(empty.isEmpty());
        QCOMPARE(empty.width, 0.0);
    }

    void scalesWithSize()
    {
        const Box small = box(QStringLiteral("x+\\frac{1}{2}"), 20);
        const Box large = box(QStringLiteral("x+\\frac{1}{2}"), 40);
        QVERIFY(qAbs(large.width - 2 * small.width) < 0.01);
        QVERIFY(qAbs(large.height - 2 * small.height) < 0.01);
        QVERIFY(qAbs(large.depth - 2 * small.depth) < 0.01);
    }

    void spacingByAtom()
    {
        // TeX: a medium space around a binary operator, a thick one around a relation, none between ordinary atoms.
        const qreal ab = box(QStringLiteral("ab")).width;
        const qreal a = box(QStringLiteral("a")).width;
        const qreal b = box(QStringLiteral("b")).width;
        QVERIFY(qAbs(ab - a - b) < 2.5);
        const qreal plus = box(QStringLiteral("a+b")).width - ab - box(QStringLiteral("+")).width;
        const qreal equals = box(QStringLiteral("a=b")).width - ab - box(QStringLiteral("=")).width;
        QVERIFY2(qAbs(plus - 2 * 4.0 / 18 * 40) < 1.5, qPrintable(QString::number(plus)));
        QVERIFY2(qAbs(equals - 2 * 5.0 / 18 * 40) < 1.5, qPrintable(QString::number(equals)));
        // A sign is not an operation: no space after a leading minus.
        const qreal minus = box(QStringLiteral("-b")).width - b - box(QStringLiteral("-")).width;
        QVERIFY2(qAbs(minus) < 1.5, qPrintable(QString::number(minus)));
        // Nor in scripts.
        const Box script = box(QStringLiteral("x^{a+b}"));
        const Box loose = box(QStringLiteral("x^{a}"));
        QVERIFY(script.width - loose.width < plus + box(QStringLiteral("+")).width + b);
        // The spaces of the source mean nothing.
        QCOMPARE(box(QStringLiteral("a + b")).width, box(QStringLiteral("a+b")).width);
    }

    void explicitSpaces()
    {
        const qreal ab = box(QStringLiteral("ab")).width;
        QVERIFY(qAbs(box(QStringLiteral("a\\quad b")).width - ab - 40) < 0.5);
        QVERIFY(qAbs(box(QStringLiteral("a\\qquad b")).width - ab - 80) < 0.5);
        QVERIFY(qAbs(box(QStringLiteral("a\\,b")).width - ab - 40.0 * 3 / 18) < 0.5);
        QVERIFY(box(QStringLiteral("a\\!b")).width < ab);
    }

    void fractionOnTheAxis()
    {
        const QString source = QStringLiteral("\\frac{a}{b}");
        const Box frac = box(source);
        const qreal axis = MathFont::get()->axis() * 40;
        const QRectF numerator = ink(frac, 6, 7);
        const QRectF denominator = ink(frac, 9, 10);
        QVERIFY(numerator.isValid() && denominator.isValid());
        // One above the axis, one below, clear of the rule.
        QVERIFY(numerator.bottom() < -axis);
        QVERIFY(denominator.top() > -axis);
        QVERIFY(frac.height > numerator.height());
        QVERIFY(frac.depth > 0);
        // Both centred on the bar.
        const Box wide = box(QStringLiteral("\\frac{a}{b+c+d}"));
        const QRectF top = ink(wide, 6, 7);
        const QRectF bottom = ink(wide, 9, 14);
        QVERIFY(qAbs(top.center().x() - bottom.center().x()) < 2.5);
        QVERIFY(top.width() < bottom.width());
    }

    void fractionsShrinkInText()
    {
        const Box display = box(QStringLiteral("\\frac{a}{b}"), 40, true);
        const Box inText = box(QStringLiteral("\\frac{a}{b}"), 40, false);
        QVERIFY(inText.height + inText.depth < display.height + display.depth);
        // \dfrac asks for the large one wherever it is.
        const Box forced = box(QStringLiteral("\\dfrac{a}{b}"), 40, false);
        QVERIFY(qAbs(forced.height - display.height) < 0.01);
        // Nested, each level is smaller, down to the script-script size.
        const Box nested = box(QStringLiteral("\\frac{\\frac{a}{b}}{c}"));
        QVERIFY(nested.height > display.height);
        QVERIFY(nested.height < 2 * display.height);
    }

    void scriptsAreSmallerAndShifted()
    {
        const Box sup = box(QStringLiteral("x^2"));
        const QRectF base = ink(sup, 0, 1);
        const QRectF exponent = ink(sup, 2, 3);
        const QRectF full = ink(box(QStringLiteral("2")), 0, 1);
        const qreal down = MathFont::get()->constant(MathFont::ScriptPercentScaleDown);
        QVERIFY(down > 0.5 && down < 0.9);
        QVERIFY(qAbs(exponent.height() - full.height() * down) < 1.0);
        QVERIFY(exponent.left() >= base.right() - 3);
        QVERIFY(exponent.bottom() < base.center().y());
        const Box sub = box(QStringLiteral("x_2"));
        const QRectF index = ink(sub, 2, 3);
        QVERIFY(index.bottom() > 0);
        QVERIFY(sub.depth > 0);
        // Both: one above the other, clear of each other.
        const Box both = box(QStringLiteral("x_1^2"));
        const QRectF lower = ink(both, 2, 3);
        const QRectF upper = ink(both, 4, 5);
        QVERIFY(upper.bottom() < lower.top());
        QVERIFY(both.width < sup.width + sub.width - base.width() + 3);
        // A script of a script is smaller still, and no smaller after that.
        const Box chain = box(QStringLiteral("x^{y^{z^{w}}}"));
        const qreal y = ink(chain, 3, 4).height();
        const qreal z = ink(box(QStringLiteral("x^{y^{z}}")), 6, 7).height();
        QVERIFY(z < y);
        QVERIFY(chain.height > sup.height);
    }

    void limitsInDisplayOnly()
    {
        const QString source = QStringLiteral("\\sum_{i}^{n}");
        const Box display = box(source, 40, true);
        const Box inText = box(source, 40, false);
        const QRectF sign = ink(display, 0, 4);
        const QRectF lower = ink(display, 6, 7);
        const QRectF upper = ink(display, 10, 11);
        // Above and below, centred.
        QVERIFY(upper.bottom() < sign.top());
        QVERIFY(lower.top() > sign.bottom());
        QVERIFY(qAbs(upper.center().x() - sign.center().x()) < 3);
        QVERIFY(qAbs(lower.center().x() - sign.center().x()) < 3);
        // To the side in a paragraph, and the sign smaller.
        const QRectF small = ink(inText, 0, 4);
        QVERIFY(ink(inText, 10, 11).left() >= small.right() - 2);
        QVERIFY(small.height() < sign.height());
        QVERIFY(inText.width > display.width);
        // An integral keeps them to the side, and the upper one clears its lean.
        const Box integral = box(QStringLiteral("\\int_{a}^{b}"));
        const QRectF intSign = ink(integral, 0, 4);
        QVERIFY(ink(integral, 6, 7).left() > intSign.center().x());
        QVERIFY(ink(integral, 10, 11).left() > ink(integral, 6, 7).left());
        QVERIFY(intSign.height() > 40);
        // …unless told otherwise.
        const Box forced = box(QStringLiteral("\\int\\limits_{a}^{b}"));
        QVERIFY(forced.width < integral.width);
    }

    void radicalCoversItsRadicand()
    {
        const Box plain = box(QStringLiteral("\\sqrt{x}"));
        const Box tall = box(QStringLiteral("\\sqrt{\\frac{a}{b}}"));
        const Box frac = box(QStringLiteral("\\frac{a}{b}"));
        QVERIFY(tall.height > frac.height);
        QVERIFY(tall.depth >= frac.depth);
        QVERIFY(tall.height > plain.height);
        QVERIFY(tall.width > frac.width);
        // The index sits in the crook, and makes it no narrower.
        const Box nth = box(QStringLiteral("\\sqrt[3]{x}"));
        QVERIFY(nth.width >= plain.width);
        const QRectF index = ink(nth, 6, 7);
        QVERIFY(index.isValid());
        QVERIFY(index.bottom() < 0);
        QVERIFY(index.left() < ink(nth, 9, 10).left());
        // The bar reaches over the whole radicand.
        const Box wide = box(QStringLiteral("\\sqrt{a+b+c}"));
        QRectF all;
        for (const Item &item : wide.items) {
            all |= item.path.boundingRect();
        }
        QVERIFY(all.right() >= ink(wide, 10, 11).right());
    }

    void delimitersGrow()
    {
        const Box small = box(QStringLiteral("\\left( x \\right)"));
        const Box tall = box(QStringLiteral("\\left( \\frac{a}{b} \\right)"));
        const Box taller = box(QStringLiteral("\\left( \\frac{\\frac{a}{b}}{\\frac{c}{d}} \\right)"));
        const Box fixed = box(QStringLiteral("( \\frac{a}{b} )"));
        QVERIFY(tall.height + tall.depth > small.height + small.depth);
        QVERIFY(taller.height + taller.depth > tall.height + tall.depth);
        // Without \left they stay as they are, and the fraction sticks out.
        const QRectF plain = ink(fixed, 0, 1);
        QVERIFY(plain.height() < fixed.height + fixed.depth - 4);
        // They cover (nearly) all of what they hold, centred on the axis.
        const QRectF paren = ink(tall, 0, 26, true);
        const Box inner = box(QStringLiteral("\\frac{a}{b}"));
        QVERIFY(paren.height() >= 0.9 * (inner.height + inner.depth));
        const qreal axis = MathFont::get()->axis() * 40;
        QVERIFY(qAbs(paren.center().y() + axis) < 3);
        // Very tall ones are built from pieces, and still cover.
        const Box huge = box(QStringLiteral("\\left\\{ \\begin{matrix} a \\\\ b \\\\ c \\\\ d \\\\ e \\\\ f \\end{matrix} \\right\\}"));
        QRectF brace;
        for (const Item &item : huge.items) {
            if (item.start == 0 && brace.isNull()) {
                brace = item.path.boundingRect();
            }
        }
        QVERIFY(brace.height() >= 0.9 * (huge.height + huge.depth) - 8);
        // The sizes by hand grow in order.
        qreal last = 0;
        for (const char *size : {"", "\\big", "\\Big", "\\bigg", "\\Bigg"}) {
            const Box sized = box(QLatin1String(size) + QStringLiteral("("));
            QVERIFY2(sized.height + sized.depth > last, size);
            last = sized.height + sized.depth;
        }
    }

    void matricesAlign()
    {
        const QString source = QStringLiteral("\\begin{pmatrix} a & bbb \\\\ ccc & d \\end{pmatrix}");
        const Box matrix = box(source);
        const QRectF a = ink(matrix, 16, 17);
        const QRectF b = ink(matrix, 20, 23);
        const QRectF c = ink(matrix, 27, 30);
        const QRectF d = ink(matrix, 33, 34);
        QVERIFY(a.isValid() && b.isValid() && c.isValid() && d.isValid());
        // Columns centred, rows on a common baseline.
        QVERIFY(qAbs(a.center().x() - c.center().x()) < 3);
        QVERIFY(qAbs(b.center().x() - d.center().x()) < 3);
        QVERIFY(c.top() > a.bottom());
        QVERIFY(b.left() > c.right());
        // The whole centred on the axis.
        const qreal axis = MathFont::get()->axis() * 40;
        QVERIFY(qAbs((matrix.depth - matrix.height) / 2 + axis) < 3);
        // cases: every column to the left.
        const Box cases = box(QStringLiteral("\\begin{cases} a & x \\\\ bbb & y \\end{cases}"));
        QVERIFY(qAbs(ink(cases, 14, 15).left() - ink(cases, 23, 26).left()) < 2.5);
        QVERIFY(qAbs(ink(cases, 18, 19).left() - ink(cases, 29, 30).left()) < 2.5);
        // aligned: the first column to the right, the second to the left.
        const Box aligned = box(QStringLiteral("\\begin{aligned} a &= b \\\\ ccc &= d \\end{aligned}"));
        QVERIFY(qAbs(ink(aligned, 16, 17).right() - ink(aligned, 26, 29).right()) < 3.5);
        QVERIFY(qAbs(ink(aligned, 19, 20).left() - ink(aligned, 31, 32).left()) < 2.5);
    }

    void accentsSitOnTop()
    {
        const Box plain = box(QStringLiteral("x"));
        for (const char *command : {"hat", "bar", "vec", "dot", "tilde", "overline"}) {
            const Box accented = box(QStringLiteral("\\%1{x}").arg(QLatin1String(command)));
            QVERIFY2(accented.height > plain.height, command);
            QVERIFY2(accented.items.size() > plain.items.size(), command);
            QVERIFY2(qAbs(accented.depth - plain.depth) < 0.5, command);
        }
        const Box under = box(QStringLiteral("\\underline{x}"));
        QVERIFY(under.depth > plain.depth);
        // A wide one covers what it is over.
        const Box wide = box(QStringLiteral("\\overline{a+b}"));
        QRectF rule;
        for (const Item &item : wide.items) {
            const QRectF r = item.path.boundingRect();
            if (r.width() > 40) {
                rule = r;
            }
        }
        QVERIFY(rule.isValid());
        QVERIFY(rule.width() >= box(QStringLiteral("a+b")).width - 6);
    }

    void integralLimitsReachItsEnds()
    {
        // Beside a tall integral the limits go to its top and its foot, not where a letter's would.
        const Box integral = box(QStringLiteral("\\int_{a}^{b} x"));
        const QRectF sign = ink(integral, 0, 4);
        const QRectF lower = ink(integral, 6, 7);
        const QRectF upper = ink(integral, 10, 11);
        QVERIFY(sign.height() > 60);
        QVERIFY(lower.left() > sign.left() && upper.left() > lower.left());
        QVERIFY(lower.bottom() > sign.bottom() - 4);
        QVERIFY(lower.top() > sign.center().y() + 8);
        QVERIFY(upper.top() < sign.top() + 4);
        QVERIFY(upper.bottom() < sign.center().y() - 8);
        // And above and below when asked.
        const Box stacked = box(QStringLiteral("\\int\\limits_{a}^{b} x"));
        QVERIFY(ink(stacked, 13, 14).top() >= ink(stacked, 0, 4).bottom());
        QVERIFY(ink(stacked, 17, 18).bottom() <= ink(stacked, 0, 4).top());
    }

    void scriptForms()
    {
        // A font with drawings of its own for scripts gives them: not the same shape made smaller.
        const MathFont *font = MathFont::get();
        const quint32 prime = font->glyph(0x2032);
        QCOMPARE(font->scriptForm(prime, 0), prime);
        if (font->scriptForm(prime, 1) == prime) {
            QSKIP("This font has no script forms.");
        }
        QVERIFY(font->bounds(font->scriptForm(prime, 1)).height() > font->bounds(prime).height());
        QVERIFY(font->scriptForm(prime, 2) != prime);
    }

    void fontStyles()
    {
        // The letters of a formula are italic, the digits and function names upright.
        const Box italic = box(QStringLiteral("R"));
        const Box roman = box(QStringLiteral("\\mathrm{R}"));
        const Box bold = box(QStringLiteral("\\mathbf{R}"));
        const Box blackboard = box(QStringLiteral("\\mathbb{R}"));
        const Box script = box(QStringLiteral("\\mathcal{R}"));
        const QList<QPainterPath> shapes = {italic.items.first().path, roman.items.first().path, bold.items.first().path, blackboard.items.first().path, script.items.first().path};
        for (int i = 0; i < shapes.size(); ++i) {
            for (int j = i + 1; j < shapes.size(); ++j) {
                QVERIFY2(shapes.at(i) != shapes.at(j), qPrintable(QStringLiteral("%1 %2").arg(i).arg(j)));
            }
        }
        // A function's name is upright, and stands apart from what it is applied to.
        const Box named = box(QStringLiteral("\\sin x"));
        const Box letters = box(QStringLiteral("sinx"));
        QVERIFY(named.items.first().path != letters.items.first().path);
        QVERIFY(named.items.last().path.boundingRect().left() - named.items.at(named.items.size() - 2).path.boundingRect().right()
            > letters.items.last().path.boundingRect().left() - letters.items.at(letters.items.size() - 2).path.boundingRect().right() + 2);
    }

    void colour()
    {
        const Box coloured = box(QStringLiteral("a + \\color{red}{b}"));
        QVERIFY(!coloured.items.first().color.isValid());
        QCOMPARE(coloured.items.last().color, QColor(Qt::red));
        const QImage image = Formula::image(coloured, Qt::black, 1, 4, Qt::white);
        bool red = false;
        bool black = false;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                red = red || (c.red() > 200 && c.green() < 60 && c.blue() < 60);
                black = black || (c.red() < 40 && c.green() < 40 && c.blue() < 40);
            }
        }
        QVERIFY(red);
        QVERIFY(black);
    }

    void errorsDoNotStopTheDrawing()
    {
        const Layout layout = Formula::layout(QStringLiteral("a + \\nosuch + \\frac{b}{c"));
        QVERIFY(!layout.errors.isEmpty());
        QVERIFY(!layout.box.isEmpty());
        QVERIFY(layout.box.width > box(QStringLiteral("a +")).width);
        // The mistake is drawn in its own colour.
        bool marked = false;
        for (const Item &item : layout.box.items) {
            marked = marked || (item.color.isValid() && item.start == 4);
        }
        QVERIFY(marked);
    }

    void holesAreGhosts()
    {
        const Box frac = box(QStringLiteral("\\frac{}{}"));
        // A box drawn faint where each argument goes, and the bar as ever.
        int ghosts = 0;
        int solid = 0;
        for (const Item &item : frac.items) {
            ghosts += item.ghost;
            solid += !item.ghost;
        }
        QVERIFY(ghosts >= 2);
        QCOMPARE(solid, 1);
        QVERIFY(ink(frac, 5, 7).isValid());
        QVERIFY(ink(frac, 7, 9).top() > ink(frac, 5, 7).bottom());
        QVERIFY(frac.width > 10);
    }

    void sourceUnderThePointer()
    {
        const QString source = QStringLiteral("x + \\frac{a}{b}");
        const Box formula = box(source);
        const QRectF x = ink(formula, 0, 1);
        // The cursor goes after what was pressed, ready to go on writing.
        QCOMPARE(Formula::sourceAt(formula, x.center()), 1);
        const QRectF a = ink(formula, 10, 11);
        QCOMPARE(Formula::sourceAt(formula, a.center()), 11);
        const QRectF b = ink(formula, 13, 14);
        QCOMPARE(Formula::sourceAt(formula, b.center()), 14);
        // Beside the formula, the nearest thing; with nothing drawn, nothing.
        QCOMPARE(Formula::sourceAt(formula, QPointF(-500, 0)), 1);
        QCOMPARE(Formula::sourceAt(Box(), QPointF(0, 0)), -1);
        // And back: where what the cursor is in is drawn.
        QVERIFY(Formula::rectOf(formula, 10).intersects(a));
        QVERIFY(!Formula::rectOf(formula, 10).intersects(x));
        QVERIFY(Formula::rectOf(formula, 0).intersects(x));
    }

    void picture()
    {
        const Box formula = box(QStringLiteral("E = mc^2"), 32);
        const QImage one = Formula::image(formula, Qt::black, 1, 8);
        const QImage two = Formula::image(formula, Qt::black, 2, 8);
        QVERIFY(one.hasAlphaChannel());
        QCOMPARE(one.pixelColor(0, 0).alpha(), 0);
        QVERIFY(qAbs(two.width() - 2 * one.width()) <= 2);
        QVERIFY(qAbs(one.width() - (formula.width + 16)) <= 2);
        QVERIFY(qAbs(one.height() - (formula.height + formula.depth + 16)) <= 2);
        // Nothing is cut: the border is empty.
        for (int x = 0; x < one.width(); ++x) {
            QCOMPARE(one.pixelColor(x, 0).alpha(), 0);
            QCOMPARE(one.pixelColor(x, one.height() - 1).alpha(), 0);
        }
        for (int y = 0; y < one.height(); ++y) {
            QCOMPARE(one.pixelColor(0, y).alpha(), 0);
            QCOMPARE(one.pixelColor(one.width() - 1, y).alpha(), 0);
        }
        const QImage white = Formula::image(formula, Qt::black, 1, 8, Qt::white);
        QCOMPARE(white.pixelColor(0, 0), QColor(Qt::white));
    }

    void svg()
    {
        const Box formula = box(QStringLiteral("\\frac{a}{b} + \\color{red}{c}"), 32);
        const QByteArray svg = Formula::svg(formula, QColor(QStringLiteral("#123456")), 4);
        QVERIFY(svg.contains("<svg"));
        QVERIFY(svg.contains("xmlns=\"http://www.w3.org/2000/svg\""));
        QVERIFY(svg.contains("viewBox"));
        QVERIFY(svg.contains("<path"));
        QVERIFY(svg.contains("#123456"));
        QVERIFY(svg.contains("#ff0000"));
        // Outlines only: it needs no font where it is opened.
        QVERIFY(!svg.contains("<text"));
        QVERIFY(!svg.contains("font-family"));
    }

    void hardOnesFitTheirBox_data()
    {
        QTest::addColumn<QString>("source");
        QTest::newRow("nested fractions") << "\\frac{1}{1+\\frac{1}{1+\\frac{1}{x}}}";
        QTest::newRow("integral") << "\\int_{-\\infty}^{\\infty} e^{-x^2} \\, dx = \\sqrt{\\pi}";
        QTest::newRow("matrix") << "\\begin{pmatrix} a_{11} & \\cdots & a_{1n} \\\\ \\vdots & \\ddots & \\vdots \\\\ a_{m1} & \\cdots & a_{mn} \\end{pmatrix}";
        QTest::newRow("tall root") << "\\sqrt{\\frac{\\sum_{i=1}^{n} (x_i - \\bar{x})^2}{n-1}}";
        QTest::newRow("chains") << "x_{i_{j_k}}^{a^{b^c}}";
        QTest::newRow("cases") << "f(x) = \\begin{cases} x^2 & \\text{si } x \\geq 0 \\\\ -x & \\text{si } x < 0 \\end{cases}";
        QTest::newRow("accents") << "\\hat{x} + \\vec{v} + \\overline{a+b} + \\underbrace{a+b}_{n}";
        QTest::newRow("delimiters") << "\\left\\langle \\frac{a}{b} \\middle| \\frac{c}{d} \\right\\rangle";
    }
    void hardOnesFitTheirBox()
    {
        QFETCH(QString, source);
        LayoutOptions options;
        options.size = 40;
        const Layout layout = Formula::layout(source, options);
        QVERIFY2(layout.errors.isEmpty(), qPrintable(layout.errors.value(0).message));
        QRectF all;
        for (const Item &item : layout.box.items) {
            all |= item.path.boundingRect();
        }
        // The ink is inside what the box says it takes, bar the overhang of a glyph.
        QVERIFY2(all.top() >= -layout.box.height - 3, "above");
        QVERIFY2(all.bottom() <= layout.box.depth + 3, "below");
        QVERIFY2(all.left() >= -4, "left");
        QVERIFY2(all.right() <= layout.box.width + 4, "right");
    }
};

QTEST_MAIN(TestLayout)
#include "tst_layout.moc"
