#include "tomldoc.h"

#include <QSet>

#include <cmath>

namespace {

bool isBareKeyChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-');
}

bool startsWith(const QStringList &path, const QStringList &prefix)
{
    return path.size() >= prefix.size() && path.mid(0, prefix.size()) == prefix;
}

struct Parser {
    const QString &s;
    qsizetype p = 0;
    QString error;

    bool atEnd() const { return p >= s.size(); }
    QChar peek(qsizetype ahead = 0) const { return p + ahead < s.size() ? s.at(p + ahead) : QChar(); }
    bool looksAt(QLatin1StringView token) const { return QStringView(s).mid(p).startsWith(token); }

    bool fail(const QString &what)
    {
        if (error.isEmpty()) {
            const qsizetype line = s.left(p).count(QLatin1Char('\n')) + 1;
            error = QStringLiteral("line %1: %2").arg(line).arg(what);
        }
        return false;
    }

    void skipSpaces()
    {
        while (!atEnd() && (peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t'))) {
            ++p;
        }
    }

    void skipComment()
    {
        while (!atEnd() && peek() != QLatin1Char('\n')) {
            ++p;
        }
    }

    /// Whitespace, newlines and comments: what may sit between the items of an array.
    void skipFiller()
    {
        while (!atEnd()) {
            const QChar c = peek();
            if (c == QLatin1Char('#')) {
                skipComment();
            } else if (c.isSpace()) {
                ++p;
            } else {
                break;
            }
        }
    }

    /// The rest of a line after a header or a value: optional comment, then the newline.
    bool endLine()
    {
        skipSpaces();
        if (peek() == QLatin1Char('#')) {
            skipComment();
        }
        if (peek() == QLatin1Char('\r')) {
            ++p;
        }
        if (atEnd()) {
            return true;
        }
        if (peek() != QLatin1Char('\n')) {
            return fail(QStringLiteral("unexpected text after a value"));
        }
        ++p;
        return true;
    }

    bool escape(QString &out)
    {
        const QChar c = peek();
        ++p;
        switch (c.unicode()) {
        case 'b': out += QLatin1Char('\b'); return true;
        case 't': out += QLatin1Char('\t'); return true;
        case 'n': out += QLatin1Char('\n'); return true;
        case 'f': out += QLatin1Char('\f'); return true;
        case 'r': out += QLatin1Char('\r'); return true;
        case 'e': out += QChar(0x1b); return true;
        case '"': out += QLatin1Char('"'); return true;
        case '\\': out += QLatin1Char('\\'); return true;
        case 'x':
        case 'u':
        case 'U': {
            const int digits = c == QLatin1Char('x') ? 2 : (c == QLatin1Char('u') ? 4 : 8);
            bool ok = false;
            const char32_t code = s.mid(p, digits).toUInt(&ok, 16);
            if (!ok || p + digits > s.size()) {
                return fail(QStringLiteral("bad unicode escape"));
            }
            p += digits;
            out += QString::fromUcs4(&code, 1);
            return true;
        }
        default:
            return fail(QStringLiteral("unknown escape"));
        }
    }

    bool basicString(QString &out)
    {
        const bool multi = looksAt(QLatin1StringView("\"\"\""));
        p += multi ? 3 : 1;
        if (multi && peek() == QLatin1Char('\r')) {
            ++p;
        }
        if (multi && peek() == QLatin1Char('\n')) {
            ++p;
        }
        while (!atEnd()) {
            const QChar c = peek();
            if (multi && looksAt(QLatin1StringView("\"\"\""))) {
                p += 3;
                // Up to two quotes may sit right before the closing delimiter.
                while (peek() == QLatin1Char('"')) {
                    out += QLatin1Char('"');
                    ++p;
                }
                return true;
            }
            if (!multi && c == QLatin1Char('"')) {
                ++p;
                return true;
            }
            if (!multi && c == QLatin1Char('\n')) {
                break;
            }
            ++p;
            if (c != QLatin1Char('\\')) {
                out += c;
                continue;
            }
            if (multi && (peek() == QLatin1Char('\n') || peek() == QLatin1Char('\r') || peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t'))) {
                qsizetype q = p;
                while (q < s.size() && (s.at(q) == QLatin1Char(' ') || s.at(q) == QLatin1Char('\t') || s.at(q) == QLatin1Char('\r'))) {
                    ++q;
                }
                if (q < s.size() && s.at(q) == QLatin1Char('\n')) {
                    p = q;
                    while (!atEnd() && peek().isSpace()) {
                        ++p;
                    }
                    continue;
                }
            }
            if (!escape(out)) {
                return false;
            }
        }
        return fail(QStringLiteral("unterminated string"));
    }

    bool literalString(QString &out)
    {
        const bool multi = looksAt(QLatin1StringView("'''"));
        p += multi ? 3 : 1;
        if (multi && peek() == QLatin1Char('\r')) {
            ++p;
        }
        if (multi && peek() == QLatin1Char('\n')) {
            ++p;
        }
        while (!atEnd()) {
            if (multi && looksAt(QLatin1StringView("'''"))) {
                p += 3;
                while (peek() == QLatin1Char('\'')) {
                    out += QLatin1Char('\'');
                    ++p;
                }
                return true;
            }
            const QChar c = peek();
            if (!multi && c == QLatin1Char('\'')) {
                ++p;
                return true;
            }
            if (!multi && c == QLatin1Char('\n')) {
                break;
            }
            out += c;
            ++p;
        }
        return fail(QStringLiteral("unterminated string"));
    }

    bool keyPart(QString &out)
    {
        out.clear();
        if (peek() == QLatin1Char('"')) {
            return basicString(out);
        }
        if (peek() == QLatin1Char('\'')) {
            return literalString(out);
        }
        while (!atEnd() && isBareKeyChar(peek())) {
            out += peek();
            ++p;
        }
        return out.isEmpty() ? fail(QStringLiteral("expected a key")) : true;
    }

    bool keyPath(QStringList &out)
    {
        while (true) {
            skipSpaces();
            QString part;
            if (!keyPart(part)) {
                return false;
            }
            out << part;
            skipSpaces();
            if (peek() != QLatin1Char('.')) {
                return true;
            }
            ++p;
        }
    }

    bool array(QVariant &out)
    {
        ++p;
        QVariantList list;
        while (true) {
            skipFiller();
            if (peek() == QLatin1Char(']')) {
                ++p;
                break;
            }
            QVariant item;
            if (!value(item)) {
                return false;
            }
            list << item;
            skipFiller();
            if (peek() == QLatin1Char(',')) {
                ++p;
            } else if (peek() != QLatin1Char(']')) {
                return fail(QStringLiteral("expected , or ] in an array"));
            }
        }
        out = list;
        return true;
    }

    bool inlineTable(QVariant &out)
    {
        ++p;
        QVariantMap map;
        while (true) {
            skipFiller();
            if (peek() == QLatin1Char('}')) {
                ++p;
                break;
            }
            QStringList path;
            if (!keyPath(path)) {
                return false;
            }
            if (peek() != QLatin1Char('=')) {
                return fail(QStringLiteral("expected = in an inline table"));
            }
            ++p;
            skipSpaces();
            QVariant item;
            if (!value(item)) {
                return false;
            }
            insertNested(map, path, item);
            skipFiller();
            if (peek() == QLatin1Char(',')) {
                ++p;
            } else if (peek() != QLatin1Char('}')) {
                return fail(QStringLiteral("expected , or } in an inline table"));
            }
        }
        out = map;
        return true;
    }

    static void insertNested(QVariantMap &map, const QStringList &path, const QVariant &item)
    {
        if (path.size() == 1) {
            map.insert(path.first(), item);
            return;
        }
        QVariantMap child = map.value(path.first()).toMap();
        insertNested(child, path.mid(1), item);
        map.insert(path.first(), child);
    }

    bool scalar(QVariant &out)
    {
        const qsizetype start = p;
        while (!atEnd()) {
            const QChar c = peek();
            if (c == QLatin1Char(',') || c == QLatin1Char(']') || c == QLatin1Char('}') || c == QLatin1Char('#') || c.isSpace()) {
                break;
            }
            ++p;
        }
        const QString token = s.mid(start, p - start);
        if (token.isEmpty()) {
            return fail(QStringLiteral("expected a value"));
        }
        if (token == QLatin1String("true") || token == QLatin1String("false")) {
            out = token == QLatin1String("true");
            return true;
        }
        QString digits = token;
        digits.remove(QLatin1Char('_'));
        bool ok = false;
        qlonglong integer = 0;
        if (digits.startsWith(QLatin1String("0x"))) {
            integer = digits.mid(2).toLongLong(&ok, 16);
        } else if (digits.startsWith(QLatin1String("0o"))) {
            integer = digits.mid(2).toLongLong(&ok, 8);
        } else if (digits.startsWith(QLatin1String("0b"))) {
            integer = digits.mid(2).toLongLong(&ok, 2);
        } else {
            integer = digits.toLongLong(&ok, 10);
        }
        if (ok) {
            out = integer;
            return true;
        }
        const double real = digits.toDouble(&ok);
        if (ok) {
            out = real;
            return true;
        }
        // Dates and times: kept as written.
        out = token;
        return true;
    }

    bool value(QVariant &out)
    {
        const QChar c = peek();
        if (c == QLatin1Char('"')) {
            QString text;
            if (!basicString(text)) {
                return false;
            }
            out = text;
            return true;
        }
        if (c == QLatin1Char('\'')) {
            QString text;
            if (!literalString(text)) {
                return false;
            }
            out = text;
            return true;
        }
        if (c == QLatin1Char('[')) {
            return array(out);
        }
        if (c == QLatin1Char('{')) {
            return inlineTable(out);
        }
        return scalar(out);
    }
};

QString encodeString(const QString &text)
{
    QString out = QStringLiteral("\"");
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '"': out += QLatin1String("\\\""); break;
        case '\\': out += QLatin1String("\\\\"); break;
        case '\n': out += QLatin1String("\\n"); break;
        case '\t': out += QLatin1String("\\t"); break;
        case '\r': out += QLatin1String("\\r"); break;
        default:
            if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
                out += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QLatin1Char('0'));
            } else {
                out += c;
            }
        }
    }
    return out + QLatin1Char('"');
}

