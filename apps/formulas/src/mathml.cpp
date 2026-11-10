#include "formats.h"

#include "latex.h"
#include "zip.h"

#include <QDomDocument>
#include <QHash>
#include <QJsonDocument>
#include <QRegularExpression>

// MathML and StarMath, the two languages of an OpenDocument formula: both are
// written from the tree, and MathML is read back into LaTeX.

namespace {

QString esc(const QString &text)
{
    return text.toHtmlEscaped();
}

bool isLetters(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }
    for (const QChar c : text) {
        if (!c.isLetter() && !c.isHighSurrogate() && !c.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool isBigOperator(const NodePtr &node)
{
    return node && node->type == Node::Symbol && (node->big || (node->upright && node->atom == Atom::Op));
}

/// Scripts go over and under the base.
bool overUnder(const NodePtr &script)
{
    const NodePtr base = script->kids.value(0);
    if (script->stacked) {
        return true;
    }
    return base && base->type == Node::Symbol && (base->limits == 1 || (base->movable && base->limits != 2));
}

const QHash<QString, QString> &variants()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("rm"), QStringLiteral("normal")}, {QStringLiteral("bf"), QStringLiteral("bold")},
        {QStringLiteral("it"), QStringLiteral("italic")}, {QStringLiteral("bs"), QStringLiteral("bold-italic")},
        {QStringLiteral("bb"), QStringLiteral("double-struck")}, {QStringLiteral("cal"), QStringLiteral("script")},
        {QStringLiteral("frak"), QStringLiteral("fraktur")}, {QStringLiteral("sf"), QStringLiteral("sans-serif")},
        {QStringLiteral("tt"), QStringLiteral("monospace")},
    };
    return map;
}

struct AccentInfo {
    const char *command;
    char32_t mark;
    const char *star;
};
const QList<AccentInfo> &accents()
{
    static const QList<AccentInfo> list = {
        {"hat", 0x5E, "hat"}, {"widehat", 0x5E, "widehat"}, {"bar", 0xAF, "bar"}, {"vec", 0x2192, "vec"},
        {"dot", 0x2D9, "dot"}, {"ddot", 0xA8, "ddot"}, {"tilde", 0x7E, "tilde"}, {"widetilde", 0x7E, "widetilde"},
        {"check", 0x2C7, "check"}, {"breve", 0x2D8, "breve"}, {"acute", 0xB4, "acute"}, {"grave", 0x60, "grave"},
        {"mathring", 0x2DA, "circle"}, {"overline", 0x203E, "overline"}, {"underline", 0x5F, "underline"},
        {"overbrace", 0x23DE, "overbrace"}, {"underbrace", 0x23DF, "underbrace"},
        {"overrightarrow", 0x2192, "widevec"}, {"overleftarrow", 0x2190, "widevec"},
    };
    return list;
}
const AccentInfo *accent(const QString &command)
{
    for (const AccentInfo &info : accents()) {
        if (command == QLatin1String(info.command)) {
            return &info;
        }
    }
    return nullptr;
}

// ---- MathML, written -------------------------------------------------------

QString ml(const NodePtr &node);

QString mlRow(const QList<NodePtr> &kids)
{
    QString out;
    for (const NodePtr &kid : kids) {
        out += ml(kid);
    }
    return out;
}

/// Always one element, as the arguments of mfrac, msub… must be.
QString mlOne(const NodePtr &node)
{
    if (!node) {
        return QStringLiteral("<mrow/>");
    }
    if (node->type == Node::Row && !node->hole) {
        if (node->kids.size() == 1 && node->kids.first() && node->kids.first()->type != Node::Row && !node->kids.first()->flows) {
            return mlOne(node->kids.first());
        }
        return QStringLiteral("<mrow>") + mlRow(node->kids) + QStringLiteral("</mrow>");
    }
    if (node->flows || node->type == Node::Space) {
        return QStringLiteral("<mrow>") + ml(node) + QStringLiteral("</mrow>");
    }
    return ml(node);
}

QString fence(const QString &text)
{
    return QStringLiteral("<mo fence=\"true\" stretchy=\"true\">") + esc(text) + QStringLiteral("</mo>");
}

QString mlSymbol(const NodePtr &node)
{
    const QString text = esc(node->text);
    if (node->middle) {
        return QStringLiteral("<mo fence=\"true\" stretchy=\"true\" form=\"infix\">") + text + QStringLiteral("</mo>");
    }
    if (node->sized > 0) {
        const QString size = QString::number(1.2 + 0.6 * (node->sized - 1), 'f', 1);
        return QStringLiteral("<mo minsize=\"%1em\" maxsize=\"%1em\">").arg(size) + text + QStringLiteral("</mo>");
    }
    if (node->upright && !node->text.isEmpty() && node->text.at(0).isDigit()) {
        return QStringLiteral("<mn>") + text + QStringLiteral("</mn>");
    }
    switch (node->atom) {
    case Atom::Ord:
        if (isLetters(node->text)) {
            const bool single = node->text.toUcs4().size() == 1;
            return (node->upright && single ? QStringLiteral("<mi mathvariant=\"normal\">") : QStringLiteral("<mi>")) + text + QStringLiteral("</mi>");
        }
        return QStringLiteral("<mo>") + text + QStringLiteral("</mo>");
    case Atom::Op:
        if (node->upright) {
            return QStringLiteral("<mi>") + text + QStringLiteral("</mi>");
        }
        return QStringLiteral("<mo>") + text + QStringLiteral("</mo>");
    case Atom::Open:
    case Atom::Close:
        return QStringLiteral("<mo stretchy=\"false\">") + text + QStringLiteral("</mo>");
    default:
        return QStringLiteral("<mo>") + text + QStringLiteral("</mo>");
    }
}

QString mlTable(const NodePtr &node)
{
    QString align;
    for (const QChar c : node->columns) {
        align += (align.isEmpty() ? QString() : QStringLiteral(" "))
            + (c == QLatin1Char('l') ? QStringLiteral("left") : c == QLatin1Char('r') ? QStringLiteral("right") : QStringLiteral("center"));
    }
    QString out = align.isEmpty() || align == QLatin1String("center") ? QStringLiteral("<mtable>") : QStringLiteral("<mtable columnalign=\"%1\">").arg(align);
    for (const QList<NodePtr> &line : node->cells) {
        out += QStringLiteral("<mtr>");
        for (const NodePtr &cell : line) {
            out += QStringLiteral("<mtd>") + (cell ? mlRow(cell->kids) : QString()) + QStringLiteral("</mtd>");
        }
        out += QStringLiteral("</mtr>");
    }
    return out + QStringLiteral("</mtable>");
}

