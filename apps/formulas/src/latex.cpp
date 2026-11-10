#include "latex.h"

#include <QColor>
#include <QHash>

namespace {

NodePtr make(Node::Type type, int start, int end = -1)
{
    NodePtr node(new Node);
    node->type = type;
    node->start = start;
    node->end = end < 0 ? start : end;
    return node;
}

NodePtr rowOf(const QList<NodePtr> &kids, int start, int end)
{
    NodePtr row = make(Node::Row, start, end);
    row->kids = kids;
    return row;
}

struct Environment {
    const char *name;
    const char *left;
    const char *right;
    const char *columns;
};

const Environment *environment(const QString &name)
{
    static const Environment list[] = {
        {"matrix", "", "", "c"},
        {"pmatrix", "(", ")", "c"},
        {"bmatrix", "[", "]", "c"},
        {"Bmatrix", "{", "}", "c"},
        {"vmatrix", "|", "|", "c"},
        {"Vmatrix", "\u2016", "\u2016", "c"},
        {"smallmatrix", "", "", "c"},
        {"cases", "{", "", "ll"},
        {"aligned", "", "", "rl"},
        {"align", "", "", "rl"},
        {"align*", "", "", "rl"},
        {"alignat", "", "", "rl"},
        {"split", "", "", "rl"},
        {"eqnarray", "", "", "rcl"},
        {"eqnarray*", "", "", "rcl"},
        {"gathered", "", "", "c"},
        {"gather", "", "", "c"},
        {"gather*", "", "", "c"},
        {"equation", "", "", "c"},
        {"equation*", "", "", "c"},
        {"multline", "", "", "c"},
        {"multline*", "", "", "c"},
        {"array", "", "", "c"},
        {"substack", "", "", "c"},
    };
    for (const Environment &env : list) {
        if (name == QLatin1String(env.name)) {
            return &env;
        }
    }
    return nullptr;
}

const QHash<QString, QString> &fonts()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("mathrm"), QStringLiteral("rm")}, {QStringLiteral("mathbf"), QStringLiteral("bf")},
        {QStringLiteral("mathit"), QStringLiteral("it")}, {QStringLiteral("mathbb"), QStringLiteral("bb")},
        {QStringLiteral("mathcal"), QStringLiteral("cal")}, {QStringLiteral("mathscr"), QStringLiteral("cal")},
        {QStringLiteral("mathfrak"), QStringLiteral("frak")}, {QStringLiteral("mathsf"), QStringLiteral("sf")},
        {QStringLiteral("mathtt"), QStringLiteral("tt")}, {QStringLiteral("boldsymbol"), QStringLiteral("bs")},
        {QStringLiteral("bm"), QStringLiteral("bs")}, {QStringLiteral("mathnormal"), QStringLiteral("")},
    };
    return map;
}

const QHash<QString, QString> &declarations()
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("rm"), QStringLiteral("rm")}, {QStringLiteral("bf"), QStringLiteral("bf")},
        {QStringLiteral("it"), QStringLiteral("it")}, {QStringLiteral("cal"), QStringLiteral("cal")},
        {QStringLiteral("sf"), QStringLiteral("sf")}, {QStringLiteral("tt"), QStringLiteral("tt")},
    };
    return map;
}

bool isAccent(const QString &command)
{
    static const QStringList list = {
        QStringLiteral("hat"), QStringLiteral("widehat"), QStringLiteral("bar"), QStringLiteral("vec"), QStringLiteral("dot"),
        QStringLiteral("ddot"), QStringLiteral("tilde"), QStringLiteral("widetilde"), QStringLiteral("check"), QStringLiteral("breve"),
        QStringLiteral("acute"), QStringLiteral("grave"), QStringLiteral("overline"), QStringLiteral("underline"),
        QStringLiteral("overbrace"), QStringLiteral("underbrace"), QStringLiteral("overrightarrow"), QStringLiteral("overleftarrow"),
        QStringLiteral("mathring"),
    };
    return list.contains(command);
}

class Parser
{
public:
    explicit Parser(const QString &source)
        : s(source)
    {
    }

    Parsed run()
    {
        // The whole formula is read as the body of an `aligned`: a formula
        // with `\\` or `&` at the top is several lines; one without is one cell.
        NodePtr root;
        const QList<QList<NodePtr>> cells = parseCells(QString());
        if (cells.size() == 1 && cells.first().size() == 1) {
            root = cells.first().first();
        } else {
            NodePtr matrix = make(Node::Matrix, 0, s.size());
            matrix->text = QStringLiteral("aligned");
            matrix->columns = QStringLiteral("rl");
            matrix->cells = cells;
            root = rowOf({matrix}, 0, s.size());
        }
        root->start = 0;
        root->end = s.size();
        return {root, errors};
    }

private:
    const QString &s;
    int p = 0;
    /// What is open around the cursor: g a group, l a \left, e an environment, b a [ ].
    QString open;
    QList<ParseError> errors;