QString encodeFloat(double number)
{
    if (std::isnan(number)) {
        return QStringLiteral("nan");
    }
    if (std::isinf(number)) {
        return number > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    }
    QString out = QString::number(number, 'g', 12);
    if (!out.contains(QLatin1Char('.')) && !out.contains(QLatin1Char('e'))) {
        out += QLatin1String(".0");
    }
    return out;
}

QVariant removeNested(QVariantMap map, const QStringList &path)
{
    if (path.size() == 1) {
        map.remove(path.first());
        return map;
    }
    if (map.contains(path.first())) {
        map.insert(path.first(), removeNested(map.value(path.first()).toMap(), path.mid(1)));
    }
    return map;
}

} // namespace

bool TomlDoc::parse(const QString &text, QString *error)
{
    Parser parser{text};
    QList<Entry> entries;
    QList<Table> tables;
    QStringList current;
    QList<QStringList> arrayPaths;
    bool inArray = false;

    while (!parser.atEnd()) {
        const qsizetype lineStart = parser.p;
        parser.skipSpaces();
        if (parser.atEnd()) {
            break;
        }
        const QChar c = parser.peek();
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            ++parser.p;
            continue;
        }
        if (c == QLatin1Char('#')) {
            parser.skipComment();
            continue;
        }
        if (c == QLatin1Char('[')) {
            const bool array = parser.peek(1) == QLatin1Char('[');
            parser.p += array ? 2 : 1;
            QStringList path;
            if (!parser.keyPath(path)) {
                break;
            }
            if (parser.peek() != QLatin1Char(']') || (array && parser.peek(1) != QLatin1Char(']'))) {
                parser.fail(QStringLiteral("unterminated table header"));
                break;
            }
            parser.p += array ? 2 : 1;
            if (!parser.endLine()) {
                break;
            }
            if (array && !arrayPaths.contains(path)) {
                arrayPaths << path;
            }
            inArray = false;
            for (const QStringList &arrayPath : std::as_const(arrayPaths)) {
                inArray = inArray || startsWith(path, arrayPath);
            }
            current = path;
            tables << Table{path, inArray, lineStart, parser.p};
            continue;
        }
        Entry entry;
        QStringList key;
        if (!parser.keyPath(key)) {
            break;
        }
        if (parser.peek() != QLatin1Char('=')) {
            parser.fail(QStringLiteral("expected = after a key"));
            break;
        }
        ++parser.p;
        parser.skipSpaces();
        entry.valueStart = parser.p;
        if (!parser.value(entry.value)) {
            break;
        }
        entry.valueEnd = parser.p;
        if (!parser.endLine()) {
            break;
        }
        entry.path = current + key;
        entry.inArray = inArray;
        entry.lineStart = lineStart;
        entry.lineEnd = parser.p;
        entries << entry;
    }

    if (!parser.error.isEmpty()) {
        if (error) {
            *error = parser.error;
        }
        return false;
    }
    m_text = text;
    m_entries = entries;
    m_tables = tables;
    return true;
}