QString ml(const NodePtr &node)
{
    if (!node) {
        return QString();
    }
    switch (node->type) {
    case Node::Row:
        if (node->hole) {
            return QStringLiteral("<mrow/>");
        }
        return node->flows ? mlRow(node->kids) : QStringLiteral("<mrow>") + mlRow(node->kids) + QStringLiteral("</mrow>");
    case Node::Symbol:
        return mlSymbol(node);
    case Node::Text:
        return QStringLiteral("<mtext>") + esc(node->text) + QStringLiteral("</mtext>");
    case Node::Frac: {
        QString out = (node->rule ? QStringLiteral("<mfrac>") : QStringLiteral("<mfrac linethickness=\"0\">"))
            + mlOne(node->kids.value(0)) + mlOne(node->kids.value(1)) + QStringLiteral("</mfrac>");
        if (!node->left.isEmpty() || !node->right.isEmpty()) {
            out = QStringLiteral("<mrow>") + fence(node->left) + out + fence(node->right) + QStringLiteral("</mrow>");
        }
        return out;
    }
    case Node::Sqrt:
        if (node->kids.value(1)) {
            return QStringLiteral("<mroot>") + mlOne(node->kids.value(0)) + mlOne(node->kids.value(1)) + QStringLiteral("</mroot>");
        }
        return QStringLiteral("<msqrt>") + mlOne(node->kids.value(0)) + QStringLiteral("</msqrt>");
    case Node::Script: {
        const NodePtr sub = node->kids.value(1);
        const NodePtr sup = node->kids.value(2);
        const bool stack = overUnder(node);
        const QString tag = sub && sup ? (stack ? QStringLiteral("munderover") : QStringLiteral("msubsup"))
            : sub ? (stack ? QStringLiteral("munder") : QStringLiteral("msub")) : (stack ? QStringLiteral("mover") : QStringLiteral("msup"));
        return QLatin1Char('<') + tag + QLatin1Char('>') + mlOne(node->kids.value(0)) + (sub ? mlOne(sub) : QString()) + (sup ? mlOne(sup) : QString())
            + QStringLiteral("</") + tag + QLatin1Char('>');
    }
    case Node::Delim: {
        const NodePtr body = node->kids.value(0);
        return QStringLiteral("<mrow>") + fence(node->left) + QStringLiteral("<mrow>") + (body ? mlRow(body->kids) : QString()) + QStringLiteral("</mrow>")
            + fence(node->right) + QStringLiteral("</mrow>");
    }
    case Node::Matrix: {
        const QString table = mlTable(node);
        if (node->left.isEmpty() && node->right.isEmpty()) {
            return table;
        }
        return QStringLiteral("<mrow>") + fence(node->left) + table + fence(node->right) + QStringLiteral("</mrow>");
    }
    case Node::Accent: {
        const AccentInfo *info = accent(node->text);
        const QString mark = info ? QString::fromUcs4(&info->mark, 1) : QStringLiteral("^");
        const bool under = node->text.startsWith(QLatin1String("under"));
        const bool wide = node->text.size() > 5;
        const QString tag = under ? QStringLiteral("munder") : QStringLiteral("mover");
        return QLatin1Char('<') + tag + (under ? QStringLiteral(" accentunder=\"true\">") : QStringLiteral(" accent=\"true\">")) + mlOne(node->kids.value(0))
            + (wide ? QStringLiteral("<mo stretchy=\"true\">") : QStringLiteral("<mo stretchy=\"false\">")) + esc(mark) + QStringLiteral("</mo></") + tag
            + QLatin1Char('>');
    }
    case Node::Style: {
        const NodePtr body = node->kids.value(0);
        const QString inside = body && body->type == Node::Row && !body->hole ? mlRow(body->kids) : ml(body);
        if (node->text == QLatin1String("font")) {
            const QString variant = variants().value(node->arg);
            return variant.isEmpty() ? QStringLiteral("<mrow>") + inside + QStringLiteral("</mrow>")
                : QStringLiteral("<mstyle mathvariant=\"%1\">").arg(variant) + inside + QStringLiteral("</mstyle>");
        }
        if (node->text == QLatin1String("color")) {
            const QString name = Latex::colorName(node->arg);
            return QStringLiteral("<mstyle mathcolor=\"%1\">").arg(name.isEmpty() ? QStringLiteral("#000000") : QColor(name).name()) + inside
                + QStringLiteral("</mstyle>");
        }
        if (node->text == QLatin1String("math")) {
            const int style = node->arg.toInt();
            return QStringLiteral("<mstyle displaystyle=\"%1\" scriptlevel=\"%2\">").arg(style == 0 ? QStringLiteral("true") : QStringLiteral("false")).arg(qMax(0, style - 1))
                + inside + QStringLiteral("</mstyle>");
        }
        if (node->text == QLatin1String("box")) {
            return QStringLiteral("<menclose notation=\"box\">") + inside + QStringLiteral("</menclose>");
        }
        if (node->text == QLatin1String("phantom")) {
            return QStringLiteral("<mphantom>") + inside + QStringLiteral("</mphantom>");
        }
        return QStringLiteral("<mrow>") + inside + QStringLiteral("</mrow>");
    }
    case Node::Space:
        return QStringLiteral("<mspace width=\"%1em\"/>").arg(QString::number(node->width / 18.0, 'f', 3));
    case Node::Error:
        return QStringLiteral("<merror><mtext>") + esc(node->text) + QStringLiteral("</mtext></merror>");
    }
    return QString();
}

// ---- StarMath, written -----------------------------------------------------

QString quoted(const QString &text)
{
    QString safe = text;
    safe.replace(QLatin1Char('"'), QStringLiteral("''"));
    return QLatin1Char('"') + safe + QLatin1Char('"');
}

const QHash<QString, QString> &starNames()
{
    static const QHash<QString, QString> map = {
        {"times", "times"}, {"cdot", "cdot"}, {"div", "div"}, {"pm", "+-"}, {"mp", "-+"}, {"leq", "<="}, {"le", "<="}, {"geq", ">="}, {"ge", ">="},
        {"neq", "<>"}, {"ne", "<>"}, {"approx", "approx"}, {"equiv", "equiv"}, {"sim", "sim"}, {"simeq", "simeq"}, {"propto", "prop"}, {"in", "in"},
        {"notin", "notin"}, {"ni", "owns"}, {"subset", "subset"}, {"subseteq", "subseteq"}, {"supset", "supset"}, {"supseteq", "supseteq"},
        {"cup", "union"}, {"cap", "intersection"}, {"setminus", "setminus"}, {"infty", "infinity"}, {"partial", "partial"}, {"nabla", "nabla"},
        {"forall", "forall"}, {"exists", "exists"}, {"nexists", "notexists"}, {"to", "toward"}, {"rightarrow", "rightarrow"}, {"leftarrow", "leftarrow"},
        {"Rightarrow", "drarrow"}, {"implies", "drarrow"}, {"Leftarrow", "dlarrow"}, {"Leftrightarrow", "dlrarrow"}, {"iff", "dlrarrow"},
        {"uparrow", "uparrow"}, {"downarrow", "downarrow"}, {"ll", "<<"}, {"gg", ">>"}, {"emptyset", "emptyset"}, {"varnothing", "emptyset"},
        {"neg", "neg"}, {"lnot", "neg"}, {"land", "and"}, {"wedge", "and"}, {"lor", "or"}, {"vee", "or"}, {"cdots", "dotsaxis"}, {"ldots", "dotslow"},
        {"dots", "dotslow"}, {"vdots", "dotsvert"}, {"ddots", "dotsdown"}, {"hbar", "hbar"}, {"Re", "Re"}, {"Im", "Im"}, {"aleph", "aleph"},
        {"perp", "ortho"}, {"parallel", "parallel"}, {"circ", "circ"}, {"mid", "divides"}, {"nmid", "ndivides"}, {"oplus", "oplus"},
        {"ominus", "ominus"}, {"otimes", "otimes"}, {"odot", "odot"}, {"ast", "*"}, {"wp", "wp"}, {"prec", "prec"}, {"succ", "succ"},
        {"preceq", "preceq"}, {"succeq", "succeq"}, {"cong", "approx"}, {"sum", "sum"}, {"prod", "prod"}, {"coprod", "coprod"}, {"int", "int"},
        {"iint", "iint"}, {"iiint", "iiint"}, {"oint", "lint"}, {"oiint", "llint"}, {"oiiint", "lllint"}, {"lim", "lim"}, {"limsup", "limsup"},
        {"liminf", "liminf"}, {"sin", "sin"}, {"cos", "cos"}, {"tan", "tan"}, {"cot", "cot"}, {"sinh", "sinh"}, {"cosh", "cosh"}, {"tanh", "tanh"},
        {"coth", "coth"}, {"arcsin", "arcsin"}, {"arccos", "arccos"}, {"arctan", "arctan"}, {"ln", "ln"}, {"log", "log"}, {"exp", "exp"},
        {"langle", "langle"}, {"rangle", "rangle"}, {"lfloor", "lfloor"}, {"rfloor", "rfloor"}, {"lceil", "lceil"}, {"rceil", "rceil"},
        {"lbrace", "lbrace"}, {"rbrace", "rbrace"},
    };
    return map;
}