    void fail(int position, int length, const QString &message)
    {
        errors.append({position, qMax(1, length), message});
    }

    bool atEnd() const { return p >= s.size(); }

    void skipSpace()
    {
        while (p < s.size()) {
            if (s.at(p).isSpace()) {
                ++p;
            } else if (s.at(p) == QLatin1Char('%')) {
                while (p < s.size() && s.at(p) != QLatin1Char('\n')) {
                    ++p;
                }
            } else {
                break;
            }
        }
    }

    /// The name of the command at the cursor (which is on its backslash), without moving.
    QString peekCommand() const
    {
        if (p >= s.size() || s.at(p) != QLatin1Char('\\')) {
            return QString();
        }
        int q = p + 1;
        if (q >= s.size()) {
            return QString();
        }
        if (!s.at(q).isLetter()) {
            return s.mid(q, 1);
        }
        while (q < s.size() && s.at(q).isLetter() && s.at(q).unicode() < 128) {
            ++q;
        }
        if (q < s.size() && s.at(q) == QLatin1Char('*')) {
            const QString name = s.mid(p + 1, q - p - 1);
            if (name == QLatin1String("operatorname")) {
                ++q;
            }
        }
        return s.mid(p + 1, q - p - 1);
    }

    QString takeCommand()
    {
        const QString name = peekCommand();
        p += 1 + name.size();
        return name;
    }

    /// `{raw text}`, braces balanced; the cursor ends after it.
    QString rawArgument(const QString &owner)
    {
        skipSpace();
        if (atEnd() || s.at(p) != QLatin1Char('{')) {
            fail(qMin(p, s.size() - 1), 1, QStringLiteral("Falta el argumento de «\\%1».").arg(owner));
            return QString();
        }
        const int from = p;
        int depth = 0;
        QString out;
        while (p < s.size()) {
            const QChar c = s.at(p);
            if (c == QLatin1Char('\\') && p + 1 < s.size()) {
                const QChar next = s.at(p + 1);
                if (QStringLiteral("{}$%&#_ ").contains(next)) {
                    out += next;
                    p += 2;
                    continue;
                }
            }
            if (c == QLatin1Char('{')) {
                if (depth++ > 0) {
                    out += c;
                }
            } else if (c == QLatin1Char('}')) {
                if (--depth == 0) {
                    ++p;
                    return out;
                }
                out += c;
            } else {
                out += c;
            }
            ++p;
        }
        fail(from, 1, QStringLiteral("Falta «}»."));
        return out;
    }

    /// One required argument: a group, or the next single thing.
    NodePtr argument(const QString &owner)
    {
        skipSpace();
        const int at = p;
        if (atEnd() || isStopper()) {
            fail(qMax(0, qMin(at, s.size() - 1)), 1, QStringLiteral("Falta el argumento de «\\%1».").arg(owner));
            NodePtr hole = make(Node::Row, at, at);
            hole->hole = true;
            return hole;
        }
        NodePtr node = s.at(p) == QLatin1Char('{') ? group() : atom();
        if (!node) {
            node = make(Node::Row, at, p);
        }
        if (node->type == Node::Row && node->kids.isEmpty()) {
            node->hole = true;
        }
        return node;
    }

    /// Is the cursor on something that ends a row (whoever it belongs to)?
    bool isStopper() const
    {
        if (atEnd()) {
            return true;
        }
        const QChar c = s.at(p);
        if (c == QLatin1Char('}') || c == QLatin1Char('&')) {
            return true;
        }
        if (c == QLatin1Char(']')) {
            return open.endsWith(QLatin1Char('b'));
        }
        if (c == QLatin1Char('\\')) {
            const QString name = peekCommand();
            return name == QLatin1String("right") || name == QLatin1String("end") || name == QLatin1String("\\")
                || name == QLatin1String("cr");
        }
        return false;
    }

    NodePtr group()
    {
        const int from = p;
        ++p;
        open += QLatin1Char('g');
        const QList<NodePtr> kids = row();
        open.chop(1);
        if (!atEnd() && s.at(p) == QLatin1Char('}')) {
            ++p;
        } else {
            fail(from, 1, QStringLiteral("Falta «}» para esta llave."));
        }
        return rowOf(kids, from, p);
    }