void TomlDoc::reparse()
{
    const QString text = m_text;
    parse(text);
}

const TomlDoc::Entry *TomlDoc::find(const QStringList &path) const
{
    for (const Entry &entry : m_entries) {
        if (!entry.inArray && entry.path == path) {
            return &entry;
        }
    }
    return nullptr;
}

const TomlDoc::Entry *TomlDoc::findPrefix(const QStringList &path) const
{
    for (const Entry &entry : m_entries) {
        if (!entry.inArray && entry.path.size() < path.size() && startsWith(path, entry.path) && entry.value.typeId() == QMetaType::QVariantMap) {
            return &entry;
        }
    }
    return nullptr;
}

bool TomlDoc::contains(const QStringList &path) const
{
    return value(path).isValid();
}

QVariant TomlDoc::value(const QStringList &path) const
{
    if (const Entry *entry = find(path)) {
        return entry->value;
    }
    if (const Entry *entry = findPrefix(path)) {
        QVariant inner = entry->value;
        for (const QString &part : path.mid(entry->path.size())) {
            const QVariantMap map = inner.toMap();
            if (!map.contains(part)) {
                return {};
            }
            inner = map.value(part);
        }
        return inner;
    }
    return {};
}

QStringList TomlDoc::keys(const QStringList &table) const
{
    QStringList out;
    for (const Entry &entry : m_entries) {
        if (entry.inArray) {
            continue;
        }
        if (entry.path.size() == table.size() + 1 && startsWith(entry.path, table)) {
            out << entry.path.last();
        } else if (entry.path == table && entry.value.typeId() == QMetaType::QVariantMap) {
            out << entry.value.toMap().keys();
        }
    }
    return out;
}

