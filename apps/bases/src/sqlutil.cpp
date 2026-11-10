#include "sqlutil.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace Sql
{

QString ident(const QString &name)
{
    QString quoted = name;
    quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString literal(const QVariant &value)
{
    if (value.isNull() || !value.isValid()) {
        return QStringLiteral("NULL");
    }
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("1") : QStringLiteral("0");
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QString::number(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float: {
        QString text = QString::number(value.toDouble(), 'g', 17);
        // The shortest form that reads back the same.
        const QString shorter = QString::number(value.toDouble(), 'g', 15);
        if (shorter.toDouble() == value.toDouble()) {
            text = shorter;
        }
        if (!text.contains(QLatin1Char('.')) && !text.contains(QLatin1Char('e')) && !text.contains(QLatin1String("inf")) && !text.contains(QLatin1String("nan"))) {
            text += QLatin1String(".0");
        }
        return text;
    }
    case QMetaType::QByteArray:
        return QStringLiteral("X'") + QString::fromLatin1(value.toByteArray().toHex()) + QLatin1Char('\'');
    default: {
        QString text = value.toString();
        text.replace(QLatin1Char('\''), QLatin1String("''"));
        return QLatin1Char('\'') + text + QLatin1Char('\'');
    }
    }
}

QString affinity(const QString &declared)
{
    const QString type = declared.toUpper();
    if (type.contains(QLatin1String("INT"))) {
        return QStringLiteral("INTEGER");
    }
    if (type.contains(QLatin1String("CHAR")) || type.contains(QLatin1String("CLOB")) || type.contains(QLatin1String("TEXT"))) {
        return QStringLiteral("TEXT");
    }
    if (type.contains(QLatin1String("BLOB")) || type.isEmpty()) {
        return QStringLiteral("BLOB");
    }
    if (type.contains(QLatin1String("REAL")) || type.contains(QLatin1String("FLOA")) || type.contains(QLatin1String("DOUB"))) {
        return QStringLiteral("REAL");
    }
    return QStringLiteral("NUMERIC");
}

QString kind(const QString &declared)
{
    const QString type = declared.toUpper();
    if (type.contains(QLatin1String("BOOL"))) {
        return QStringLiteral("bool");
    }
    if (type.contains(QLatin1String("DATETIME")) || type.contains(QLatin1String("TIMESTAMP"))) {
        return QStringLiteral("datetime");
    }
    if (type.contains(QLatin1String("DATE"))) {
        return QStringLiteral("date");
    }
    const QString a = affinity(declared);
    if (a == QLatin1String("INTEGER")) {
        return QStringLiteral("integer");
    }
    if (a == QLatin1String("REAL") || a == QLatin1String("NUMERIC")) {
        return QStringLiteral("real");
    }
    if (a == QLatin1String("BLOB") && !type.isEmpty()) {
        return QStringLiteral("blob");
    }
    return QStringLiteral("text");
}

bool validType(const QString &declared)
{
    static const QRegularExpression shape(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*( [A-Za-z][A-Za-z0-9_]*)*( ?\\( ?\\d+ ?(, ?\\d+ ?)?\\))?$"));
    if (declared.isEmpty()) {
        return true;
    }
    if (!shape.match(declared).hasMatch()) {
        return false;
    }
    // Words that begin a constraint are not part of a type's name.
    static const QSet<QString> constraints = {QStringLiteral("NOT"), QStringLiteral("NULL"), QStringLiteral("PRIMARY"), QStringLiteral("KEY"), QStringLiteral("UNIQUE"),
                                              QStringLiteral("CHECK"), QStringLiteral("DEFAULT"), QStringLiteral("COLLATE"), QStringLiteral("REFERENCES"), QStringLiteral("GENERATED"),
                                              QStringLiteral("AS"), QStringLiteral("CONSTRAINT"), QStringLiteral("ON"), QStringLiteral("AUTOINCREMENT")};
    static const QRegularExpression word(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));
    auto it = word.globalMatch(declared);
    while (it.hasNext()) {
        if (constraints.contains(it.next().captured().toUpper())) {
            return false;
        }
    }
    return true;
}

static const QSet<QString> &keywordSet()
{
    static const QSet<QString> set = {
        QStringLiteral("ABORT"), QStringLiteral("ACTION"), QStringLiteral("ADD"), QStringLiteral("AFTER"), QStringLiteral("ALL"),
        QStringLiteral("ALTER"), QStringLiteral("ALWAYS"), QStringLiteral("ANALYZE"), QStringLiteral("AND"), QStringLiteral("AS"),
        QStringLiteral("ASC"), QStringLiteral("ATTACH"), QStringLiteral("AUTOINCREMENT"), QStringLiteral("BEFORE"), QStringLiteral("BEGIN"),
        QStringLiteral("BETWEEN"), QStringLiteral("BY"), QStringLiteral("CASCADE"), QStringLiteral("CASE"), QStringLiteral("CAST"),
        QStringLiteral("CHECK"), QStringLiteral("COLLATE"), QStringLiteral("COLUMN"), QStringLiteral("COMMIT"), QStringLiteral("CONFLICT"),
        QStringLiteral("CONSTRAINT"), QStringLiteral("CREATE"), QStringLiteral("CROSS"), QStringLiteral("CURRENT"), QStringLiteral("CURRENT_DATE"),
        QStringLiteral("CURRENT_TIME"), QStringLiteral("CURRENT_TIMESTAMP"), QStringLiteral("DATABASE"), QStringLiteral("DEFAULT"),
        QStringLiteral("DEFERRABLE"), QStringLiteral("DEFERRED"), QStringLiteral("DELETE"), QStringLiteral("DESC"), QStringLiteral("DETACH"),
        QStringLiteral("DISTINCT"), QStringLiteral("DO"), QStringLiteral("DROP"), QStringLiteral("EACH"), QStringLiteral("ELSE"),
        QStringLiteral("END"), QStringLiteral("ESCAPE"), QStringLiteral("EXCEPT"), QStringLiteral("EXCLUSIVE"), QStringLiteral("EXISTS"),
        QStringLiteral("EXPLAIN"), QStringLiteral("FAIL"), QStringLiteral("FALSE"), QStringLiteral("FILTER"), QStringLiteral("FIRST"),
        QStringLiteral("FOLLOWING"), QStringLiteral("FOR"), QStringLiteral("FOREIGN"), QStringLiteral("FROM"), QStringLiteral("FULL"),
        QStringLiteral("GENERATED"), QStringLiteral("GLOB"), QStringLiteral("GROUP"), QStringLiteral("HAVING"), QStringLiteral("IF"),
        QStringLiteral("IGNORE"), QStringLiteral("IMMEDIATE"), QStringLiteral("IN"), QStringLiteral("INDEX"), QStringLiteral("INDEXED"),
        QStringLiteral("INITIALLY"), QStringLiteral("INNER"), QStringLiteral("INSERT"), QStringLiteral("INSTEAD"), QStringLiteral("INTERSECT"),
        QStringLiteral("INTO"), QStringLiteral("IS"), QStringLiteral("ISNULL"), QStringLiteral("JOIN"), QStringLiteral("KEY"),
        QStringLiteral("LAST"), QStringLiteral("LEFT"), QStringLiteral("LIKE"), QStringLiteral("LIMIT"), QStringLiteral("MATCH"),
        QStringLiteral("NATURAL"), QStringLiteral("NO"), QStringLiteral("NOT"), QStringLiteral("NOTHING"), QStringLiteral("NOTNULL"),
        QStringLiteral("NULL"), QStringLiteral("NULLS"), QStringLiteral("OF"), QStringLiteral("OFFSET"), QStringLiteral("ON"),
        QStringLiteral("OR"), QStringLiteral("ORDER"), QStringLiteral("OUTER"), QStringLiteral("OVER"), QStringLiteral("PARTITION"),
        QStringLiteral("PLAN"), QStringLiteral("PRAGMA"), QStringLiteral("PRECEDING"), QStringLiteral("PRIMARY"), QStringLiteral("QUERY"),
        QStringLiteral("RAISE"), QStringLiteral("RECURSIVE"), QStringLiteral("REFERENCES"), QStringLiteral("REGEXP"), QStringLiteral("REINDEX"),
        QStringLiteral("RELEASE"), QStringLiteral("RENAME"), QStringLiteral("REPLACE"), QStringLiteral("RESTRICT"), QStringLiteral("RETURNING"),
        QStringLiteral("RIGHT"), QStringLiteral("ROLLBACK"), QStringLiteral("ROW"), QStringLiteral("ROWS"), QStringLiteral("SAVEPOINT"),
        QStringLiteral("SELECT"), QStringLiteral("SET"), QStringLiteral("STRICT"), QStringLiteral("TABLE"), QStringLiteral("TEMP"),
        QStringLiteral("TEMPORARY"), QStringLiteral("THEN"), QStringLiteral("TO"), QStringLiteral("TRANSACTION"), QStringLiteral("TRIGGER"),
        QStringLiteral("TRUE"), QStringLiteral("UNBOUNDED"), QStringLiteral("UNION"), QStringLiteral("UNIQUE"), QStringLiteral("UPDATE"),
        QStringLiteral("USING"), QStringLiteral("VACUUM"), QStringLiteral("VALUES"), QStringLiteral("VIEW"), QStringLiteral("VIRTUAL"),
        QStringLiteral("WHEN"), QStringLiteral("WHERE"), QStringLiteral("WINDOW"), QStringLiteral("WITH"), QStringLiteral("WITHOUT"),
    };
    return set;
}

static const QStringList &functionList()
{
    static const QStringList list = {
        QStringLiteral("abs"), QStringLiteral("avg"), QStringLiteral("coalesce"), QStringLiteral("count"), QStringLiteral("date"),
        QStringLiteral("datetime"), QStringLiteral("group_concat"), QStringLiteral("hex"), QStringLiteral("ifnull"), QStringLiteral("iif"),
        QStringLiteral("instr"), QStringLiteral("julianday"), QStringLiteral("length"), QStringLiteral("lower"), QStringLiteral("ltrim"),
        QStringLiteral("max"), QStringLiteral("min"), QStringLiteral("nullif"), QStringLiteral("printf"), QStringLiteral("random"),
        QStringLiteral("replace"), QStringLiteral("round"), QStringLiteral("rtrim"), QStringLiteral("strftime"), QStringLiteral("substr"),
        QStringLiteral("sum"), QStringLiteral("time"), QStringLiteral("total"), QStringLiteral("trim"), QStringLiteral("typeof"),
        QStringLiteral("unixepoch"), QStringLiteral("upper"),
    };
    return list;
}

bool isKeyword(const QString &word)
{
    return keywordSet().contains(word.toUpper());
}

QStringList keywords()
{
    QStringList list(keywordSet().begin(), keywordSet().end());
    list.sort();
    return list;
}

QStringList functions()
{
    return functionList();
}

QList<Token> tokenize(const QString &sql)
{
    QList<Token> tokens;
    const int n = sql.size();
    int i = 0;
    const auto wordChar = [](QChar c) {
        return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('$');
    };
    while (i < n) {
        const QChar c = sql.at(i);
        const int start = i;
        Token::Type type = Token::Operator;
        if (c.isSpace()) {
            while (i < n && sql.at(i).isSpace()) {
                ++i;
            }
            type = Token::Space;
        } else if (c == QLatin1Char('-') && i + 1 < n && sql.at(i + 1) == QLatin1Char('-')) {
            while (i < n && sql.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            type = Token::Comment;
        } else if (c == QLatin1Char('/') && i + 1 < n && sql.at(i + 1) == QLatin1Char('*')) {
            const int end = sql.indexOf(QLatin1String("*/"), i + 2);
            i = end < 0 ? n : end + 2;
            type = Token::Comment;
        } else if (c == QLatin1Char('\'') || c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('[')) {
            // A doubled quote inside is the quote itself; an unclosed one runs to the end.
            const QChar close = c == QLatin1Char('[') ? QLatin1Char(']') : c;
            ++i;
            while (i < n) {
                if (sql.at(i) == close) {
                    if (close != QLatin1Char(']') && i + 1 < n && sql.at(i + 1) == close) {
                        i += 2;
                        continue;
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            type = c == QLatin1Char('\'') ? Token::String : Token::Identifier;
        } else if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && sql.at(i + 1).isDigit())) {
            while (i < n && (sql.at(i).isLetterOrNumber() || sql.at(i) == QLatin1Char('.'))) {
                // The sign of an exponent belongs to the number.
                if ((sql.at(i) == QLatin1Char('e') || sql.at(i) == QLatin1Char('E')) && i + 1 < n && (sql.at(i + 1) == QLatin1Char('+') || sql.at(i + 1) == QLatin1Char('-'))) {
                    ++i;
                }
                ++i;
            }
            type = Token::Number;
        } else if (c == QLatin1Char('?')) {
            ++i;
            while (i < n && sql.at(i).isDigit()) {
                ++i;
            }
            type = Token::Parameter;
        } else if ((c == QLatin1Char(':') || c == QLatin1Char('@') || c == QLatin1Char('$')) && i + 1 < n && (sql.at(i + 1).isLetter() || sql.at(i + 1) == QLatin1Char('_'))) {
            ++i;
            while (i < n && wordChar(sql.at(i))) {
                ++i;
            }
            type = Token::Parameter;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            while (i < n && wordChar(sql.at(i))) {
                ++i;
            }
            const QString word = sql.mid(start, i - start);
            int after = i;
            while (after < n && sql.at(after).isSpace()) {
                ++after;
            }
            const bool called = after < n && sql.at(after) == QLatin1Char('(');
            if (called && functionList().contains(word.toLower())) {
                type = Token::Function;
            } else if (isKeyword(word)) {
                type = Token::Keyword;
            } else {
                type = called ? Token::Function : Token::Identifier;
            }
        } else {
            ++i;
            // Two-character operators stay together.
            if (i < n) {
                const QString two = sql.mid(start, 2);
                static const QStringList pairs = {QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("<>"), QStringLiteral("!="),
                                                  QStringLiteral("=="), QStringLiteral("||"), QStringLiteral("<<"), QStringLiteral(">>")};
                if (pairs.contains(two)) {
                    ++i;
                }
            }
            type = Token::Operator;
        }
        tokens.append({type, start, i - start});
    }
    return tokens;
}

QStringList split(const QString &script)
{
    QStringList statements;
    const QList<Token> tokens = tokenize(script);
    int from = 0;
    bool content = false;
    // A trigger holds semicolons between its BEGIN and its END.
    bool trigger = false;
    int depth = 0;
    QString previous;
    QStringList head;
    const auto flush = [&](int to) {
        const QString statement = script.mid(from, to - from).trimmed();
        if (content && !statement.isEmpty()) {
            statements.append(statement);
        }
        from = to + 1;
        content = false;
        trigger = false;
        depth = 0;
        head.clear();
    };
    for (const Token &token : tokens) {
        if (token.type == Token::Space || token.type == Token::Comment) {
            continue;
        }
        const QString text = script.mid(token.start, token.length);
        if (token.type == Token::Operator && text == QLatin1String(";")) {
            if (depth == 0) {
                flush(token.start);
            }
            continue;
        }
        content = true;
        if (token.type == Token::Keyword) {
            const QString word = text.toUpper();
            if (head.size() < 4) {
                head.append(word);
                if (word == QLatin1String("TRIGGER") && head.first() == QLatin1String("CREATE")) {
                    trigger = true;
                }
            }
            if (trigger) {
                if (word == QLatin1String("BEGIN") || word == QLatin1String("CASE")) {
                    ++depth;
                } else if (word == QLatin1String("END") && depth > 0) {
                    --depth;
                }
            }
            previous = word;
        }
    }
    flush(script.size());
    return statements;
}

QStringList parameters(const QString &sql)
{
    QStringList names;
    for (const Token &token : tokenize(sql)) {
        if (token.type == Token::Parameter && token.length > 1) {
            const QString name = sql.mid(token.start, token.length);
            if (!name.startsWith(QLatin1Char('?')) && !names.contains(name)) {
                names.append(name);
            }
        }
    }
    return names;
}

QString verb(const QString &sql)
{
    for (const Token &token : tokenize(sql)) {
        if (token.type == Token::Space || token.type == Token::Comment) {
            continue;
        }
        return sql.mid(token.start, token.length).toUpper();
    }
    return {};
}

bool selects(const QString &sql)
{
    const QString first = verb(sql);
    return first == QLatin1String("SELECT") || first == QLatin1String("WITH") || first == QLatin1String("VALUES");
}

QString trimmed(const QString &sql)
{
    const QList<Token> tokens = tokenize(sql);
    int end = 0;
    for (const Token &token : tokens) {
        if (token.type == Token::Space || token.type == Token::Comment) {
            continue;
        }
        if (token.type == Token::Operator && sql.at(token.start) == QLatin1Char(';')) {
            continue;
        }
        end = token.start + token.length;
    }
    int start = 0;
    while (start < end && sql.at(start).isSpace()) {
        ++start;
    }
    return sql.mid(start, end - start);
}

QString defaultClause(const QString &typed)
{
    const QString text = typed.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    bool number = false;
    text.toDouble(&number);
    if (number) {
        return text;
    }
    static const QStringList words = {QStringLiteral("NULL"), QStringLiteral("TRUE"), QStringLiteral("FALSE"), QStringLiteral("CURRENT_TIMESTAMP"),
                                      QStringLiteral("CURRENT_DATE"), QStringLiteral("CURRENT_TIME")};
    if (words.contains(text.toUpper())) {
        return text.toUpper();
    }
    const QList<Token> tokens = tokenize(text);
    if (tokens.size() == 1 && tokens.first().type == Token::String && text.size() >= 2 && text.endsWith(QLatin1Char('\''))) {
        // One whole string literal, properly closed.
        const QString inside = text.mid(1, text.size() - 2);
        QString check = inside;
        check.remove(QLatin1String("''"));
        if (!check.contains(QLatin1Char('\''))) {
            return text;
        }
    }
    if (text.startsWith(QLatin1Char('('))) {
        // An expression: its first parenthesis must close on the last character,
        // so nothing can follow it into the rest of the statement.
        int depth = 0;
        bool whole = true;
        for (int i = 0; i < tokens.size(); ++i) {
            const Token &token = tokens.at(i);
            const QString piece = text.mid(token.start, token.length);
            if (token.type == Token::Comment) {
                whole = false;
                break;
            }
            if (token.type == Token::String && (piece.size() < 2 || !piece.endsWith(QLatin1Char('\'')))) {
                whole = false;
                break;
            }
            if (token.type != Token::Operator) {
                continue;
            }
            if (piece == QLatin1String("(")) {
                ++depth;
            } else if (piece == QLatin1String(")")) {
                --depth;
                if (depth < 0 || (depth == 0 && i != tokens.size() - 1)) {
                    whole = false;
                    break;
                }
            } else if (piece == QLatin1String(";")) {
                whole = false;
                break;
            }
        }
        if (whole && depth == 0) {
            return text;
        }
    }
    return literal(text);
}

static QVariant typedValue(const QString &text)
{
    bool ok = false;
    const qlonglong whole = text.toLongLong(&ok);
    if (ok) {
        return whole;
    }
    const double real = text.toDouble(&ok);
    if (ok) {
        return real;
    }
    QString plain = text;
    if (plain.size() >= 2 && ((plain.startsWith(QLatin1Char('\'')) && plain.endsWith(QLatin1Char('\''))) || (plain.startsWith(QLatin1Char('"')) && plain.endsWith(QLatin1Char('"'))))) {
        plain = plain.mid(1, plain.size() - 2);
    }
    return plain;
}

QString criterion(const QString &expression, const QString &typed, QVariantList *binds)
{
    const QString text = typed.trimmed();
    if (text.isEmpty()) {
        return {};
    }
    const QString lower = text.toLower();
    if (lower == QLatin1String("null") || lower == QLatin1String("nulo") || lower == QLatin1String("es nulo") || lower == QLatin1String("is null")) {
        return expression + QStringLiteral(" IS NULL");
    }
    if (lower == QLatin1String("no null") || lower == QLatin1String("not null") || lower == QLatin1String("no nulo") || lower == QLatin1String("is not null") || lower == QLatin1String("!null")) {
        return expression + QStringLiteral(" IS NOT NULL");
    }
    static const QStringList operators = {QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("<>"), QStringLiteral("!="),
                                          QStringLiteral("="), QStringLiteral("<"), QStringLiteral(">")};
    for (const QString &op : operators) {
        if (text.startsWith(op)) {
            const QString rest = text.mid(op.size()).trimmed();
            if (rest.isEmpty()) {
                return {};
            }
            binds->append(typedValue(rest));
            return expression + QLatin1Char(' ') + (op == QLatin1String("!=") ? QStringLiteral("<>") : op) + QStringLiteral(" ?");
        }
    }
    const int range = text.indexOf(QLatin1String(".."));
    if (range > 0 && range < text.size() - 2) {
        binds->append(typedValue(text.left(range).trimmed()));
        binds->append(typedValue(text.mid(range + 2).trimmed()));
        return expression + QStringLiteral(" BETWEEN ? AND ?");
    }
    // Text: * and ? are the wildcards people know; SQL's own are escaped.
    QString pattern;
    const bool wild = text.contains(QLatin1Char('*')) || text.contains(QLatin1Char('?'));
    for (const QChar c : text) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\')) {
            pattern += QLatin1Char('\\');
            pattern += c;
        } else if (c == QLatin1Char('*')) {
            pattern += QLatin1Char('%');
        } else if (c == QLatin1Char('?')) {
            pattern += QLatin1Char('_');
        } else {
            pattern += c;
        }
    }
    if (!wild) {
        pattern = QLatin1Char('%') + pattern + QLatin1Char('%');
    }
    binds->append(pattern);
    return expression + QStringLiteral(" LIKE ? ESCAPE '\\'");
}

QString criterionInline(const QString &expression, const QString &typed)
{
    QVariantList binds;
    const QString sql = criterion(expression, typed, &binds);
    QString out;
    int next = 0;
    // The only question marks are the ones criterion() wrote: the expression is
    // an identifier this module quoted, where a `?` sits inside double quotes.
    bool quoted = false;
    for (const QChar c : sql) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        }
        if (c == QLatin1Char('?') && !quoted && next < binds.size()) {
            out += literal(binds.at(next++));
        } else {
            out += c;
        }
    }
    return out;
}