    /// Atoms up to whatever ends this row. The stopper is left for the caller.
    QList<NodePtr> row()
    {
        QList<NodePtr> out;
        while (true) {
            skipSpace();
            if (atEnd()) {
                break;
            }
            if (isStopper()) {
                // Mine, or of something still open further out: stop either way.
                const QChar c = s.at(p);
                QChar kind;
                QString what;
                int length = 1;
                if (c == QLatin1Char('}')) {
                    kind = QLatin1Char('g');
                    what = QStringLiteral("«}» sin «{».");
                } else if (c == QLatin1Char(']')) {
                    kind = QLatin1Char('b');
                } else if (c == QLatin1Char('&')) {
                    kind = QLatin1Char('e');
                    what = QStringLiteral("«&» fuera de una matriz.");
                } else {
                    const QString name = peekCommand();
                    length = name.size() + 1;
                    if (name == QLatin1String("right")) {
                        kind = QLatin1Char('l');
                        what = QStringLiteral("«\\right» sin «\\left».");
                    } else if (name == QLatin1String("end")) {
                        kind = QLatin1Char('e');
                        what = QStringLiteral("«\\end» sin «\\begin».");
                    } else {
                        kind = QLatin1Char('e');
                        what = QStringLiteral("Salto de línea fuera de lugar.");
                    }
                }
                // A cell ends at `&` and `\\` only when the environment is the innermost thing open.
                const bool cellBreak = kind == QLatin1Char('e') && (c == QLatin1Char('&') || peekCommand() == QLatin1String("\\") || peekCommand() == QLatin1String("cr"));
                if (cellBreak ? (open.endsWith(QLatin1Char('e')) || open.endsWith(QLatin1Char('t'))) : open.contains(kind)) {
                    break;
                }
                fail(p, length, what);
                NodePtr error = make(Node::Error, p, p + length);
                error->text = s.mid(p, length);
                out.append(error);
                p += length;
                if (kind == QLatin1Char('e') && !cellBreak) {
                    rawArgumentQuiet();
                }
                continue;
            }
            const QChar c = s.at(p);
            if (c == QLatin1Char('\\')) {
                const QString name = peekCommand();
                if (declaration(name, out)) {
                    break;
                }
                if (name == QLatin1String("limits") || name == QLatin1String("nolimits")) {
                    const int at = p;
                    takeCommand();
                    if (!out.isEmpty() && (out.last()->type == Node::Symbol || out.last()->type == Node::Style)) {
                        out.last()->limits = name == QLatin1String("limits") ? 1 : 2;
                        // The scripts that follow are still the operator's.
                        skipSpace();
                        if (!atEnd() && (s.at(p) == QLatin1Char('^') || s.at(p) == QLatin1Char('_'))) {
                            out.append(scripts(out.takeLast()));
                        }
                    } else {
                        fail(at, name.size() + 1, QStringLiteral("«\\%1» debe seguir a un operador.").arg(name));
                    }
                    continue;
                }
            }
            NodePtr base;
            if (c != QLatin1Char('^') && c != QLatin1Char('_') && c != QLatin1Char('\'')) {
                base = atom();
                if (!base) {
                    continue;
                }
            }
            out.append(scripts(base));
        }
        return out;
    }

    void rawArgumentQuiet()
    {
        skipSpace();
        if (!atEnd() && s.at(p) == QLatin1Char('{')) {
            const int count = errors.size();
            rawArgument(QString());
            while (errors.size() > count) {
                errors.removeLast();
            }
        }
    }

    /// `\color{red}`, `\displaystyle`, `\bf`: they change the rest of the row.
    bool declaration(const QString &name, QList<NodePtr> &out)
    {
        QString kind;
        QString arg;
        const int from = p;
        if (name == QLatin1String("color")) {
            takeCommand();
            skipSpace();
            if (!atEnd() && s.at(p) == QLatin1Char('[')) {
                // \color[HTML]{RRGGBB}
                const int close = s.indexOf(QLatin1Char(']'), p);
                const QString model = close < 0 ? QString() : s.mid(p + 1, close - p - 1);
                p = close < 0 ? s.size() : close + 1;
                arg = rawArgument(name);
                if (model == QLatin1String("HTML")) {
                    arg.prepend(QLatin1Char('#'));
                }
            } else {
                arg = rawArgument(name);
            }
            kind = QStringLiteral("color");
            if (Latex::colorName(arg).isEmpty()) {
                fail(from, p - from, QStringLiteral("Color desconocido «%1».").arg(arg));
            }
        } else if (name == QLatin1String("displaystyle") || name == QLatin1String("textstyle") || name == QLatin1String("scriptstyle")
                   || name == QLatin1String("scriptscriptstyle")) {
            takeCommand();
            kind = QStringLiteral("math");
            arg = name == QLatin1String("displaystyle") ? QStringLiteral("0") : name == QLatin1String("textstyle") ? QStringLiteral("1")
                : name == QLatin1String("scriptstyle") ? QStringLiteral("2") : QStringLiteral("3");
        } else if (declarations().contains(name)) {
            takeCommand();
            kind = QStringLiteral("font");
            arg = declarations().value(name);
        } else {
            return false;
        }
        const int bodyStart = p;
        const QList<NodePtr> rest = row();
        NodePtr style = make(Node::Style, from, p);
        style->text = kind;
        style->arg = arg;
        style->flows = true;
        style->kids = {rowOf(rest, bodyStart, p)};
        out.append(style);
        return true;
    }