QString starDelimiter(const QString &text, bool left)
{
    if (text.isEmpty()) {
        return QStringLiteral("none");
    }
    switch (text.at(0).unicode()) {
    case '(': case ')': case '[': case ']': return text;
    case '{': return QStringLiteral("lbrace");
    case '}': return QStringLiteral("rbrace");
    case '|': return left ? QStringLiteral("lline") : QStringLiteral("rline");
    case 0x2016: return left ? QStringLiteral("ldline") : QStringLiteral("rdline");
    case 0x27E8: return QStringLiteral("langle");
    case 0x27E9: return QStringLiteral("rangle");
    case 0x230A: return QStringLiteral("lfloor");
    case 0x230B: return QStringLiteral("rfloor");
    case 0x2308: return QStringLiteral("lceil");
    case 0x2309: return QStringLiteral("rceil");
    default: return left ? QStringLiteral("lline") : QStringLiteral("rline");
    }
}

QString star(const NodePtr &node);
QString starRow(const QList<NodePtr> &kids);

QString starGroup(const NodePtr &node)
{
    if (!node) {
        return QStringLiteral("{}");
    }
    const QString inside = node->type == Node::Row && !node->hole ? starRow(node->kids) : star(node);
    return QStringLiteral("{") + inside + QStringLiteral("}");
}

/// 'b' a binary operator or relation, 'p' an operator that wants what follows, 'o' an operand, ' ' nothing.
QChar starKind(const NodePtr &node)
{
    if (!node) {
        return QLatin1Char(' ');
    }
    switch (node->type) {
    case Node::Space:
        return QLatin1Char(' ');
    case Node::Symbol:
        if (node->middle || node->sized > 0) {
            return QLatin1Char('o');
        }
        if (node->atom == Atom::Bin || node->atom == Atom::Rel) {
            return QLatin1Char('b');
        }
        if (isBigOperator(node)) {
            return QLatin1Char('p');
        }
        if (node->atom == Atom::Punct) {
            return QLatin1Char(',');
        }
        return QLatin1Char('o');
    case Node::Script:
        return isBigOperator(node->kids.value(0)) ? QLatin1Char('p') : QLatin1Char('o');
    default:
        return QLatin1Char('o');
    }
}

QString starSymbol(const NodePtr &node)
{
    const QString &text = node->text;
    if (node->middle) {
        return QStringLiteral("mline");
    }
    if (!node->command.isEmpty()) {
        if (const SymbolInfo *info = Symbols::find(node->command)) {
            if (QLatin1String(info->category) == QLatin1String("greek")) {
                const QString name = node->command;
                return name.at(0).isUpper() ? QLatin1Char('%') + name.toUpper() : QStringLiteral("%i") + name;
            }
        }
        const QString name = starNames().value(node->command);
        if (!name.isEmpty()) {
            return name;
        }
        if (node->upright && node->atom == Atom::Op) {
            return QStringLiteral("func ") + text;
        }
        if (node->big) {
            return QStringLiteral("oper ") + text;
        }
    }
    if (text.size() == 1) {
        switch (text.at(0).unicode()) {
        case 0x2212: return QStringLiteral("-");
        case 0x2217: return QStringLiteral("*");
        case 0x2260: return QStringLiteral("<>");
        case 0x2209: return QStringLiteral("notin");
        case 0x2032: return QStringLiteral("'");
        case '!': return QStringLiteral("fact");
        case '+': case '=': case '<': case '>': case ',': case ';': case '/': return text;
        default: break;
        }
        if (text.at(0).isLetterOrNumber() && text.at(0).unicode() < 128) {
            return text;
        }
    }
    if (node->upright && node->atom == Atom::Op && isLetters(text)) {
        return QStringLiteral("func ") + text;
    }
    bool plain = !text.isEmpty();
    for (const QChar c : text) {
        plain = plain && (c.isDigit() || c == QLatin1Char('.'));
    }
    return plain ? text : quoted(text);
}

/// The bracket a plain symbol is, as StarMath names it, with the one that closes it.
bool starBracket(const NodePtr &node, QString *name, QString *closes)
{
    if (!node || node->type != Node::Symbol || node->middle || node->sized > 0 || (node->atom != Atom::Open && node->atom != Atom::Close) || node->text.size() != 1) {
        return false;
    }
    const bool left = node->atom == Atom::Open;
    switch (node->text.at(0).unicode()) {
    case '(': case '[': case '{': case 0x27E8: case 0x230A: case 0x2308:
    case ')': case ']': case '}': case 0x27E9: case 0x230B: case 0x2309:
        *name = starDelimiter(node->text, left);
        break;
    default:
        return false;
    }
    static const QHash<QString, QString> pairs = {
        {")", "("}, {"]", "["}, {"rbrace", "lbrace"}, {"rangle", "langle"}, {"rfloor", "lfloor"}, {"rceil", "lceil"},
    };
    *closes = pairs.value(*name);
    return true;
}

