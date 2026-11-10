#include "formula.h"

#include "eval_p.h"

#include <QHash>
#include <QRegularExpression>

#include <cmath>

// --- Names of cells -----------------------------------------------------------

QString Formula::columnName(int col)
{
    QString name;
    for (int n = col + 1; n > 0; n = (n - 1) / 26) {
        name.prepend(QChar(u'A' + (n - 1) % 26));
    }
    return name;
}

int Formula::columnIndex(const QString &name)
{
    if (name.isEmpty() || name.size() > 3) {
        return -1;
    }
    int col = 0;
    for (const QChar c : name) {
        const char16_t u = c.toUpper().unicode();
        if (u < u'A' || u > u'Z') {
            return -1;
        }
        col = col * 26 + (u - u'A' + 1);
    }
    return col <= Limits::Cols ? col - 1 : -1;
}

QString Formula::cellName(int row, int col)
{
    return columnName(col) + QString::number(row + 1);
}

bool Formula::parseCell(const QString &text, CellRef *ref)
{
    int i = 0;
    const int n = text.size();
    CellRef out;
    if (i < n && text.at(i) == QLatin1Char('$')) {
        out.absCol = true;
        ++i;
    }
    const int lettersFrom = i;
    while (i < n && text.at(i).unicode() < 128 && text.at(i).isLetter()) {
        ++i;
    }
    out.col = columnIndex(text.mid(lettersFrom, i - lettersFrom));
    if (out.col < 0) {
        return false;
    }
    if (i < n && text.at(i) == QLatin1Char('$')) {
        out.absRow = true;
        ++i;
    }
    const int digitsFrom = i;
    while (i < n && text.at(i).unicode() >= u'0' && text.at(i).unicode() <= u'9') {
        ++i;
    }
    if (i != n || i == digitsFrom || i - digitsFrom > 7) {
        return false;
    }
    const int row = text.mid(digitsFrom).toInt();
    if (row < 1 || row > Limits::Rows) {
        return false;
    }
    out.row = row - 1;
    *ref = out;
    return true;
}

bool Formula::parseArea(const QString &typed, int *r1, int *c1, int *r2, int *c2)
{
    QString text = typed.trimmed();
    text.remove(QLatin1Char('$'));
    const int colon = text.indexOf(QLatin1Char(':'));
    const QString first = colon < 0 ? text : text.left(colon);
    const QString second = colon < 0 ? text : text.mid(colon + 1);
    CellRef a, b;
    if (parseCell(first, &a) && parseCell(second, &b)) {
        *r1 = qMin(a.row, b.row);
        *r2 = qMax(a.row, b.row);
        *c1 = qMin(a.col, b.col);
        *c2 = qMax(a.col, b.col);
        return true;
    }
    if (colon < 0) {
        return false;
    }
    const int colA = columnIndex(first);
    const int colB = columnIndex(second);
    if (colA >= 0 && colB >= 0) {
        *r1 = 0;
        *r2 = Limits::Rows - 1;
        *c1 = qMin(colA, colB);
        *c2 = qMax(colA, colB);
        return true;
    }
    bool okA = false, okB = false;
    const int rowA = first.toInt(&okA);
    const int rowB = second.toInt(&okB);
    if (okA && okB && rowA >= 1 && rowB >= 1 && rowA <= Limits::Rows && rowB <= Limits::Rows) {
        *r1 = qMin(rowA, rowB) - 1;
        *r2 = qMax(rowA, rowB) - 1;
        *c1 = 0;
        *c2 = Limits::Cols - 1;
        return true;
    }
    return false;
}

QString Formula::areaName(int r1, int c1, int r2, int c2)
{
    if (r1 == r2 && c1 == c2) {
        return cellName(r1, c1);
    }
    if (r1 == 0 && r2 == Limits::Rows - 1) {
        return columnName(c1) + QLatin1Char(':') + columnName(c2);
    }
    if (c1 == 0 && c2 == Limits::Cols - 1) {
        return QString::number(r1 + 1) + QLatin1Char(':') + QString::number(r2 + 1);
    }
    return cellName(r1, c1) + QLatin1Char(':') + cellName(r2, c2);
}