    /// Whatever `^`, `_` and `'` follow a base.
    NodePtr scripts(NodePtr base)
    {
        NodePtr sub;
        NodePtr sup;
        const int from = base ? base->start : p;
        while (true) {
            // Spaces between a base and its scripts do not count.
            int q = p;
            while (q < s.size() && s.at(q).isSpace()) {
                ++q;
            }
            if (q >= s.size()) {
                break;
            }
            const QChar c = s.at(q);
            if (c == QLatin1Char('\'')) {
                p = q;
                const int at = p;
                QList<NodePtr> primes;
                while (p < s.size() && s.at(p) == QLatin1Char('\'')) {
                    NodePtr prime = make(Node::Symbol, p, p + 1);
                    prime->text = QString(QChar(0x2032));
                    prime->command = QStringLiteral("prime");
                    primes.append(prime);
                    ++p;
                }
                if (sup) {
                    fail(at, p - at, QStringLiteral("Doble superíndice."));
                } else {
                    sup = rowOf(primes, at, p);
                    // x'^2: the exponent joins the primes.
                    skipSpace();
                    if (!atEnd() && s.at(p) == QLatin1Char('^')) {
                        ++p;
                        NodePtr more = argument(QStringLiteral("^"));
                        sup->kids.append(more);
                        sup->end = p;
                    }
                }
            } else if (c == QLatin1Char('^') || c == QLatin1Char('_')) {
                p = q + 1;
                NodePtr script = argument(QString(c));
                NodePtr &slot = c == QLatin1Char('^') ? sup : sub;
                if (slot) {
                    fail(q, 1, c == QLatin1Char('^') ? QStringLiteral("Doble superíndice.") : QStringLiteral("Doble subíndice."));
                } else {
                    slot = script;
                }
            } else {
                break;
            }
        }
        if (!sub && !sup) {
            return base;
        }
        NodePtr node = make(Node::Script, from, p);
        node->kids = {base, sub, sup};
        if (base && base->type == Node::Accent && (base->text == QLatin1String("overbrace") || base->text == QLatin1String("underbrace"))) {
            node->stacked = true;
        }
        return node;
    }

    NodePtr symbol(int from, const QString &text, Atom atom, const QString &command = QString())
    {
        NodePtr node = make(Node::Symbol, from, p);
        node->text = text;
        node->atom = atom;
        node->command = command;
        return node;
    }

    /// The delimiter after `\left`, `\right`, `\big`…: ok is false when it is not one.
    QString delimiter(bool *ok)
    {
        skipSpace();
        *ok = true;
        if (atEnd()) {
            *ok = false;
            return QString();
        }
        const QChar c = s.at(p);
        if (c == QLatin1Char('\\')) {
            const QString name = peekCommand();
            QString out;
            if (name == QLatin1String("{")) {
                out = QStringLiteral("{");
            } else if (name == QLatin1String("}")) {
                out = QStringLiteral("}");
            } else if (name == QLatin1String("|")) {
                out = QString(QChar(0x2016));
            } else if (const SymbolInfo *info = Symbols::find(name)) {
                const QLatin1String category(info->category);
                const bool arrow = info->code >= 0x2190 && info->code <= 0x21D5;
                if (category == QLatin1String("delimiters") || name == QLatin1String("lbrack") || name == QLatin1String("rbrack")
                    || name.endsWith(QLatin1String("ert")) || name == QLatin1String("backslash") || arrow) {
                    out = QString::fromUcs4(&info->code, 1);
                }
            }
            if (out.isEmpty()) {
                *ok = false;
                return QString();
            }
            takeCommand();
            return out;
        }
        if (c == QLatin1Char('.')) {
            ++p;
            return QString();
        }
        if (c == QLatin1Char('<')) {
            ++p;
            return QString(QChar(0x27E8));
        }
        if (c == QLatin1Char('>')) {
            ++p;
            return QString(QChar(0x27E9));
        }
        if (QStringLiteral("()[]|/").contains(c)) {
            ++p;
            return QString(c);
        }
        *ok = false;
        return QString();
    }

    NodePtr leftRight(int from)
    {
        bool ok = false;
        NodePtr node = make(Node::Delim, from);
        node->atom = Atom::Inner;
        node->left = delimiter(&ok);
        if (!ok) {
            fail(from, 5, QStringLiteral("Tras «\\left» va un delimitador: ( [ \\{ | . …"));
        }
        const int bodyStart = p;
        open += QLatin1Char('l');
        // A \middle inside is part of the body.
        const QList<NodePtr> kids = row();
        open.chop(1);
        const int bodyEnd = p;
        if (peekCommand() == QLatin1String("right") && !atEnd()) {
            takeCommand();
            node->right = delimiter(&ok);
            if (!ok) {
                fail(bodyEnd, 6, QStringLiteral("Tras «\\right» va un delimitador: ) ] \\} | . …"));
            }
        } else {
            fail(from, 5, QStringLiteral("Falta «\\right» para este «\\left»."));
        }
        node->kids = {rowOf(kids, bodyStart, bodyEnd)};
        node->end = p;
        return node;
    }