QString starRow(const QList<NodePtr> &kids)
{
    // Brackets must pair in StarMath: those that do not are written as text.
    QList<int> stack;
    QHash<int, bool> paired;
    QList<int> bars;
    for (int i = 0; i < kids.size(); ++i) {
        QString name;
        QString closes;
        if (starBracket(kids.at(i), &name, &closes)) {
            if (closes.isEmpty()) {
                stack.append(i);
            } else if (!stack.isEmpty()) {
                QString openName;
                QString unused;
                starBracket(kids.at(stack.last()), &openName, &unused);
                if (openName == closes) {
                    paired.insert(stack.takeLast(), true);
                    paired.insert(i, true);
                }
            }
        } else if (kids.at(i) && kids.at(i)->type == Node::Symbol && kids.at(i)->text == QLatin1String("|") && !kids.at(i)->middle) {
            bars.append(i);
        }
    }
    QStringList parts;
    QChar previous = QLatin1Char(' ');
    bool any = false;
    for (int i = 0; i < kids.size(); ++i) {
        const NodePtr &kid = kids.at(i);
        const QChar kind = starKind(kid);
        if (!kid) {
            continue;
        }
        QString name;
        QString closes;
        QString text;
        if (starBracket(kid, &name, &closes)) {
            text = paired.value(i) ? name : quoted(kid->text);
        } else if (bars.contains(i)) {
            text = bars.size() % 2 == 0 ? (bars.indexOf(i) % 2 == 0 ? QStringLiteral("lline") : QStringLiteral("rline")) : quoted(kid->text);
        } else {
            text = star(kid);
        }
        if (kind == QLatin1Char(' ')) {
            if (!text.isEmpty()) {
                parts.append(text);
            }
            continue;
        }
        const bool opens = closes.isEmpty() && !name.isEmpty();
        const bool sign = kind == QLatin1Char('b') && kid->type == Node::Symbol && (kid->text == QLatin1String("+") || kid->text == QString(QChar(0x2212)));
        // An operator with nothing on a side gets an empty group there.
        if ((kind == QLatin1Char('b') && !sign && (!any || previous != QLatin1Char('o')))
            || (kind == QLatin1Char('b') && sign && any && previous == QLatin1Char('b'))) {
            parts.append(QStringLiteral("{}"));
        }
        if ((previous == QLatin1Char('b') || previous == QLatin1Char('p')) && any && (kind == QLatin1Char(',') || (!name.isEmpty() && !opens))) {
            parts.append(QStringLiteral("{}"));
        }
        parts.append(text);
        previous = !name.isEmpty() ? (opens ? QLatin1Char(' ') : QLatin1Char('o')) : kind;
        any = previous != QLatin1Char(' ');
        if (opens) {
            any = false;
        }
    }
    if (any && (previous == QLatin1Char('b') || previous == QLatin1Char('p'))) {
        parts.append(QStringLiteral("{}"));
    }
    return parts.join(QLatin1Char(' '));
}

QString starMatrix(const NodePtr &node)
{
    const bool stack = node->text == QLatin1String("substack");
    QStringList lines;
    for (const QList<NodePtr> &line : node->cells) {
        QStringList cells;
        for (int i = 0; i < line.size(); ++i) {
            const QChar align = node->columns.isEmpty() ? QLatin1Char('c') : node->columns.at(i % node->columns.size());
            const QString body = QStringLiteral("{") + (line.at(i) ? starRow(line.at(i)->kids) : QString()) + QStringLiteral("}");
            cells.append((align == QLatin1Char('l') ? QStringLiteral("alignl ") : align == QLatin1Char('r') ? QStringLiteral("alignr ") : QString()) + body);
        }
        lines.append(cells.join(stack ? QStringLiteral(" ") : QStringLiteral(" # ")));
    }
    QString out = (stack ? QStringLiteral("stack{") : QStringLiteral("matrix{")) + lines.join(stack ? QStringLiteral(" # ") : QStringLiteral(" ## ")) + QStringLiteral("}");
    if (!node->left.isEmpty() || !node->right.isEmpty()) {
        out = QStringLiteral("left ") + starDelimiter(node->left, true) + QLatin1Char(' ') + out + QStringLiteral(" right ") + starDelimiter(node->right, false);
    }
    return out;
}

QString star(const NodePtr &node)
{
    if (!node) {
        return QString();
    }
    switch (node->type) {
    case Node::Row:
        if (node->hole) {
            return QStringLiteral("{}");
        }
        return node->flows ? starRow(node->kids) : QStringLiteral("{") + starRow(node->kids) + QStringLiteral("}");
    case Node::Symbol:
        if (node->sized > 0) {
            return quoted(node->text);
        }
        return starSymbol(node);
    case Node::Text:
        return quoted(node->text);
    case Node::Frac: {
        if (node->rule) {
            return QStringLiteral("{") + starGroup(node->kids.value(0)) + QStringLiteral(" over ") + starGroup(node->kids.value(1)) + QStringLiteral("}");
        }
        QString out = QStringLiteral("binom") + starGroup(node->kids.value(0)) + starGroup(node->kids.value(1));
        if (!node->left.isEmpty() || !node->right.isEmpty()) {
            out = QStringLiteral("left ") + starDelimiter(node->left, true) + QLatin1Char(' ') + out + QStringLiteral(" right ") + starDelimiter(node->right, false);
        }
        return out;
    }
    case Node::Sqrt:
        if (node->kids.value(1)) {
            return QStringLiteral("nroot") + starGroup(node->kids.value(1)) + starGroup(node->kids.value(0));
        }
        return QStringLiteral("sqrt") + starGroup(node->kids.value(0));
    case Node::Script: {
        const NodePtr base = node->kids.value(0);
        const NodePtr sub = node->kids.value(1);
        const NodePtr sup = node->kids.value(2);
        if (isBigOperator(base)) {
            return starSymbol(base) + (sub ? QStringLiteral(" from") + starGroup(sub) : QString()) + (sup ? QStringLiteral(" to") + starGroup(sup) : QString());
        }
        if (base && base->type == Node::Accent && (base->text == QLatin1String("overbrace") || base->text == QLatin1String("underbrace"))) {
            const bool over = base->text == QLatin1String("overbrace");
            return QStringLiteral("{") + starGroup(base->kids.value(0)) + (over ? QStringLiteral(" overbrace ") : QStringLiteral(" underbrace "))
                + starGroup(over ? sup : sub) + QStringLiteral("}");
        }
        // A relation under a script is no longer one for StarMath.
        const QString start = base && base->type == Node::Symbol && (base->atom == Atom::Bin || base->atom == Atom::Rel) ? quoted(base->text)
            : base ? (base->type == Node::Symbol ? starSymbol(base) : starGroup(base)) : QStringLiteral("{}");
        if (node->stacked) {
            return QStringLiteral("{") + start + (sub ? QStringLiteral(" csub") + starGroup(sub) : QString()) + (sup ? QStringLiteral(" csup") + starGroup(sup) : QString())
                + QStringLiteral("}");
        }
        return start + (sub ? QStringLiteral("_") + starGroup(sub) : QString()) + (sup ? QStringLiteral("^") + starGroup(sup) : QString());
    }
    case Node::Delim: {
        const NodePtr body = node->kids.value(0);
        return QStringLiteral("left ") + starDelimiter(node->left, true) + QLatin1Char(' ') + (body ? starRow(body->kids) : QString()) + QStringLiteral(" right ")
            + starDelimiter(node->right, false);
    }
    case Node::Matrix:
        return starMatrix(node);
    case Node::Accent: {
        const AccentInfo *info = accent(node->text);
        const QString name = info ? QString::fromLatin1(info->star) : QStringLiteral("hat");
        if (name == QLatin1String("overbrace") || name == QLatin1String("underbrace")) {
            return QStringLiteral("{") + starGroup(node->kids.value(0)) + QLatin1Char(' ') + name + QStringLiteral(" {}}");
        }
        return name + QLatin1Char(' ') + starGroup(node->kids.value(0));
    }
    case Node::Style: {
        const NodePtr body = node->kids.value(0);
        const QString inside = starGroup(body);
        if (node->text == QLatin1String("font")) {
            static const QHash<QString, QString> fonts = {
                {"rm", "nitalic "}, {"bf", "bold "}, {"it", "ital "}, {"bs", "bold ital "}, {"sf", "font sans "}, {"tt", "font fixed "},
            };
            if (node->arg == QLatin1String("bb") && body && body->kids.size() == 1 && body->kids.first() && body->kids.first()->type == Node::Symbol
                && QStringLiteral("CNQRZ").contains(body->kids.first()->text)) {
                return QStringLiteral("set") + body->kids.first()->text;
            }
            return fonts.value(node->arg) + inside;
        }
        if (node->text == QLatin1String("color")) {
            const QColor color(Latex::colorName(node->arg));
            return QStringLiteral("color rgb %1 %2 %3 ").arg(color.red()).arg(color.green()).arg(color.blue()) + inside;
        }
        if (node->text == QLatin1String("phantom")) {
            return QStringLiteral("phantom ") + inside;
        }
        return inside;
    }
    case Node::Space:
        return node->width >= 30 ? QStringLiteral("~~~") : node->width >= 15 ? QStringLiteral("~~") : node->width >= 6 ? QStringLiteral("~")
            : node->width > 0 ? QStringLiteral("`") : QString();
    case Node::Error:
        return quoted(node->text);
    }
    return QString();
}