qsizetype TomlDoc::insertionPoint(const QStringList &table, bool *exists) const
{
    *exists = true;
    const Table *header = nullptr;
    if (!table.isEmpty()) {
        for (const Table &candidate : m_tables) {
            if (!candidate.array && candidate.path == table) {
                header = &candidate;
                break;
            }
        }
        if (!header) {
            *exists = false;
            return m_text.size();
        }
    }
    const qsizetype bodyStart = header ? header->bodyStart : 0;
    qsizetype bodyEnd = m_text.size();
    for (const Table &next : m_tables) {
        if (next.headerStart >= bodyStart) {
            bodyEnd = next.headerStart;
            break;
        }
    }
    qsizetype last = -1;
    for (const Entry &entry : m_entries) {
        if (entry.lineStart >= bodyStart && entry.lineStart < bodyEnd) {
            last = entry.lineEnd;
        }
    }
    if (last >= 0) {
        return last;
    }
    if (!header) {
        // No top-level key yet: go above the first header and the comment block that introduces it.
        if (m_tables.isEmpty()) {
            return m_text.size();
        }
        qsizetype point = bodyEnd;
        while (point > 0) {
            const qsizetype previous = point < 2 ? 0 : m_text.lastIndexOf(QLatin1Char('\n'), point - 2) + 1;
            if (!QStringView(m_text).mid(previous, point - previous).trimmed().startsWith(QLatin1Char('#'))) {
                break;
            }
            point = previous;
        }
        return point;
    }
    // Below the comment block that opens the table.
    qsizetype point = bodyStart;
    while (point < bodyEnd) {
        qsizetype next = m_text.indexOf(QLatin1Char('\n'), point);
        next = next < 0 ? m_text.size() : next + 1;
        if (!QStringView(m_text).mid(point, next - point).trimmed().startsWith(QLatin1Char('#'))) {
            break;
        }
        point = next;
    }
    return point;
}