    /// The cells of an environment, up to its `\end` (or the end of the formula when `name` is empty).
    QList<QList<NodePtr>> parseCells(const QString &name)
    {
        QList<QList<NodePtr>> rows;
        QList<NodePtr> line;
        // t: the top of the formula, where `&` and `\\` break cells but no `\end` is due.
        open += name.isEmpty() ? QLatin1Char('t') : QLatin1Char('e');
        while (true) {
            const int from = p;
            const QList<NodePtr> kids = row();
            line.append(rowOf(kids, from, p));
            if (atEnd()) {
                break;
            }
            const QChar c = s.at(p);
            if (c == QLatin1Char('&')) {
                ++p;
                continue;
            }
            const QString command = peekCommand();
            if (command == QLatin1String("\\") || command == QLatin1String("cr")) {
                takeCommand();
                // \\[2pt]: the extra space is not kept.
                if (!atEnd() && s.at(p) == QLatin1Char('[')) {
                    const int close = s.indexOf(QLatin1Char(']'), p);
                    const QString inside = close < 0 ? QString() : s.mid(p + 1, close - p - 1);
                    if (close > 0 && (inside.endsWith(QLatin1String("pt")) || inside.endsWith(QLatin1String("em")) || inside.endsWith(QLatin1String("ex")))) {
                        p = close + 1;
                    }
                }
                rows.append(line);
                line.clear();
                continue;
            }
            if (command == QLatin1String("end") && !name.isEmpty()) {
                break;
            }
            if (c == QLatin1Char('}') && name == QLatin1String("substack")) {
                break;
            }
            break;
        }
        open.chop(1);
        // A last line with nothing in it is the `\\` that ended the one before.
        const bool empty = line.size() == 1 && line.first()->kids.isEmpty();
        if (!empty || rows.isEmpty()) {
            rows.append(line);
        }
        return rows;
    }

    NodePtr beginEnvironment(int from)
    {
        const QString name = rawArgument(QStringLiteral("begin"));
        const Environment *env = environment(name);
        NodePtr node = make(Node::Matrix, from);
        node->text = name;
        if (!env) {
            fail(from, p - from, QStringLiteral("Entorno desconocido «%1».").arg(name));
            node->columns = QStringLiteral("c");
        } else {
            node->left = QString::fromUtf8(env->left);
            node->right = QString::fromUtf8(env->right);
            node->columns = QString::fromLatin1(env->columns);
        }
        if (name == QLatin1String("array")) {
            QString spec = rawArgument(QStringLiteral("begin{array}"));
            spec.remove(QLatin1Char('|')).remove(QLatin1Char(' '));
            node->columns = spec.isEmpty() ? QStringLiteral("c") : spec;
        } else if (name == QLatin1String("alignat")) {
            rawArgumentQuiet();
        }
        node->cells = parseCells(name.isEmpty() ? QStringLiteral("?") : name);
        if (peekCommand() == QLatin1String("end") && !atEnd()) {
            const int at = p;
            takeCommand();
            const QString closing = rawArgument(QStringLiteral("end"));
            if (closing != name) {
                fail(at, p - at, QStringLiteral("«\\end{%1}» no cierra «\\begin{%2}».").arg(closing, name));
            }
        } else {
            fail(from, 6, QStringLiteral("Falta «\\end{%1}».").arg(name));
        }
        node->end = p;
        return node;
    }

    NodePtr sizedDelimiter(int from, const QString &name)
    {
        // \big \Big \bigg \Bigg, with l, r or m after.
        const QChar last = name.at(name.size() - 1);
        const QString stem = (last == QLatin1Char('l') || last == QLatin1Char('r') || last == QLatin1Char('m')) ? name.left(name.size() - 1) : name;
        const int step = stem == QLatin1String("big") ? 1 : stem == QLatin1String("Big") ? 2 : stem == QLatin1String("bigg") ? 3 : 4;
        bool ok = false;
        const QString delim = delimiter(&ok);
        if (!ok) {
            fail(from, name.size() + 1, QStringLiteral("Tras «\\%1» va un delimitador.").arg(name));
        }
        NodePtr node = symbol(from, delim, last == QLatin1Char('l') ? Atom::Open : last == QLatin1Char('r') ? Atom::Close
            : last == QLatin1Char('m') ? Atom::Rel : Atom::Ord);
        node->sized = step;
        return node;
    }