QString Formula::quoteSheet(const QString &name)
{
    bool plain = !name.isEmpty() && !name.at(0).isDigit();
    for (const QChar c : name) {
        plain = plain && (c.isLetterOrNumber() || c == QLatin1Char('_'));
    }
    // «A1» or «TRUE» as a sheet's name would read as something else.
    CellRef ref;
    if (plain && !parseCell(name, &ref)) {
        return name;
    }
    QString quoted = name;
    quoted.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// --- Tokens -------------------------------------------------------------------

namespace
{
enum class T { End, Number, String, Word, Sheet, Error, Op, Open, Close, Sep, Colon };

struct Token {
    T kind = T::End;
    QString text;
    double num = 0;
};

bool isWordStart(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_') || c == QLatin1Char('$') || c == QLatin1Char('\\');
}
bool isWordPart(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$') || c == QLatin1Char('.');
}

bool tokenize(const QString &text, QList<Token> *out, QString *error, bool commaIsDecimal = false)
{
    // With «;» between arguments, a comma inside a number is its decimal mark.
    bool semicolons = commaIsDecimal;
    {
        bool quoted = false;
        for (const QChar c : text) {
            if (c == QLatin1Char('"')) {
                quoted = !quoted;
            } else if (c == QLatin1Char(';') && !quoted) {
                semicolons = true;
            }
        }
    }
    const int n = text.size();
    int depth = 0;
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (c.isSpace()) {
            ++i;
            continue;
        }
        Token token;
        if (c.isDigit() || ((c == QLatin1Char('.') || (c == QLatin1Char(',') && semicolons)) && i + 1 < n && text.at(i + 1).isDigit()
                            && (out->isEmpty() || out->last().kind == T::Op || out->last().kind == T::Open || out->last().kind == T::Sep))) {
            int j = i;
            QString literal;
            while (j < n && text.at(j).isDigit()) {
                literal += text.at(j++);
            }
            const bool commaDecimal = j + 1 < n && text.at(j) == QLatin1Char(',') && text.at(j + 1).isDigit() && (semicolons || depth == 0);
            if (j < n && (text.at(j) == QLatin1Char('.') || commaDecimal) && !(j + 1 < n && text.at(j) == QLatin1Char('.') && text.at(j + 1).isLetter())) {
                literal += QLatin1Char('.');
                ++j;
                while (j < n && text.at(j).isDigit()) {
                    literal += text.at(j++);
                }
            }
            if (j < n && (text.at(j) == QLatin1Char('e') || text.at(j) == QLatin1Char('E'))) {
                int k = j + 1;
                if (k < n && (text.at(k) == QLatin1Char('+') || text.at(k) == QLatin1Char('-'))) {
                    ++k;
                }
                if (k < n && text.at(k).isDigit()) {
                    literal += QLatin1Char('E') + text.mid(j + 1, k - j - 1);
                    while (k < n && text.at(k).isDigit()) {
                        literal += text.at(k++);
                    }
                    j = k;
                }
            }
            token.kind = T::Number;
            token.text = literal;
            token.num = literal.toDouble();
            i = j;
        } else if (c == QLatin1Char('"')) {
            QString value;
            int j = i + 1;
            bool closed = false;
            while (j < n) {
                if (text.at(j) == QLatin1Char('"')) {
                    if (j + 1 < n && text.at(j + 1) == QLatin1Char('"')) {
                        value += QLatin1Char('"');
                        j += 2;
                        continue;
                    }
                    closed = true;
                    ++j;
                    break;
                }
                value += text.at(j++);
            }
            if (!closed) {
                *error = QStringLiteral("Falta cerrar unas comillas.");
                return false;
            }
            token.kind = T::String;
            token.text = value;
            i = j;
        } else if (c == QLatin1Char('\'')) {
            // 'Mi hoja'!A1
            QString name;
            int j = i + 1;
            bool closed = false;
            while (j < n) {
                if (text.at(j) == QLatin1Char('\'')) {
                    if (j + 1 < n && text.at(j + 1) == QLatin1Char('\'')) {
                        name += QLatin1Char('\'');
                        j += 2;
                        continue;
                    }
                    closed = true;
                    ++j;
                    break;
                }
                name += text.at(j++);
            }
            if (!closed || j >= n || text.at(j) != QLatin1Char('!')) {
                *error = QStringLiteral("El nombre de la hoja no está bien escrito.");
                return false;
            }
            token.kind = T::Sheet;
            token.text = name;
            i = j + 1;
        } else if (c == QLatin1Char('#')) {
            static const QStringList errors = {QStringLiteral("#DIV/0!"), QStringLiteral("#VALUE!"), QStringLiteral("#REF!"), QStringLiteral("#NAME?"),
                                               QStringLiteral("#N/A"),    QStringLiteral("#NUM!"),   QStringLiteral("#NULL!"), QStringLiteral("#CIRC!")};
            bool found = false;
            for (const QString &name : errors) {
                if (text.mid(i, name.size()).compare(name, Qt::CaseInsensitive) == 0) {
                    token.kind = T::Error;
                    token.text = name;
                    i += name.size();
                    found = true;
                    break;
                }
            }
            if (!found) {
                *error = QStringLiteral("«#» no se entiende aquí.");
                return false;
            }
        } else if (isWordStart(c)) {
            int j = i;
            while (j < n && isWordPart(text.at(j))) {
                ++j;
            }
            token.text = text.mid(i, j - i);
            if (j < n && text.at(j) == QLatin1Char('!')) {
                token.kind = T::Sheet;
                ++j;
            } else {
                token.kind = T::Word;
            }
            i = j;
        } else if (c == QLatin1Char('(')) {
            token.kind = T::Open;
            ++depth;
            ++i;
        } else if (c == QLatin1Char(')')) {
            token.kind = T::Close;
            --depth;
            ++i;
        } else if (c == QLatin1Char(',') || c == QLatin1Char(';')) {
            token.kind = T::Sep;
            ++i;
        } else if (c == QLatin1Char(':')) {
            token.kind = T::Colon;
            ++i;
        } else if (c == QLatin1Char('<') || c == QLatin1Char('>')) {
            token.kind = T::Op;
            token.text = c;
            if (i + 1 < n && (text.at(i + 1) == QLatin1Char('=') || (c == QLatin1Char('<') && text.at(i + 1) == QLatin1Char('>')))) {
                token.text += text.at(i + 1);
                ++i;
            }
            ++i;
        } else if (QStringLiteral("+-*/^&=%").contains(c)) {
            token.kind = T::Op;
            token.text = c;
            ++i;
        } else if (c == QChar(0x2212) || c == QChar(0xd7) || c == QChar(0xf7)) {
            // − × ÷, typed or pasted from a document.
            token.kind = T::Op;
            token.text = c == QChar(0x2212) ? QStringLiteral("-") : c == QChar(0xd7) ? QStringLiteral("*") : QStringLiteral("/");
            ++i;
        } else {
            *error = QStringLiteral("«%1» no se entiende en una fórmula.").arg(c);
            return false;
        }
        out->append(token);
    }
    out->append(Token());
    return true;
}

class Parser
{
public:
    Parser(const QList<Token> &tokens, QString *error)
        : m_tokens(tokens)
        , m_error(error)
    {
    }

    NodePtr formula()
    {
        NodePtr node = comparison();
        if (node && peek().kind != T::End) {
            fail(QStringLiteral("Sobra algo al final de la fórmula."));
            return nullptr;
        }
        return node;
    }

private:
    const Token &peek(int ahead = 0) const { return m_tokens.at(qMin(m_at + ahead, int(m_tokens.size()) - 1)); }
    const Token &take() { return m_tokens.at(m_at < m_tokens.size() - 1 ? m_at++ : m_at); }
    bool isOp(const QString &op) const { return peek().kind == T::Op && peek().text == op; }
    NodePtr fail(const QString &message)
    {
        if (m_error && m_error->isEmpty()) {
            *m_error = message;
        }
        return nullptr;
    }
    static NodePtr binary(const QString &op, const NodePtr &left, const NodePtr &right)
    {
        auto node = std::make_shared<Node>();
        node->kind = Node::Binary;
        node->text = op;
        node->args = {left, right};
        return node;
    }

    NodePtr comparison()
    {
        NodePtr left = concat();
        while (left && peek().kind == T::Op && (peek().text == QLatin1String("=") || peek().text == QLatin1String("<>") || peek().text == QLatin1String("<")
                                                || peek().text == QLatin1String(">") || peek().text == QLatin1String("<=") || peek().text == QLatin1String(">="))) {
            const QString op = take().text;
            NodePtr right = concat();
            if (!right) {
                return nullptr;
            }
            left = binary(op, left, right);
        }
        return left;
    }
    NodePtr concat()
    {
        NodePtr left = additive();
        while (left && isOp(QStringLiteral("&"))) {
            take();
            NodePtr right = additive();
            if (!right) {
                return nullptr;
            }
            left = binary(QStringLiteral("&"), left, right);
        }
        return left;
    }
    NodePtr additive()
    {
        NodePtr left = term();
        while (left && (isOp(QStringLiteral("+")) || isOp(QStringLiteral("-")))) {
            const QString op = take().text;
            NodePtr right = term();
            if (!right) {
                return nullptr;
            }
            left = binary(op, left, right);
        }
        return left;
    }
    NodePtr term()
    {
        NodePtr left = power();
        while (left && (isOp(QStringLiteral("*")) || isOp(QStringLiteral("/")))) {
            const QString op = take().text;
            NodePtr right = power();
            if (!right) {
                return nullptr;
            }
            left = binary(op, left, right);
        }
        return left;
    }
    NodePtr power()
    {
        NodePtr left = unary();
        while (left && isOp(QStringLiteral("^"))) {
            take();
            NodePtr right = unary();
            if (!right) {
                return nullptr;
            }
            left = binary(QStringLiteral("^"), left, right);
        }
        return left;
    }
    NodePtr unary()
    {
        if (isOp(QStringLiteral("-")) || isOp(QStringLiteral("+"))) {
            const QString op = take().text;
            NodePtr operand = unary();
            if (!operand) {
                return nullptr;
            }
            auto node = std::make_shared<Node>();
            node->kind = Node::Unary;
            node->text = op;
            node->args = {operand};
            return node;
        }
        return postfix();
    }
    NodePtr postfix()
    {
        NodePtr node = primary();
        while (node && isOp(QStringLiteral("%"))) {
            take();
            auto percent = std::make_shared<Node>();
            percent->kind = Node::Percent;
            percent->args = {node};
            node = percent;
        }
        return node;
    }

    /// «A1», «$A$1», and after a colon the other corner; «A:C» and «1:5» too.
    NodePtr reference(const QString &sheet, bool hasSheet)
    {
        auto node = std::make_shared<Node>();
        node->sheet = sheet;
        node->hasSheet = hasSheet;
        const Token first = take();
        QString firstText = first.text;
        CellRef a;
        if (first.kind == T::Word && Formula::parseCell(firstText, &a)) {
            node->a = a;
            node->kind = Node::Ref;
            if (peek().kind == T::Colon && peek(1).kind == T::Word) {
                CellRef b;
                if (Formula::parseCell(peek(1).text, &b)) {
                    take();
                    take();
                    node->kind = Node::Range;
                    node->b = b;
                    order(node.get());
                }
            }
            return node;
        }
        // Whole columns or whole rows.
        if (peek().kind == T::Colon) {
            const Token second = peek(1);
            auto column = [](const QString &text, CellRef *ref) {
                QString letters = text;
                ref->absCol = letters.startsWith(QLatin1Char('$'));
                if (ref->absCol) {
                    letters.remove(0, 1);
                }
                ref->col = Formula::columnIndex(letters);
                return ref->col >= 0;
            };
            auto rowOf = [](const Token &token, CellRef *ref) {
                QString digits = token.text;
                ref->absRow = digits.startsWith(QLatin1Char('$'));
                if (ref->absRow) {
                    digits.remove(0, 1);
                }
                bool ok = false;
                const int row = digits.toInt(&ok);
                ref->row = row - 1;
                return ok && row >= 1 && row <= Limits::Rows;
            };
            CellRef b;
            if (first.kind == T::Word && second.kind == T::Word && column(firstText, &a) && column(second.text, &b)) {
                take();
                take();
                node->kind = Node::Range;
                node->fullCols = true;
                a.row = 0;
                b.row = Limits::Rows - 1;
                a.absRow = b.absRow = true;
                node->a = a;
                node->b = b;
                order(node.get());
                return node;
            }
            if ((first.kind == T::Number || first.kind == T::Word) && (second.kind == T::Number || second.kind == T::Word) && rowOf(first, &a) && rowOf(second, &b)) {
                take();
                take();
                node->kind = Node::Range;
                node->fullRows = true;
                a.col = 0;
                b.col = Limits::Cols - 1;
                a.absCol = b.absCol = true;
                node->a = a;
                node->b = b;
                order(node.get());
                return node;
            }
        }
        return nullptr;
    }
    static void order(Node *node)
    {
        if (node->a.row > node->b.row) {
            std::swap(node->a.row, node->b.row);
            std::swap(node->a.absRow, node->b.absRow);
        }
        if (node->a.col > node->b.col) {
            std::swap(node->a.col, node->b.col);
            std::swap(node->a.absCol, node->b.absCol);
        }
    }

    NodePtr primary()
    {
        const Token &token = peek();
        switch (token.kind) {
        case T::Number: {
            if (peek(1).kind == T::Colon && peek(2).kind == T::Number) {
                const int saved = m_at;
                if (NodePtr rows = reference(QString(), false)) {
                    return rows;
                }
                m_at = saved;
            }
            auto node = std::make_shared<Node>();
            node->kind = Node::Number;
            node->num = token.num;
            node->text = token.text;
            take();
            return node;
        }
        case T::String: {
            auto node = std::make_shared<Node>();
            node->kind = Node::String;
            node->text = token.text;
            take();
            return node;
        }
        case T::Error: {
            auto node = std::make_shared<Node>();
            node->kind = Node::Error;
            node->err = Value::errorFromText(token.text);
            take();
            return node;
        }
        case T::Open: {
            take();
            NodePtr inner = comparison();
            if (!inner) {
                return nullptr;
            }
            if (peek().kind != T::Close) {
                return fail(QStringLiteral("Falta cerrar un paréntesis."));
            }
            take();
            auto node = std::make_shared<Node>();
            node->kind = Node::Paren;
            node->args = {inner};
            return node;
        }
        case T::Sheet: {
            const QString sheet = take().text;
            const int saved = m_at;
            if (NodePtr ref = reference(sheet, true)) {
                return ref;
            }
            m_at = saved;
            return fail(QStringLiteral("Después de «%1!» va una celda o un rango.").arg(sheet));
        }
        case T::Word: {
            if (peek(1).kind == T::Open) {
                return call();
            }
            const int saved = m_at;
            if (NodePtr ref = reference(QString(), false)) {
                return ref;
            }
            m_at = saved;
            const QString upper = token.text.toUpper();
            if (upper == QLatin1String("TRUE") || upper == QLatin1String("VERDADERO") || upper == QLatin1String("FALSE") || upper == QLatin1String("FALSO")) {
                auto node = std::make_shared<Node>();
                node->kind = Node::Bool;
                node->num = upper == QLatin1String("TRUE") || upper == QLatin1String("VERDADERO") ? 1 : 0;
                take();
                return node;
            }
            // A name nobody defined: the formula is kept and gives #NAME?.
            auto node = std::make_shared<Node>();
            node->kind = Node::Func;
            node->text = upper;
            node->err = Err::Name;
            take();
            return node;
        }
        case T::End:
            return fail(QStringLiteral("La fórmula está incompleta."));
        default:
            return fail(QStringLiteral("«%1» no va ahí.").arg(token.kind == T::Close ? QStringLiteral(")") : token.kind == T::Sep ? QStringLiteral(";")
                                                                 : token.kind == T::Colon ? QStringLiteral(":") : token.text));
        }
    }

    NodePtr call()
    {
        const QString typed = take().text;
        take();
        auto node = std::make_shared<Node>();
        node->kind = Node::Func;
        const QString canonical = Formula::canonicalFunction(typed);
        node->text = canonical.isEmpty() ? typed.toUpper() : canonical;
        if (peek().kind == T::Close) {
            take();
            return node;
        }
        while (true) {
            if (peek().kind == T::Sep || peek().kind == T::Close) {
                // An argument left out: IF(A1;;2).
                node->args.append(std::make_shared<Node>());
            } else {
                NodePtr arg = comparison();
                if (!arg) {
                    return nullptr;
                }
                node->args.append(arg);
            }
            if (peek().kind == T::Sep) {
                take();
                continue;
            }
            if (peek().kind == T::Close) {
                take();
                return node;
            }
            return fail(QStringLiteral("Falta cerrar el paréntesis de %1.").arg(typed.toUpper()));
        }
    }

    QList<Token> m_tokens;
    QString *m_error;
    int m_at = 0;
};
}