static QString likePattern(const QString &text)
{
    QString pattern;
    for (const QChar c : text) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\')) {
            pattern += QLatin1Char('\\');
        }
        pattern += c;
    }
    return pattern;
}

QString condition(const QString &expression, const QString &op, const QVariant &value, QVariantList *binds)
{
    if (op == QLatin1String("null")) {
        return expression + QStringLiteral(" IS NULL");
    }
    if (op == QLatin1String("notnull")) {
        return expression + QStringLiteral(" IS NOT NULL");
    }
    static const QHash<QString, QString> compare{{QStringLiteral("eq"), QStringLiteral(" = ?")}, {QStringLiteral("gt"), QStringLiteral(" > ?")},
                                                 {QStringLiteral("ge"), QStringLiteral(" >= ?")}, {QStringLiteral("lt"), QStringLiteral(" < ?")},
                                                 {QStringLiteral("le"), QStringLiteral(" <= ?")}};
    if (compare.contains(op)) {
        binds->append(value);
        return expression + compare.value(op);
    }
    if (op == QLatin1String("ne")) {
        // What holds nothing is not equal to anything either: it stays in.
        binds->append(value);
        return QLatin1Char('(') + expression + QStringLiteral(" <> ? OR ") + expression + QStringLiteral(" IS NULL)");
    }
    const QString text = likePattern(value.toString());
    if (op == QLatin1String("contains")) {
        binds->append(QLatin1Char('%') + text + QLatin1Char('%'));
        return expression + QStringLiteral(" LIKE ? ESCAPE '\\'");
    }
    if (op == QLatin1String("excludes")) {
        binds->append(QLatin1Char('%') + text + QLatin1Char('%'));
        return QLatin1Char('(') + expression + QStringLiteral(" NOT LIKE ? ESCAPE '\\' OR ") + expression + QStringLiteral(" IS NULL)");
    }
    if (op == QLatin1String("starts")) {
        binds->append(text + QLatin1Char('%'));
        return expression + QStringLiteral(" LIKE ? ESCAPE '\\'");
    }
    if (op == QLatin1String("ends")) {
        binds->append(QLatin1Char('%') + text);
        return expression + QStringLiteral(" LIKE ? ESCAPE '\\'");
    }
    return {};
}