void TomlDoc::set(const QStringList &path, const QVariant &value, Kind kind)
{
    if (path.isEmpty()) {
        return;
    }
    const QString encoded = encode(value, kind);
    if (const Entry *entry = find(path)) {
        m_text.replace(entry->valueStart, entry->valueEnd - entry->valueStart, encoded);
        reparse();
        return;
    }
    if (const Entry *entry = findPrefix(path)) {
        QVariantMap map = entry->value.toMap();
        QVariant typed = value;
        if (kind == Kind::Int) {
            typed = value.toLongLong();
        } else if (kind == Kind::Float) {
            typed = value.toDouble();
        }
        Parser::insertNested(map, path.mid(entry->path.size()), typed);
        m_text.replace(entry->valueStart, entry->valueEnd - entry->valueStart, encode(map));
        reparse();
        return;
    }

    const QStringList table = path.mid(0, path.size() - 1);
    // A table spelled with dotted keys under a parent header (`keyboard.layout = …`) has no
    // header of its own to add to: follow the same spelling beside its siblings.
    qsizetype dottedDepth = -1;
    qsizetype dottedAfter = -1;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.inArray || !startsWith(entry.path, table) || entry.path.size() <= table.size()) {
            continue;
        }
        qsizetype depth = 0;
        for (const Table &header : std::as_const(m_tables)) {
            if (header.headerStart < entry.lineStart) {
                depth = header.path.size();
            }
        }
        if (depth < table.size()) {
            dottedDepth = depth;
            dottedAfter = entry.lineEnd;
        }
    }

    QString line;
    qsizetype point = 0;
    bool exists = true;
    if (dottedDepth >= 0) {
        QStringList parts;
        for (const QString &part : path.mid(dottedDepth)) {
            parts << encodeKey(part);
        }
        line = parts.join(QLatin1Char('.')) + QLatin1String(" = ") + encoded + QLatin1Char('\n');
        point = dottedAfter;
    } else {
        line = encodeKey(path.last()) + QLatin1String(" = ") + encoded + QLatin1Char('\n');
        point = insertionPoint(table, &exists);
    }

    if (!exists) {
        QStringList parts;
        for (const QString &part : table) {
            parts << encodeKey(part);
        }
        QString block;
        if (!m_text.isEmpty() && !m_text.endsWith(QLatin1Char('\n'))) {
            block += QLatin1Char('\n');
        }
        if (!m_text.isEmpty()) {
            block += QLatin1Char('\n');
        }
        block += QLatin1Char('[') + parts.join(QLatin1Char('.')) + QLatin1String("]\n") + line;
        m_text += block;
    } else {
        if (point > 0 && m_text.at(point - 1) != QLatin1Char('\n')) {
            line.prepend(QLatin1Char('\n'));
        }
        const bool rootAboveHeader = table.isEmpty() && dottedDepth < 0 && point < m_text.size() && keys({}).isEmpty();
        if (rootAboveHeader) {
            line += QLatin1Char('\n');
        }
        m_text.insert(point, line);
    }
    reparse();
}

void TomlDoc::remove(const QStringList &path)
{
    if (const Entry *entry = find(path)) {
        m_text.remove(entry->lineStart, entry->lineEnd - entry->lineStart);
        reparse();
        return;
    }
    if (const Entry *entry = findPrefix(path)) {
        const QVariant map = removeNested(entry->value.toMap(), path.mid(entry->path.size()));
        m_text.replace(entry->valueStart, entry->valueEnd - entry->valueStart, encode(map));
        reparse();
    }
}

QString TomlDoc::encodeKey(const QString &key)
{
    bool bare = !key.isEmpty();
    for (const QChar c : key) {
        // TOML's bare keys are ASCII only, whatever QChar counts as a letter.
        bare = bare && c.unicode() < 0x80 && isBareKeyChar(c);
    }
    return bare ? key : encodeString(key);
}

QString TomlDoc::encode(const QVariant &value, Kind kind)
{
    switch (kind) {
    case Kind::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case Kind::Int:
        return QString::number(qRound64(value.toDouble()));
    case Kind::Float:
        return encodeFloat(value.toDouble());
    case Kind::String:
        return encodeString(value.toString());
    case Kind::Auto:
        break;
    }
    switch (value.typeId()) {
    case QMetaType::Bool:
        return encode(value, Kind::Bool);
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return QString::number(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return encodeFloat(value.toDouble());
    case QMetaType::QVariantMap: {
        QStringList items;
        const QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            items << encodeKey(it.key()) + QLatin1String(" = ") + encode(it.value());
        }
        return items.isEmpty() ? QStringLiteral("{}") : QLatin1String("{ ") + items.join(QLatin1String(", ")) + QLatin1String(" }");
    }
    case QMetaType::QStringList:
    case QMetaType::QVariantList: {
        QStringList items;
        bool allStrings = true;
        qsizetype length = 0;
        const QVariantList list = value.toList();
        for (const QVariant &item : list) {
            allStrings = allStrings && item.typeId() == QMetaType::QString;
            items << encode(item);
            length += items.last().size() + 2;
        }
        if (allStrings && items.size() > 1 && length > 60) {
            return QLatin1String("[\n    ") + items.join(QLatin1String(",\n    ")) + QLatin1String(",\n]");
        }
        return QLatin1Char('[') + items.join(QLatin1String(", ")) + QLatin1Char(']');
    }
    default:
        return encodeString(value.toString());
    }
}