    NodePtr command(int from)
    {
        const QString name = takeCommand();
        if (name.isEmpty()) {
            fail(from, 1, QStringLiteral("«\\» suelta."));
            NodePtr error = make(Node::Error, from, p);
            error->text = QStringLiteral("\\");
            return error;
        }
        // --- One character after the backslash ---
        if (name.size() == 1 && !name.at(0).isLetter()) {
            const QChar c = name.at(0);
            switch (c.unicode()) {
            case ',': return space(from, 3);
            case ':': case '>': return space(from, 4);
            case ';': return space(from, 5);
            case '!': return space(from, -3);
            case ' ': case '\n': case '\t': return space(from, 6);
            case '{': return symbol(from, QStringLiteral("{"), Atom::Open, QStringLiteral("lbrace"));
            case '}': return symbol(from, QStringLiteral("}"), Atom::Close, QStringLiteral("rbrace"));
            case '|': return symbol(from, QString(QChar(0x2016)), Atom::Ord, QStringLiteral("Vert"));
            case '_': case '&': case '%': case '$': case '#': return symbol(from, QString(c), Atom::Ord);
            default: break;
            }
            fail(from, 2, QStringLiteral("Orden desconocida «\\%1».").arg(name));
            NodePtr error = make(Node::Error, from, p);
            error->text = QLatin1Char('\\') + name;
            return error;
        }
        if (name == QLatin1String("frac") || name == QLatin1String("dfrac") || name == QLatin1String("tfrac") || name == QLatin1String("cfrac")
            || name == QLatin1String("binom") || name == QLatin1String("dbinom") || name == QLatin1String("tbinom")) {
            NodePtr node = make(Node::Frac, from);
            NodePtr numerator = argument(name);
            NodePtr denominator = argument(name);
            node->kids = {numerator, denominator};
            node->rule = !name.endsWith(QLatin1String("binom"));
            if (!node->rule) {
                node->left = QStringLiteral("(");
                node->right = QStringLiteral(")");
            }
            node->mathStyle = name.startsWith(QLatin1Char('d')) || name == QLatin1String("cfrac") ? 0 : name.startsWith(QLatin1Char('t')) ? 1 : -1;
            node->end = p;
            return node;
        }
        if (name == QLatin1String("sqrt")) {
            NodePtr node = make(Node::Sqrt, from);
            NodePtr index;
            skipSpace();
            if (!atEnd() && s.at(p) == QLatin1Char('[')) {
                const int at = p;
                ++p;
                open += QLatin1Char('b');
                const QList<NodePtr> kids = row();
                open.chop(1);
                if (!atEnd() && s.at(p) == QLatin1Char(']')) {
                    ++p;
                } else {
                    fail(at, 1, QStringLiteral("Falta «]» para el índice de la raíz."));
                }
                index = rowOf(kids, at, p);
            }
            NodePtr body = argument(name);
            node->kids = {body, index};
            node->end = p;
            return node;
        }
        if (name == QLatin1String("left")) {
            return leftRight(from);
        }
        if (name == QLatin1String("middle")) {
            bool ok = false;
            const QString delim = delimiter(&ok);
            if (!ok) {
                fail(from, 7, QStringLiteral("Tras «\\middle» va un delimitador."));
            }
            NodePtr node = symbol(from, delim, Atom::Ord);
            node->middle = true;
            if (!open.contains(QLatin1Char('l'))) {
                fail(from, 7, QStringLiteral("«\\middle» fuera de «\\left … \\right»."));
                node->middle = false;
            }
            return node;
        }
        if (name == QLatin1String("begin")) {
            return beginEnvironment(from);
        }
        if (name == QLatin1String("substack")) {
            NodePtr node = make(Node::Matrix, from);
            node->text = name;
            node->columns = QStringLiteral("c");
            skipSpace();
            if (!atEnd() && s.at(p) == QLatin1Char('{')) {
                ++p;
                open += QLatin1Char('g');
                node->cells = parseCells(name);
                open.chop(1);
                if (!atEnd() && s.at(p) == QLatin1Char('}')) {
                    ++p;
                } else {
                    fail(from, 9, QStringLiteral("Falta «}»."));
                }
            } else {
                fail(from, 9, QStringLiteral("Falta el argumento de «\\substack»."));
            }
            node->end = p;
            return node;
        }
        if (name == QLatin1String("text") || name == QLatin1String("textrm") || name == QLatin1String("mbox") || name == QLatin1String("textbf")
            || name == QLatin1String("textit") || name == QLatin1String("textnormal") || name == QLatin1String("textsf") || name == QLatin1String("texttt")) {
            NodePtr node = make(Node::Text, from);
            node->text = rawArgument(name);
            node->arg = name == QLatin1String("textbf") ? QStringLiteral("bf") : name == QLatin1String("textit") ? QStringLiteral("it")
                : name == QLatin1String("textsf") ? QStringLiteral("sf") : name == QLatin1String("texttt") ? QStringLiteral("tt") : QString();
            node->end = p;
            return node;
        }
        if (name == QLatin1String("operatorname") || name == QLatin1String("operatorname*")) {
            NodePtr node = make(Node::Symbol, from);
            node->text = rawArgument(name).simplified();
            node->atom = Atom::Op;
            node->upright = true;
            node->movable = name.endsWith(QLatin1Char('*'));
            node->command = QStringLiteral("operatorname");
            node->end = p;
            return node;
        }
        if (fonts().contains(name)) {
            NodePtr node = make(Node::Style, from);
            node->text = QStringLiteral("font");
            node->arg = fonts().value(name);
            node->kids = {argument(name)};
            node->end = p;
            return node;
        }
        if (isAccent(name)) {
            NodePtr node = make(Node::Accent, from);
            node->text = name;
            node->kids = {argument(name)};
            node->end = p;
            return node;
        }
        if (name == QLatin1String("textcolor")) {
            NodePtr node = make(Node::Style, from);
            node->text = QStringLiteral("color");
            node->arg = rawArgument(name);
            if (Latex::colorName(node->arg).isEmpty()) {
                fail(from, p - from, QStringLiteral("Color desconocido «%1».").arg(node->arg));
            }
            node->flows = true;
            node->kids = {argument(name)};
            node->end = p;
            return node;
        }
        if (name == QLatin1String("boxed") || name == QLatin1String("phantom") || name == QLatin1String("mathrel") || name == QLatin1String("mathbin")
            || name == QLatin1String("mathop") || name == QLatin1String("mathord")) {
            NodePtr node = make(Node::Style, from);
            node->text = name == QLatin1String("boxed") ? QStringLiteral("box") : name == QLatin1String("phantom") ? QStringLiteral("phantom") : QStringLiteral("class");
            node->arg = name.mid(4);
            node->atom = name == QLatin1String("mathrel") ? Atom::Rel : name == QLatin1String("mathbin") ? Atom::Bin
                : name == QLatin1String("mathop") ? Atom::Op : Atom::Ord;
            node->movable = name == QLatin1String("mathop");
            node->kids = {argument(name)};
            node->end = p;
            return node;
        }
        if (name == QLatin1String("overset") || name == QLatin1String("underset") || name == QLatin1String("stackrel")) {
            NodePtr script = argument(name);
            NodePtr base = argument(name);
            NodePtr node = make(Node::Script, from, p);
            node->stacked = true;
            node->kids = name == QLatin1String("underset") ? QList<NodePtr>{base, script, NodePtr()} : QList<NodePtr>{base, NodePtr(), script};
            if (name == QLatin1String("stackrel")) {
                node->atom = Atom::Rel;
                node->arg = QStringLiteral("rel");
            }
            return node;
        }
        if (name.startsWith(QLatin1String("big"), Qt::CaseInsensitive) && name.size() <= 5 && !Symbols::find(name)) {
            return sizedDelimiter(from, name);
        }
        if (name == QLatin1String("quad")) return space(from, 18);
        if (name == QLatin1String("qquad")) return space(from, 36);
        if (name == QLatin1String("enspace")) return space(from, 9);
        if (name == QLatin1String("thinspace")) return space(from, 3);
        if (name == QLatin1String("medspace")) return space(from, 4);
        if (name == QLatin1String("thickspace")) return space(from, 5);
        if (name == QLatin1String("negthinspace")) return space(from, -3);
        if (name == QLatin1String("label") || name == QLatin1String("tag")) {
            rawArgument(name);
            return NodePtr();
        }
        if (name == QLatin1String("nonumber") || name == QLatin1String("notag") || name == QLatin1String("hline") || name == QLatin1String("displaylimits")) {
            return NodePtr();
        }
        if (name == QLatin1String("not")) {
            skipSpace();
            NodePtr next = (atEnd() || isStopper()) ? NodePtr() : atom();
            if (!next || next->type != Node::Symbol) {
                fail(from, 4, QStringLiteral("«\\not» debe ir delante de un símbolo."));
                return next;
            }
            if (next->text == QLatin1String("=")) {
                next->text = QString(QChar(0x2260));
            } else if (next->text == QString(QChar(0x2208))) {
                next->text = QString(QChar(0x2209));
            } else {
                next->text += QChar(0x0338);
            }
            next->start = from;
            next->command.clear();
            return next;
        }
        if (name == QLatin1String("pmod") || name == QLatin1String("mod")) {
            QList<NodePtr> kids;
            kids.append(space(from, name == QLatin1String("pmod") ? 8 : 12));
            if (name == QLatin1String("pmod")) {
                kids.append(symbol(from, QStringLiteral("("), Atom::Open));
            }
            NodePtr mod = symbol(from, QStringLiteral("mod"), Atom::Ord);
            mod->upright = true;
            kids.append(mod);
            kids.append(space(from, 6));
            if (name == QLatin1String("pmod")) {
                kids.append(argument(name));
                kids.append(symbol(from, QStringLiteral(")"), Atom::Close));
            }
            NodePtr node = rowOf(kids, from, p);
            node->flows = true;
            return node;
        }
        if (const FunctionInfo *function = Symbols::function(name)) {
            NodePtr node = symbol(from, QString::fromLatin1(function->text), name == QLatin1String("bmod") ? Atom::Bin : Atom::Op, name);
            node->upright = true;
            node->movable = function->limits;
            return node;
        }
        if (const SymbolInfo *info = Symbols::find(name)) {
            NodePtr node = symbol(from, QString::fromUcs4(&info->code, 1), info->atom, name);
            node->big = QLatin1String(info->category) == QLatin1String("big");
            node->movable = info->limits;
            return node;
        }
        fail(from, name.size() + 1, QStringLiteral("Orden desconocida «\\%1».").arg(name));
        NodePtr error = make(Node::Error, from, p);
        error->text = QLatin1Char('\\') + name;
        return error;
    }