// ---- MathML, read ----------------------------------------------------------

/// Joins pieces of LaTeX, keeping a command apart from a letter that follows it.
void append(QString &out, const QString &piece)
{
    if (piece.isEmpty()) {
        return;
    }
    static const QRegularExpression command(QStringLiteral("\\\\[A-Za-z]+$"));
    if (!out.isEmpty() && piece.at(0).isLetter() && command.match(out).hasMatch()) {
        out += QLatin1Char(' ');
    }
    out += piece;
}

QString texChar(char32_t code)
{
    switch (code) {
    case '{': return QStringLiteral("\\{");
    case '}': return QStringLiteral("\\}");
    case '%': return QStringLiteral("\\%");
    case '#': return QStringLiteral("\\#");
    case '&': return QStringLiteral("\\&");
    case '_': return QStringLiteral("\\_");
    case '$': return QStringLiteral("\\$");
    case '\\': return QStringLiteral("\\backslash");
    case '~': return QStringLiteral("\\sim");
    case '^': return QStringLiteral("\\wedge");
    case 0x2212: return QStringLiteral("-");
    case 0x2217: return QStringLiteral("*");
    case 0x2032: return QStringLiteral("'");
    case 0x2223: return QStringLiteral("\\mid");
    case 0x2061: case 0x2062: case 0x2063: case 0x2064: case 0x200B: return QString();
    default: break;
    }
    if (code < 128) {
        return QString(QChar(ushort(code)));
    }
    if (const SymbolInfo *info = Symbols::byCode(code)) {
        return QLatin1Char('\\') + QString::fromLatin1(info->command);
    }
    return QString::fromUcs4(&code, 1);
}

QString texChars(const QString &text)
{
    QString out;
    for (const uint code : text.toUcs4()) {
        append(out, texChar(code));
    }
    return out;
}

QString texDelimiter(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty()) {
        return QStringLiteral(".");
    }
    switch (t.at(0).unicode()) {
    case '{': return QStringLiteral("\\{");
    case '}': return QStringLiteral("\\}");
    case 0x2016: case 0x2225: return QStringLiteral("\\|");
    case 0x2223: return QStringLiteral("|");
    case 0x27E8: case 0x2329: case 0x3008: return QStringLiteral("\\langle");
    case 0x27E9: case 0x232A: case 0x3009: return QStringLiteral("\\rangle");
    default: break;
    }
    return texChars(t);
}

bool isFence(const QDomElement &e)
{
    if (e.tagName() != QLatin1String("mo")) {
        return false;
    }
    return e.attribute(QStringLiteral("stretchy")) == QLatin1String("true") && e.attribute(QStringLiteral("fence")) == QLatin1String("true")
        && e.attribute(QStringLiteral("form")) != QLatin1String("infix");
}

QList<QDomElement> children(const QDomElement &e)
{
    QList<QDomElement> out;
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (!c.tagName().startsWith(QLatin1String("annotation"))) {
            out.append(c);
        }
    }
    return out;
}

QString tex(const QDomElement &e);

QString texAll(const QList<QDomElement> &list)
{
    QString out;
    for (const QDomElement &c : list) {
        append(out, tex(c));
    }
    return out;
}

QString braced(const QString &inside)
{
    return QLatin1Char('{') + inside + QLatin1Char('}');
}

bool isLeaf(const QDomElement &e)
{
    const QString tag = e.tagName();
    return tag == QLatin1String("mi") || tag == QLatin1String("mn") || tag == QLatin1String("mo");
}

/// A script's base: braced unless it is one token.
QString texBase(const QDomElement &e)
{
    const QString inside = tex(e);
    if (isLeaf(e) && (e.text().trimmed().toUcs4().size() == 1 || e.tagName() == QLatin1String("mn") || inside.startsWith(QLatin1Char('\\')))) {
        return inside;
    }
    if (e.tagName() == QLatin1String("mrow") && children(e).size() == 1) {
        return texBase(children(e).first());
    }
    return braced(inside);
}

QString texTable(const QDomElement &table, const QString &environment)
{
    QStringList lines;
    for (const QDomElement &row : children(table)) {
        QStringList cells;
        if (row.tagName() == QLatin1String("mtr") || row.tagName() == QLatin1String("mlabeledtr")) {
            for (const QDomElement &cell : children(row)) {
                cells.append(texAll(children(cell)));
            }
        } else {
            cells.append(tex(row));
        }
        lines.append(cells.join(QStringLiteral(" & ")));
    }
    return QStringLiteral("\\begin{%1} ").arg(environment) + lines.join(QStringLiteral(" \\\\ ")) + QStringLiteral(" \\end{%1}").arg(environment);
}

QString texVariant(const QString &variant, const QString &inside)
{
    static const QHash<QString, QString> map = {
        {"bold", "\\mathbf"}, {"italic", "\\mathit"}, {"bold-italic", "\\boldsymbol"}, {"double-struck", "\\mathbb"}, {"script", "\\mathcal"},
        {"fraktur", "\\mathfrak"}, {"sans-serif", "\\mathsf"}, {"monospace", "\\mathtt"}, {"normal", "\\mathrm"},
    };
    const QString command = map.value(variant);
    return command.isEmpty() ? inside : command + braced(inside);
}