namespace
{
/// Whether every function got a number of arguments it can take.
bool aritiesFit(const Node &node)
{
    if (node.kind == Node::Func) {
        const FuncDef *def = findFunction(node.text);
        if (def && (node.args.size() < def->min || (def->max >= 0 && node.args.size() > def->max))) {
            return false;
        }
    }
    for (const NodePtr &arg : node.args) {
        if (arg && !aritiesFit(*arg)) {
            return false;
        }
    }
    return true;
}
bool hasBareComma(const QString &text)
{
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (c == QLatin1Char(',') && !quoted) {
            return true;
        }
    }
    return false;
}
}

NodePtr Formula::parse(const QString &text, QString *error)
{
    QString local;
    QString *message = error ? error : &local;
    message->clear();
    QList<Token> tokens;
    NodePtr node;
    if (tokenize(text, &tokens, message)) {
        Parser parser(tokens, message);
        node = parser.formula();
    }
    if ((!node || !aritiesFit(*node)) && hasBareComma(text)) {
        // «ENTERO(1,5)» has no «;» to tell, but with a comma between arguments
        // it takes one too many: the comma was a decimal mark.
        QString second;
        QList<Token> spanish;
        if (tokenize(text, &spanish, &second, true)) {
            Parser parser(spanish, &second);
            const NodePtr other = parser.formula();
            if (other && aritiesFit(*other)) {
                message->clear();
                return other;
            }
        }
    }
    if (!node && message->isEmpty()) {
        *message = QStringLiteral("La fórmula no se entiende.");
    }
    return node;
}