    NodePtr space(int from, qreal mu)
    {
        NodePtr node = make(Node::Space, from, p);
        node->width = mu;
        return node;
    }

    /// One thing: a group, a command with its arguments, a number, a character.
    NodePtr atom()
    {
        const int from = p;
        const QChar c = s.at(p);
        if (c == QLatin1Char('{')) {
            return group();
        }
        if (c == QLatin1Char('\\')) {
            return command(from);
        }
        if (c.isDigit()) {
            while (p < s.size() && (s.at(p).isDigit() || (s.at(p) == QLatin1Char('.') && p + 1 < s.size() && s.at(p + 1).isDigit()))) {
                ++p;
            }
            NodePtr node = symbol(from, s.mid(from, p - from), Atom::Ord);
            node->upright = true;
            return node;
        }
        // A character outside the BMP is two QChars.
        char32_t code = c.unicode();
        int length = 1;
        if (c.isHighSurrogate() && p + 1 < s.size() && s.at(p + 1).isLowSurrogate()) {
            code = QChar::surrogateToUcs4(c, s.at(p + 1));
            length = 2;
        }
        p += length;
        switch (code) {
        case '+': return symbol(from, QStringLiteral("+"), Atom::Bin);
        case '-': return symbol(from, QString(QChar(0x2212)), Atom::Bin);
        case '*': return symbol(from, QString(QChar(0x2217)), Atom::Bin);
        case '=': return symbol(from, QStringLiteral("="), Atom::Rel);
        case '<': return symbol(from, QStringLiteral("<"), Atom::Rel);
        case '>': return symbol(from, QStringLiteral(">"), Atom::Rel);
        case ':': return symbol(from, QStringLiteral(":"), Atom::Rel);
        case '(': case '[': return symbol(from, QString(c), Atom::Open);
        case ')': case ']': case '!': case '?': return symbol(from, QString(c), Atom::Close);
        case ',': case ';': return symbol(from, QString(c), Atom::Punct);
        case '~': return space(from, 6);
        case '$': return NodePtr();
        case '#':
            fail(from, 1, QStringLiteral("«#» no puede ir en una fórmula."));
            {
                NodePtr error = make(Node::Error, from, p);
                error->text = QStringLiteral("#");
                return error;
            }
        default: break;
        }
        if (const SymbolInfo *info = code > 127 ? Symbols::byCode(code) : nullptr) {
            NodePtr node = symbol(from, s.mid(from, length), info->atom, QString::fromLatin1(info->command));
            node->big = QLatin1String(info->category) == QLatin1String("big");
            node->movable = info->limits;
            return node;
        }
        return symbol(from, s.mid(from, length), Atom::Ord);
    }
};

}