QString texOver(const QDomElement &baseElement, const QDomElement &markElement, bool under)
{
    const QString mark = markElement.text().trimmed();
    const QString base = tex(baseElement);
    if (isLeaf(markElement) && mark.toUcs4().size() == 1) {
        const char32_t code = mark.toUcs4().first();
        const bool wide = baseElement.text().trimmed().toUcs4().size() > 1;
        QString command;
        switch (code) {
        case 0x5E: case 0x2C6: case 0x302: command = wide ? QStringLiteral("\\widehat") : QStringLiteral("\\hat"); break;
        case 0x7E: case 0x2DC: case 0x303: command = wide ? QStringLiteral("\\widetilde") : QStringLiteral("\\tilde"); break;
        case 0xAF: case 0x203E: case 0x305: case 0x2015: case 0x2500: case 0x304: case 0x5F: case 0x332:
            command = under ? QStringLiteral("\\underline") : (wide || code != 0xAF ? QStringLiteral("\\overline") : QStringLiteral("\\bar"));
            break;
        case 0x2192: case 0x20D7: command = wide ? QStringLiteral("\\overrightarrow") : QStringLiteral("\\vec"); break;
        case 0x2190: command = QStringLiteral("\\overleftarrow"); break;
        case 0x2D9: case 0x307: case 0x2E: command = QStringLiteral("\\dot"); break;
        case 0xA8: case 0x308: command = QStringLiteral("\\ddot"); break;
        case 0x2C7: case 0x30C: command = QStringLiteral("\\check"); break;
        case 0x2D8: case 0x306: command = QStringLiteral("\\breve"); break;
        case 0xB4: case 0x301: command = QStringLiteral("\\acute"); break;
        case 0x60: case 0x300: command = QStringLiteral("\\grave"); break;
        case 0x2DA: case 0x30A: command = QStringLiteral("\\mathring"); break;
        case 0x23DE: command = QStringLiteral("\\overbrace"); break;
        case 0x23DF: command = QStringLiteral("\\underbrace"); break;
        default: break;
        }
        if (!command.isEmpty() && (under == (command.startsWith(QLatin1String("\\under"))))) {
            return command + braced(base);
        }
    }
    return QString();
}

/// `\sum`, `\lim`… whose scripts LaTeX already sets over and under; `\int` needs `\limits`.
QString texLimits(const QDomElement &baseElement)
{
    if (!isLeaf(baseElement)) {
        return QString();
    }
    const QString base = tex(baseElement);
    if (!base.startsWith(QLatin1Char('\\'))) {
        return QString();
    }
    const QString name = base.mid(1);
    if (const FunctionInfo *function = Symbols::function(name)) {
        return function->limits ? base : base + QStringLiteral("\\limits");
    }
    if (const SymbolInfo *info = Symbols::find(name)) {
        if (QLatin1String(info->category) == QLatin1String("big")) {
            return info->limits ? base : base + QStringLiteral("\\limits");
        }
    }
    return QString();
}