QString inlined(const QString &sql, const QVariantList &binds)
{
    QString out;
    int next = 0;
    QChar quote;
    for (const QChar c : sql) {
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        }
        if (c == QLatin1Char('?') && quote.isNull() && next < binds.size()) {
            out += literal(binds.at(next++));
        } else {
            out += c;
        }
    }
    return out;
}

QVariantList operators()
{
    static const QList<std::tuple<const char *, const char *, bool>> all{
        {"eq", "es", false},           {"ne", "no es", false},          {"contains", "contiene", false}, {"excludes", "no contiene", false},
        {"starts", "empieza por", false}, {"ends", "termina en", false}, {"gt", "mayor que", false},      {"ge", "mayor o igual que", false},
        {"lt", "menor que", false},    {"le", "menor o igual que", false}, {"null", "está vacío", true},   {"notnull", "no está vacío", true}};
    QVariantList list;
    for (const auto &[id, label, unary] : all) {
        list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(id)}, {QStringLiteral("label"), QString::fromUtf8(label)}, {QStringLiteral("unary"), unary}});
    }
    return list;
}

QVariantMap complete(const QString &text, int cursor, const QVariantMap &schema)
{
    cursor = qBound(0, cursor, int(text.size()));
    int start = cursor;
    while (start > 0 && (text.at(start - 1).isLetterOrNumber() || text.at(start - 1) == QLatin1Char('_'))) {
        --start;
    }
    const QString prefix = text.mid(start, cursor - start);
    QVariantList items;
    // Inside a string or a comment there is nothing to offer.
    for (const Token &token : tokenize(text.left(cursor))) {
        if ((token.type == Token::String || token.type == Token::Comment) && token.start + token.length == cursor) {
            const QString inside = text.mid(token.start, token.length);
            const bool closed = token.type == Token::String && inside.size() >= 2 && inside.endsWith(inside.at(0)) ;
            if (!closed) {
                return {{QStringLiteral("start"), cursor}, {QStringLiteral("prefix"), QString()}, {QStringLiteral("items"), items}};
            }
        }
    }
    const auto add = [&](const QString &name, const QString &what) {
        if (!name.startsWith(prefix, Qt::CaseInsensitive) || name.compare(prefix, Qt::CaseSensitive) == 0) {
            return;
        }
        for (const QVariant &item : std::as_const(items)) {
            if (item.toMap().value(QStringLiteral("text")).toString().compare(name, Qt::CaseInsensitive) == 0) {
                return;
            }
        }
        items.append(QVariantMap{{QStringLiteral("text"), name}, {QStringLiteral("kind"), what}});
    };

    // «tabla.» offers the columns of that table (or of what the alias stands for).
    if (start > 0 && text.at(start - 1) == QLatin1Char('.')) {
        int end = start - 1;
        int begin = end;
        while (begin > 0 && (text.at(begin - 1).isLetterOrNumber() || text.at(begin - 1) == QLatin1Char('_'))) {
            --begin;
        }
        QString owner = text.mid(begin, end - begin);
        if (!schema.contains(owner)) {
            // An alias: «FROM clientes c» or «FROM clientes AS c».
            const QList<Token> tokens = tokenize(text);
            QStringList words;
            for (const Token &token : tokens) {
                if (token.type == Token::Identifier || token.type == Token::Keyword) {
                    words.append(text.mid(token.start, token.length));
                }
            }
            for (int i = 1; i < words.size(); ++i) {
                if (words.at(i).compare(owner, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                int before = i - 1;
                if (words.at(before).compare(QLatin1String("AS"), Qt::CaseInsensitive) == 0 && before > 0) {
                    --before;
                }
                QString table = words.at(before);
                if (table.startsWith(QLatin1Char('"')) && table.endsWith(QLatin1Char('"')) && table.size() >= 2) {
                    table = table.mid(1, table.size() - 2);
                }
                for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
                    if (it.key().compare(table, Qt::CaseInsensitive) == 0) {
                        owner = it.key();
                    }
                }
            }
        }
        for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
            if (it.key().compare(owner, Qt::CaseInsensitive) == 0) {
                for (const QString &column : it.value().toStringList()) {
                    add(column, QStringLiteral("column"));
                }
            }
        }
        return {{QStringLiteral("start"), start}, {QStringLiteral("prefix"), prefix}, {QStringLiteral("items"), items}};
    }

    if (prefix.isEmpty()) {
        return {{QStringLiteral("start"), start}, {QStringLiteral("prefix"), prefix}, {QStringLiteral("items"), items}};
    }
    for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
        add(it.key(), QStringLiteral("table"));
    }
    // Columns of the tables the statement already names come before the rest.
    QStringList named;
    for (const Token &token : tokenize(text)) {
        if (token.type == Token::Identifier) {
            named.append(text.mid(token.start, token.length).toLower());
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (auto it = schema.constBegin(); it != schema.constEnd(); ++it) {
            if (named.contains(it.key().toLower()) != (pass == 0)) {
                continue;
            }
            for (const QString &column : it.value().toStringList()) {
                add(column, QStringLiteral("column"));
            }
        }
    }
    for (const QString &word : keywords()) {
        add(word, QStringLiteral("keyword"));
    }
    for (const QString &function : functionList()) {
        add(function, QStringLiteral("function"));
    }
    if (items.size() > 40) {
        items = items.mid(0, 40);
    }
    return {{QStringLiteral("start"), start}, {QStringLiteral("prefix"), prefix}, {QStringLiteral("items"), items}};
}

QString pretty(const QString &name)
{
    QString text = name;
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text = text.simplified();
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

}