// --- Back to text ---------------------------------------------------------------

namespace
{
QString printCell(const CellRef &ref)
{
    return (ref.absCol ? QStringLiteral("$") : QString()) + Formula::columnName(ref.col) + (ref.absRow ? QStringLiteral("$") : QString()) + QString::number(ref.row + 1);
}

void printNode(const Node &node, bool spanish, QString &out)
{
    switch (node.kind) {
    case Node::Number: {
        QString literal = node.text.isEmpty() ? QString::number(node.num, 'g', 15) : node.text;
        if (spanish) {
            literal.replace(QLatin1Char('.'), QLatin1Char(','));
        }
        out += literal;
        break;
    }
    case Node::String: {
        QString escaped = node.text;
        escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
        out += QLatin1Char('"') + escaped + QLatin1Char('"');
        break;
    }
    case Node::Bool:
        out += spanish ? (node.num != 0 ? QStringLiteral("VERDADERO") : QStringLiteral("FALSO")) : (node.num != 0 ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
        break;
    case Node::Error:
        out += Value::errorText(node.err);
        break;
    case Node::Missing:
        break;
    case Node::Ref:
    case Node::Range:
        if (node.hasSheet) {
            out += Formula::quoteSheet(node.sheet) + QLatin1Char('!');
        }
        if (node.kind == Node::Ref) {
            out += printCell(node.a);
        } else if (node.fullCols) {
            out += (node.a.absCol ? QStringLiteral("$") : QString()) + Formula::columnName(node.a.col) + QLatin1Char(':') + (node.b.absCol ? QStringLiteral("$") : QString())
                + Formula::columnName(node.b.col);
        } else if (node.fullRows) {
            out += (node.a.absRow ? QStringLiteral("$") : QString()) + QString::number(node.a.row + 1) + QLatin1Char(':') + (node.b.absRow ? QStringLiteral("$") : QString())
                + QString::number(node.b.row + 1);
        } else {
            out += printCell(node.a) + QLatin1Char(':') + printCell(node.b);
        }
        break;
    case Node::Unary:
        out += node.text;
        printNode(*node.args.at(0), spanish, out);
        break;
    case Node::Binary:
        printNode(*node.args.at(0), spanish, out);
        out += node.text;
        printNode(*node.args.at(1), spanish, out);
        break;
    case Node::Percent:
        printNode(*node.args.at(0), spanish, out);
        out += QLatin1Char('%');
        break;
    case Node::Paren:
        out += QLatin1Char('(');
        printNode(*node.args.at(0), spanish, out);
        out += QLatin1Char(')');
        break;
    case Node::Func: {
        if (node.err == Err::Name && node.args.isEmpty()) {
            // A bare name.
            out += node.text;
            break;
        }
        QString name = node.text;
        if (spanish) {
            if (const FuncDef *def = findFunction(node.text)) {
                if (def->es[0] != '\0') {
                    name = QString::fromUtf8(def->es);
                }
            }
        }
        out += name + QLatin1Char('(');
        for (int i = 0; i < node.args.size(); ++i) {
            if (i > 0) {
                out += spanish ? QLatin1Char(';') : QLatin1Char(',');
            }
            printNode(*node.args.at(i), spanish, out);
        }
        out += QLatin1Char(')');
        break;
    }
    }
}

/// A copy of a tree with every node passed through `change`, which may give
/// back another node for references.
NodePtr rebuilt(const NodePtr &node, const std::function<NodePtr(const Node &)> &change)
{
    if (!node) {
        return node;
    }
    if (node->kind == Node::Ref || node->kind == Node::Range) {
        NodePtr changed = change(*node);
        return changed ? changed : node;
    }
    if (node->args.isEmpty()) {
        return node;
    }
    bool differs = false;
    QList<NodePtr> args;
    args.reserve(node->args.size());
    for (const NodePtr &arg : node->args) {
        NodePtr next = rebuilt(arg, change);
        differs = differs || next != arg;
        args.append(next);
    }
    if (!differs) {
        return node;
    }
    auto copy = std::make_shared<Node>(*node);
    copy->args = args;
    return copy;
}

NodePtr refError()
{
    auto node = std::make_shared<Node>();
    node->kind = Node::Error;
    node->err = Err::Ref;
    return node;
}
}

QString Formula::print(const NodePtr &node, bool spanish)
{
    QString out;
    if (node) {
        printNode(*node, spanish, out);
    }
    return out;
}

NodePtr Formula::shifted(const NodePtr &node, int rows, int cols)
{
    if (rows == 0 && cols == 0) {
        return node;
    }
    return rebuilt(node, [rows, cols](const Node &ref) -> NodePtr {
        auto copy = std::make_shared<Node>(ref);
        auto move = [rows, cols](CellRef &cell, bool fullCols, bool fullRows) {
            if (!cell.absRow && !fullCols) {
                cell.row += rows;
            }
            if (!cell.absCol && !fullRows) {
                cell.col += cols;
            }
            return cell.row >= 0 && cell.row < Limits::Rows && cell.col >= 0 && cell.col < Limits::Cols;
        };
        bool ok = move(copy->a, ref.fullCols, ref.fullRows);
        if (ref.kind == Node::Range) {
            ok = move(copy->b, ref.fullCols, ref.fullRows) && ok;
        }
        if (!ok) {
            return refError();
        }
        return copy;
    });
}

NodePtr Formula::adjusted(const NodePtr &node, const SheetEdit &edit, const QString &home)
{
    const bool rowsEdit = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::DeleteRows;
    const bool inserting = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::InsertCols;
    const int limit = rowsEdit ? Limits::Rows : Limits::Cols;
    return rebuilt(node, [&](const Node &ref) -> NodePtr {
        const QString &target = ref.hasSheet ? ref.sheet : home;
        if (target.compare(edit.sheet, Qt::CaseInsensitive) != 0) {
            return nullptr;
        }
        if ((rowsEdit && ref.fullCols) || (!rowsEdit && ref.fullRows)) {
            return nullptr;
        }
        int first = rowsEdit ? ref.a.row : ref.a.col;
        int last = ref.kind == Node::Range ? (rowsEdit ? ref.b.row : ref.b.col) : first;
        if (inserting) {
            if (first >= edit.at) {
                first += edit.count;
            }
            if (last >= edit.at) {
                last += edit.count;
            }
            if (last >= limit) {
                return refError();
            }
        } else {
            const int end = edit.at + edit.count; // first index that survives after the hole
            if (first >= edit.at && last < end) {
                return refError();
            }
            if (first >= end) {
                first -= edit.count;
            } else if (first >= edit.at) {
                first = edit.at;
            }
            if (last >= end) {
                last -= edit.count;
            } else if (last >= edit.at) {
                last = edit.at - 1;
            }
        }
        const int oldFirst = rowsEdit ? ref.a.row : ref.a.col;
        const int oldLast = ref.kind == Node::Range ? (rowsEdit ? ref.b.row : ref.b.col) : oldFirst;
        if (first == oldFirst && last == oldLast) {
            return nullptr;
        }
        auto copy = std::make_shared<Node>(ref);
        (rowsEdit ? copy->a.row : copy->a.col) = first;
        if (ref.kind == Node::Range) {
            (rowsEdit ? copy->b.row : copy->b.col) = last;
        }
        return copy;
    });
}

NodePtr Formula::sheetRenamed(const NodePtr &node, const QString &from, const QString &to)
{
    return rebuilt(node, [&](const Node &ref) -> NodePtr {
        if (!ref.hasSheet || ref.sheet.compare(from, Qt::CaseInsensitive) != 0) {
            return nullptr;
        }
        auto copy = std::make_shared<Node>(ref);
        copy->sheet = to;
        return copy;
    });
}

void Formula::precedents(const NodePtr &node, const EvalContext &context, QList<RangeRef> *out)
{
    if (!node) {
        return;
    }
    if (node->kind == Node::Ref || node->kind == Node::Range) {
        RangeRef range;
        range.sheet = node->hasSheet ? context.host->sheetId(node->sheet) : context.sheet;
        if (range.sheet < 0) {
            return;
        }
        range.r1 = node->a.row;
        range.c1 = node->a.col;
        range.r2 = node->kind == Node::Range ? node->b.row : node->a.row;
        range.c2 = node->kind == Node::Range ? node->b.col : node->a.col;
        out->append(range);
        return;
    }
    for (const NodePtr &arg : node->args) {
        precedents(arg, context, out);
    }
}

bool Formula::isVolatile(const NodePtr &node)
{
    if (!node) {
        return false;
    }
    if (node->kind == Node::Func) {
        const FuncDef *def = findFunction(node->text);
        if (def && def->isVolatile) {
            return true;
        }
    }
    for (const NodePtr &arg : node->args) {
        if (isVolatile(arg)) {
            return true;
        }
    }
    return false;
}