QString tex(const QDomElement &e)
{
    const QString tag = e.tagName().section(QLatin1Char(':'), -1);
    const QList<QDomElement> kids = children(e);
    const QString text = e.text().trimmed();
    if (tag == QLatin1String("mi")) {
        const QString variant = e.attribute(QStringLiteral("mathvariant"));
        const QList<uint> codes = text.toUcs4();
        if (codes.size() == 1) {
            const QString one = texChar(codes.first());
            if (variant.isEmpty() || variant == QLatin1String("italic") || (variant == QLatin1String("normal") && codes.first() > 127)) {
                return one;
            }
            return texVariant(variant, one);
        }
        if (Symbols::function(text)) {
            return QLatin1Char('\\') + text;
        }
        if (isLetters(text) && (variant.isEmpty() || variant == QLatin1String("normal"))) {
            return QStringLiteral("\\operatorname{") + text + QLatin1Char('}');
        }
        return texVariant(variant, texChars(text));
    }
    if (tag == QLatin1String("mn")) {
        return texChars(text);
    }
    if (tag == QLatin1String("mo")) {
        if (text.size() > 1 && isLetters(text) && text.at(0).unicode() < 128) {
            return Symbols::function(text) ? QLatin1Char('\\') + text : QStringLiteral("\\operatorname{") + text + QLatin1Char('}');
        }
        return texChars(text);
    }
    if (tag == QLatin1String("mtext") || tag == QLatin1String("ms")) {
        QString safe = e.text();
        safe.replace(QLatin1Char('\\'), QString()).replace(QLatin1Char('{'), QStringLiteral("(")).replace(QLatin1Char('}'), QStringLiteral(")"));
        return safe.trimmed().isEmpty() ? QStringLiteral("\\ ") : QStringLiteral("\\text{") + safe + QLatin1Char('}');
    }
    if (tag == QLatin1String("mspace")) {
        QString width = e.attribute(QStringLiteral("width"));
        const bool em = width.endsWith(QLatin1String("em"));
        width.chop(2);
        const qreal ems = em ? width.toDouble() : 0;
        return ems >= 1.8 ? QStringLiteral("\\qquad") : ems >= 0.9 ? QStringLiteral("\\quad") : ems >= 0.3 ? QStringLiteral("\\;")
            : ems > 0 ? QStringLiteral("\\,") : QString();
    }
    if (tag == QLatin1String("mfrac")) {
        if (kids.size() < 2) {
            return texAll(kids);
        }
        const QString thickness = e.attribute(QStringLiteral("linethickness"));
        if (thickness == QLatin1String("0") || thickness.startsWith(QLatin1String("0p")) || thickness.startsWith(QLatin1String("0e"))) {
            return QStringLiteral("\\substack{") + tex(kids.at(0)) + QStringLiteral(" \\\\ ") + tex(kids.at(1)) + QLatin1Char('}');
        }
        if (e.attribute(QStringLiteral("bevelled")) == QLatin1String("true")) {
            return braced(tex(kids.at(0))) + QLatin1Char('/') + braced(tex(kids.at(1)));
        }
        return QStringLiteral("\\frac") + braced(tex(kids.at(0))) + braced(tex(kids.at(1)));
    }
    if (tag == QLatin1String("msqrt")) {
        return QStringLiteral("\\sqrt") + braced(texAll(kids));
    }
    if (tag == QLatin1String("mroot") && kids.size() >= 2) {
        return QStringLiteral("\\sqrt[") + tex(kids.at(1)) + QLatin1Char(']') + braced(tex(kids.at(0)));
    }
    if ((tag == QLatin1String("msub") || tag == QLatin1String("msup")) && kids.size() >= 2) {
        return texBase(kids.at(0)) + (tag == QLatin1String("msub") ? QLatin1Char('_') : QLatin1Char('^')) + braced(tex(kids.at(1)));
    }
    if ((tag == QLatin1String("msubsup") || tag == QLatin1String("mmultiscripts")) && kids.size() >= 3) {
        return texBase(kids.at(0)) + QLatin1Char('_') + braced(tex(kids.at(1))) + QLatin1Char('^') + braced(tex(kids.at(2)));
    }
    if ((tag == QLatin1String("munder") || tag == QLatin1String("mover")) && kids.size() >= 2) {
        const bool under = tag == QLatin1String("munder");
        const QString limits = texLimits(kids.at(0));
        if (!limits.isEmpty()) {
            return limits + (under ? QLatin1Char('_') : QLatin1Char('^')) + braced(tex(kids.at(1)));
        }
        const QString accented = texOver(kids.at(0), kids.at(1), under);
        if (!accented.isEmpty()) {
            return accented;
        }
        const QString base = tex(kids.at(0));
        // A label on a brace is its script in LaTeX.
        if (base.startsWith(under ? QLatin1String("\\underbrace{") : QLatin1String("\\overbrace{"))) {
            return base + (under ? QLatin1Char('_') : QLatin1Char('^')) + braced(tex(kids.at(1)));
        }
        return (under ? QStringLiteral("\\underset") : QStringLiteral("\\overset")) + braced(tex(kids.at(1))) + braced(base);
    }
    if (tag == QLatin1String("munderover") && kids.size() >= 3) {
        const QString limits = texLimits(kids.at(0));
        if (!limits.isEmpty()) {
            return limits + QLatin1Char('_') + braced(tex(kids.at(1))) + QLatin1Char('^') + braced(tex(kids.at(2)));
        }
        return QStringLiteral("\\underset") + braced(tex(kids.at(1))) + braced(QStringLiteral("\\overset") + braced(tex(kids.at(2))) + braced(tex(kids.at(0))));
    }
    if (tag == QLatin1String("mfenced")) {
        const QString open = e.hasAttribute(QStringLiteral("open")) ? e.attribute(QStringLiteral("open")) : QStringLiteral("(");
        const QString close = e.hasAttribute(QStringLiteral("close")) ? e.attribute(QStringLiteral("close")) : QStringLiteral(")");
        const QString separator = e.hasAttribute(QStringLiteral("separators")) ? e.attribute(QStringLiteral("separators")).trimmed().left(1) : QStringLiteral(",");
        QString inside;
        for (int i = 0; i < kids.size(); ++i) {
            if (i > 0) {
                append(inside, separator);
            }
            append(inside, tex(kids.at(i)));
        }
        QString out = QStringLiteral("\\left") + texDelimiter(open);
        append(out, inside);
        append(out, QStringLiteral("\\right") + texDelimiter(close));
        return out;
    }
    if (tag == QLatin1String("mtable")) {
        return texTable(e, QStringLiteral("matrix"));
    }
    if (tag == QLatin1String("mstyle") || tag == QLatin1String("mrow") || tag == QLatin1String("math") || tag == QLatin1String("mpadded")
        || tag == QLatin1String("merror") || tag == QLatin1String("semantics") || tag == QLatin1String("maction") || tag == QLatin1String("mtd")
        || tag == QLatin1String("mtr")) {
        QString inside;
        if (kids.size() >= 2 && isFence(kids.first()) && isFence(kids.last())) {
            const QString open = kids.first().text().trimmed();
            const QString close = kids.last().text().trimmed();
            QList<QDomElement> middle = kids.mid(1, kids.size() - 2);
            while (middle.size() == 1 && middle.first().tagName() == QLatin1String("mrow") && children(middle.first()).size() == 1) {
                middle = children(middle.first());
            }
            if (middle.size() == 1 && middle.first().tagName() == QLatin1String("mrow") && children(middle.first()).size() >= 1
                && children(middle.first()).first().tagName() == QLatin1String("mtable") && children(middle.first()).size() == 1) {
                middle = children(middle.first());
            }
            static const QHash<QString, QString> environments = {
                {"()", "pmatrix"}, {"[]", "bmatrix"}, {"{}", "Bmatrix"}, {"||", "vmatrix"}, {QStringLiteral("‖‖"), "Vmatrix"},
                {QStringLiteral("∣∣"), "vmatrix"}, {QStringLiteral("∥∥"), "Vmatrix"}, {"{", "cases"},
            };
            const QString environment = environments.value(open + close);
            if (middle.size() == 1 && middle.first().tagName() == QLatin1String("mtable") && !environment.isEmpty()) {
                inside = texTable(middle.first(), environment);
            } else if (middle.size() == 1 && middle.first().tagName() == QLatin1String("mfrac") && open == QLatin1String("(") && close == QLatin1String(")")
                && middle.first().attribute(QStringLiteral("linethickness")).startsWith(QLatin1Char('0')) && children(middle.first()).size() == 2) {
                inside = QStringLiteral("\\binom") + braced(tex(children(middle.first()).at(0))) + braced(tex(children(middle.first()).at(1)));
            } else {
                inside = QStringLiteral("\\left") + texDelimiter(open);
                append(inside, texAll(middle));
                append(inside, QStringLiteral("\\right") + texDelimiter(close));
            }
        } else {
            inside = texAll(kids);
        }
        if (tag == QLatin1String("mstyle")) {
            const QColor color(e.attribute(QStringLiteral("mathcolor"), e.attribute(QStringLiteral("color"))));
            if (color.isValid() && !inside.isEmpty()) {
                inside = QStringLiteral("\\textcolor{") + color.name() + QStringLiteral("}{") + inside + QLatin1Char('}');
            }
            if (e.hasAttribute(QStringLiteral("mathvariant")) && !inside.isEmpty()) {
                inside = texVariant(e.attribute(QStringLiteral("mathvariant")), inside);
            }
        }
        return inside;
    }
    if (tag == QLatin1String("mphantom")) {
        return QStringLiteral("\\phantom") + braced(texAll(kids));
    }
    if (tag == QLatin1String("menclose")) {
        return e.attribute(QStringLiteral("notation"), QStringLiteral("box")).contains(QLatin1String("box")) ? QStringLiteral("\\boxed") + braced(texAll(kids))
            : texAll(kids);
    }
    if (tag == QLatin1String("none") || tag == QLatin1String("mprescripts")) {
        return QString();
    }
    return texAll(kids);
}

/// Down through the wrappers that hold one thing.
QDomElement unwrapped(QDomElement e)
{
    while (true) {
        const QList<QDomElement> kids = children(e);
        const QString tag = e.tagName();
        if (kids.size() == 1 && (tag == QLatin1String("math") || tag == QLatin1String("semantics") || tag == QLatin1String("mrow") || tag == QLatin1String("mstyle"))
            && !e.hasAttribute(QStringLiteral("mathcolor")) && !e.hasAttribute(QStringLiteral("mathvariant"))) {
            e = kids.first();
        } else {
            return e;
        }
    }
}

void stripPrefixes(QDomElement e)
{
    // `math:mrow` and `mrow` are the same thing here.
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        stripPrefixes(c);
    }
    if (e.tagName().contains(QLatin1Char(':'))) {
        e.setTagName(e.tagName().section(QLatin1Char(':'), -1));
    }
}

QString starColor(const FormulaDoc &doc, const QString &inside)
{
    if (doc.color.isEmpty() || !QColor(doc.color).isValid()) {
        return inside;
    }
    const QColor color(doc.color);
    return QStringLiteral("color rgb %1 %2 %3 {").arg(color.red()).arg(color.green()).arg(color.blue()) + inside + QStringLiteral("}");
}

}