namespace Latex {

Parsed parse(const QString &source)
{
    return Parser(source).run();
}

QString colorName(const QString &name)
{
    static const QHash<QString, QString> extra = {
        {QStringLiteral("darkgray"), QStringLiteral("#404040")}, {QStringLiteral("lightgray"), QStringLiteral("#bfbfbf")},
        {QStringLiteral("lime"), QStringLiteral("#bfff00")}, {QStringLiteral("forestgreen"), QStringLiteral("#009b55")},
        {QStringLiteral("royalblue"), QStringLiteral("#0071bc")}, {QStringLiteral("violet"), QStringLiteral("#800080")},
    };
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }
    const auto it = extra.constFind(trimmed.toLower());
    if (it != extra.constEnd()) {
        return it.value();
    }
    const QColor color = QColor::fromString(trimmed.toLower());
    return color.isValid() ? color.name(QColor::HexRgb) : QString();
}

int matchingBrace(const QString &source, int position)
{
    if (position < 0 || position >= source.size()) {
        return -1;
    }
    const auto escaped = [&source](int at) {
        int slashes = 0;
        while (at - 1 - slashes >= 0 && source.at(at - 1 - slashes) == QLatin1Char('\\')) {
            ++slashes;
        }
        return slashes % 2 == 1;
    };
    const QChar c = source.at(position);
    if ((c != QLatin1Char('{') && c != QLatin1Char('}')) || escaped(position)) {
        return -1;
    }
    const int step = c == QLatin1Char('{') ? 1 : -1;
    int depth = 0;
    for (int i = position; i >= 0 && i < source.size(); i += step) {
        const QChar d = source.at(i);
        if ((d == QLatin1Char('{') || d == QLatin1Char('}')) && !escaped(i)) {
            depth += d == QLatin1Char('{') ? 1 : -1;
            if (depth == 0) {
                return i;
            }
        }
    }
    return -1;
}

}