namespace Formats {

QString toStarMath(const QString &latex)
{
    const Parsed parsed = Latex::parse(latex);
    if (!parsed.root) {
        return QString();
    }
    if (parsed.root->type == Node::Row) {
        return starRow(parsed.root->kids);
    }
    return star(parsed.root);
}

QString toMathML(const QString &latex, bool annotate)
{
    const Parsed parsed = Latex::parse(latex);
    const QString body = parsed.root ? (parsed.root->type == Node::Row ? mlRow(parsed.root->kids) : ml(parsed.root)) : QString();
    QString out = QStringLiteral("<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\">");
    if (annotate) {
        out += QStringLiteral("<semantics><mrow>") + body + QStringLiteral("</mrow><annotation encoding=\"application/x-tex\">") + esc(latex.trimmed())
            + QStringLiteral("</annotation></semantics>");
    } else {
        out += body;
    }
    return out + QStringLiteral("</math>\n");
}

QStringList fromMathML(const QString &xml, QString *error)
{
    QDomDocument dom;
    const QDomDocument::ParseResult result = dom.setContent(xml);
    QDomElement root = dom.documentElement();
    if (!result || root.isNull()) {
        if (error) {
            *error = QStringLiteral("El MathML no se puede leer: %1 (línea %2).").arg(result.errorMessage).arg(result.errorLine);
        }
        return {};
    }
    stripPrefixes(root);
    if (root.tagName() != QLatin1String("math")) {
        const QDomNodeList found = root.elementsByTagName(QStringLiteral("math"));
        if (found.isEmpty()) {
            if (error) {
                *error = QStringLiteral("No hay ninguna fórmula MathML en el archivo.");
            }
            return {};
        }
        root = found.at(0).toElement();
    }
    const QDomElement top = unwrapped(root);
    QStringList out;
    // A column of rows with one cell each is how a list of equations is written.
    bool column = top.tagName() == QLatin1String("mtable") && children(top).size() > 1;
    if (column) {
        for (const QDomElement &row : children(top)) {
            column = column && children(row).size() == 1;
        }
    }
    if (column) {
        for (const QDomElement &row : children(top)) {
            out.append(tex(row).trimmed());
        }
    } else {
        out.append(tex(root).trimmed());
    }
    return out;
}

QByteArray toOdf(const FormulaDoc &doc)
{
    QStringList rows;
    QStringList stars;
    for (const QString &latex : doc.equations) {
        const Parsed parsed = Latex::parse(latex);
        const QString body = parsed.root ? (parsed.root->type == Node::Row ? mlRow(parsed.root->kids) : ml(parsed.root)) : QString();
        rows.append(body);
        stars.append(starColor(doc, toStarMath(latex)));
    }
    QString body;
    if (rows.size() == 1) {
        body = QStringLiteral("<mrow>") + rows.first() + QStringLiteral("</mrow>");
    } else {
        body = QStringLiteral("<mtable>");
        for (const QString &row : rows) {
            body += QStringLiteral("<mtr><mtd><mrow>") + row + QStringLiteral("</mrow></mtd></mtr>");
        }
        body += QStringLiteral("</mtable>");
    }
    if (!doc.color.isEmpty() && QColor(doc.color).isValid()) {
        body = QStringLiteral("<mstyle mathcolor=\"%1\">").arg(QColor(doc.color).name()) + body + QStringLiteral("</mstyle>");
    }
    const QString content = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><semantics>")
        + body + QStringLiteral("<annotation encoding=\"StarMath 5.0\">") + esc(stars.join(QStringLiteral(" newline "))) + QStringLiteral("</annotation></semantics></math>\n");
    const QString settings = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-settings xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:config=\"urn:oasis:names:tc:opendocument:xmlns:config:1.0\" office:version=\"1.3\"><office:settings>"
        "<config:config-item-set config:name=\"ooo:configuration-settings\">"
        "<config:config-item config:name=\"BaseFontHeight\" config:type=\"short\">%1</config:config-item>"
        "</config:config-item-set></office:settings></office:document-settings>\n").arg(qRound(doc.size));
    const QString meta = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" office:version=\"1.3\"><office:meta>"
        "<meta:generator>Wonderland-Formulas</meta:generator><dc:title>%1</dc:title></office:meta></office:document-meta>\n").arg(esc(doc.title));
    const QString manifest = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">"
        "<manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"application/vnd.oasis.opendocument.formula\"/>"
        "<manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"settings.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>"
        "<manifest:file-entry manifest:full-path=\"Wonderland/formula.json\" manifest:media-type=\"application/x-wonderland-formula\"/>"
        "</manifest:manifest>\n");
    ZipWriter zip;
    zip.add(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.formula", true);
    zip.add(QStringLiteral("content.xml"), content.toUtf8());
    zip.add(QStringLiteral("settings.xml"), settings.toUtf8());
    zip.add(QStringLiteral("meta.xml"), meta.toUtf8());
    // The document as this app keeps it: LibreOffice drops it when it saves, and then the MathML is what is read.
    zip.add(QStringLiteral("Wonderland/formula.json"), toWform(doc));
    zip.add(QStringLiteral("META-INF/manifest.xml"), manifest.toUtf8());
    return zip.finish();
}

bool fromOdf(const QByteArray &bytes, FormulaDoc *doc, QString *error)
{
    const ZipReader zip(bytes);
    if (!zip.isValid() || !zip.contains(QStringLiteral("content.xml"))) {
        if (error) {
            *error = QStringLiteral("No es una fórmula OpenDocument.");
        }
        return false;
    }
    if (zip.contains(QStringLiteral("Wonderland/formula.json")) && fromWform(zip.file(QStringLiteral("Wonderland/formula.json")), doc, nullptr)) {
        return true;
    }
    QString problem;
    FormulaDoc out;
    out.equations = fromMathML(QString::fromUtf8(zip.file(QStringLiteral("content.xml"))), &problem);
    if (!problem.isEmpty()) {
        if (error) {
            *error = problem;
        }
        return false;
    }
    if (zip.contains(QStringLiteral("settings.xml"))) {
        static const QRegularExpression height(QStringLiteral("config:name=\"BaseFontHeight\"[^>]*>(\\d+)<"));
        const QRegularExpressionMatch match = height.match(QString::fromUtf8(zip.file(QStringLiteral("settings.xml"))));
        if (match.hasMatch()) {
            out.size = qBound(6.0, match.captured(1).toDouble(), 144.0);
        }
    }
    if (zip.contains(QStringLiteral("meta.xml"))) {
        static const QRegularExpression title(QStringLiteral("<dc:title>([^<]*)</dc:title>"));
        const QRegularExpressionMatch match = title.match(QString::fromUtf8(zip.file(QStringLiteral("meta.xml"))));
        if (match.hasMatch()) {
            QDomDocument text;
            out.title = match.captured(1).replace(QStringLiteral("&amp;"), QStringLiteral("&")).replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                .replace(QStringLiteral("&gt;"), QStringLiteral(">")).replace(QStringLiteral("&quot;"), QStringLiteral("\""));
        }
    }
    *doc = out;
    return true;
}

}
