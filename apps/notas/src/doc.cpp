#include "doc.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

static const QString wikiScheme = QStringLiteral("notas:page/");

bool Run::sameLook(const Run &other) const
{
    return !isImage() && !other.isImage() && bold == other.bold && italic == other.italic && underline == other.underline
        && strike == other.strike && mono == other.mono && color == other.color && highlight == other.highlight && href == other.href;
}

static const char *const typeNames[] = {"p", "h1", "h2", "h3", "bullet", "number", "task", "quote", "code", "table"};

QString Block::typeName(Type type)
{
    return QLatin1String(typeNames[type]);
}

Block::Type Block::typeFromName(const QString &name)
{
    for (int i = 0; i <= Table; ++i) {
        if (name == QLatin1String(typeNames[i])) {
            return Type(i);
        }
    }
    return Paragraph;
}

QString Doc::wikiHref(const QString &title)
{
    return wikiScheme + QString::fromLatin1(QUrl::toPercentEncoding(title.trimmed()));
}

QString Doc::wikiTitle(const QString &href)
{
    return href.startsWith(wikiScheme) ? QUrl::fromPercentEncoding(href.mid(wikiScheme.size()).toLatin1()) : QString();
}

bool Doc::isEmpty() const
{
    for (const Block &block : blocks) {
        if (block.type == Block::Table) {
            return false;
        }
        for (const Run &run : block.runs) {
            if (run.isImage() || !run.text.trimmed().isEmpty()) {
                return false;
            }
        }
    }
    return true;
}

// --- JSON -------------------------------------------------------------------------

static QJsonArray runsToJson(const Runs &runs)
{
    QJsonArray array;
    for (const Run &run : runs) {
        QJsonObject o;
        if (run.isImage()) {
            o.insert(QLatin1String("img"), run.image);
            o.insert(QLatin1String("w"), run.width);
            o.insert(QLatin1String("h"), run.height);
        } else {
            o.insert(QLatin1String("s"), run.text);
            if (run.bold) o.insert(QLatin1String("b"), true);
            if (run.italic) o.insert(QLatin1String("i"), true);
            if (run.underline) o.insert(QLatin1String("u"), true);
            if (run.strike) o.insert(QLatin1String("x"), true);
            if (run.mono) o.insert(QLatin1String("m"), true);
            if (!run.color.isEmpty()) o.insert(QLatin1String("c"), run.color);
            if (!run.highlight.isEmpty()) o.insert(QLatin1String("h"), run.highlight);
            if (!run.href.isEmpty()) o.insert(QLatin1String("a"), run.href);
        }
        array << o;
    }
    return array;
}

static Runs runsFromJson(const QJsonArray &array)
{
    Runs runs;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Run run;
        if (o.contains(QLatin1String("img"))) {
            run.image = o.value(QLatin1String("img")).toString();
            run.width = o.value(QLatin1String("w")).toInt();
            run.height = o.value(QLatin1String("h")).toInt();
            if (run.image.isEmpty()) {
                continue;
            }
        } else {
            run.text = o.value(QLatin1String("s")).toString();
            run.bold = o.value(QLatin1String("b")).toBool();
            run.italic = o.value(QLatin1String("i")).toBool();
            run.underline = o.value(QLatin1String("u")).toBool();
            run.strike = o.value(QLatin1String("x")).toBool();
            run.mono = o.value(QLatin1String("m")).toBool();
            run.color = o.value(QLatin1String("c")).toString();
            run.highlight = o.value(QLatin1String("h")).toString();
            run.href = o.value(QLatin1String("a")).toString();
        }
        runs << run;
    }
    return runs;
}

QJsonObject Doc::toJson() const
{
    QJsonArray array;
    for (const Block &block : blocks) {
        QJsonObject o;
        o.insert(QLatin1String("t"), Block::typeName(block.type));
        if (block.done) o.insert(QLatin1String("done"), true);
        if (block.indent > 0) o.insert(QLatin1String("indent"), block.indent);
        if (block.type == Block::Table) {
            QJsonArray rows;
            for (const QList<Runs> &row : block.rows) {
                QJsonArray cells;
                for (const Runs &cell : row) {
                    cells << runsToJson(cell);
                }
                rows << cells;
            }
            o.insert(QLatin1String("rows"), rows);
        } else {
            o.insert(QLatin1String("runs"), runsToJson(block.runs));
        }
        array << o;
    }
    return {{QLatin1String("v"), 1}, {QLatin1String("blocks"), array}};
}

Doc Doc::fromJson(const QJsonObject &json)
{
    Doc doc;
    const QJsonArray array = json.value(QLatin1String("blocks")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Block block;
        block.type = Block::typeFromName(o.value(QLatin1String("t")).toString());
        block.done = o.value(QLatin1String("done")).toBool();
        block.indent = qBound(0, o.value(QLatin1String("indent")).toInt(), 8);
        if (block.type == Block::Table) {
            const QJsonArray rows = o.value(QLatin1String("rows")).toArray();
            int columns = 0;
            for (const QJsonValue &row : rows) {
                QList<Runs> cells;
                const QJsonArray array = row.toArray();
                for (const QJsonValue &cell : array) {
                    cells << runsFromJson(cell.toArray());
                }
                columns = qMax(columns, int(cells.size()));
                block.rows << cells;
            }
            if (columns == 0) {
                continue;
            }
            // Every row as wide as the widest.
            for (QList<Runs> &row : block.rows) {
                while (row.size() < columns) {
                    row << Runs();
                }
            }
        } else {
            block.runs = runsFromJson(o.value(QLatin1String("runs")).toArray());
        }
        doc.blocks << block;
    }
    return doc;
}

// --- Text -------------------------------------------------------------------------

static QString runsText(const Runs &runs)
{
    QString text;
    for (const Run &run : runs) {
        text += run.text;
    }
    return text;
}

QString Doc::toText() const
{
    QStringList lines;
    QHash<int, int> numbers;
    for (const Block &block : blocks) {
        if (block.type != Block::Number) {
            if (!block.isList()) {
                numbers.clear();
            }
        }
        const QString pad(block.indent * 2, QLatin1Char(' '));
        switch (block.type) {
        case Block::Table:
            for (const QList<Runs> &row : block.rows) {
                QStringList cells;
                for (const Runs &cell : row) {
                    cells << runsText(cell).replace(QLatin1Char('\n'), QLatin1Char(' '));
                }
                lines << cells.join(QLatin1Char('\t'));
            }
            break;
        case Block::Bullet:
            lines << pad + QStringLiteral("• ") + runsText(block.runs);
            break;
        case Block::Number:
            lines << pad + QString::number(++numbers[block.indent]) + QStringLiteral(". ") + runsText(block.runs);
            break;
        case Block::Task:
            lines << pad + (block.done ? QStringLiteral("[x] ") : QStringLiteral("[ ] ")) + runsText(block.runs);
            break;
        default:
            lines << runsText(block.runs);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

Doc Doc::fromText(const QString &text)
{
    Doc doc;
    QString clean = text;
    clean.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = clean.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        Block block;
        if (!line.isEmpty()) {
            block.runs << Run{.text = line};
        }
        doc.blocks << block;
    }
    return doc;
}

// --- Markdown ---------------------------------------------------------------------

static QString escapeMarkdown(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '\\': case '*': case '_': case '`': case '[': case ']': case '<': case '|': case '~':
            out += QLatin1Char('\\');
            out += c;
            break;
        case '\n':
            out += QStringLiteral("  \n");
            break;
        default:
            out += c;
        }
    }
    return out;
}

static QString inlineMarkdown(const Runs &runs, bool cell = false)
{
    QString out;
    for (const Run &run : runs) {
        if (run.isImage()) {
            // Never an empty description: Qt's Markdown reader drops such pictures.
            out += QStringLiteral("![imagen](") + QString(run.image).replace(QLatin1Char(' '), QLatin1String("%20")) + QLatin1Char(')');
            continue;
        }
        if (run.text.isEmpty()) {
            continue;
        }
        const QString wiki = Doc::wikiTitle(run.href);
        if (!wiki.isEmpty()) {
            out += QStringLiteral("[[") + wiki + QStringLiteral("]]");
            continue;
        }
        // The marks hug the words: «**bold** », never «**bold **».
        int head = 0;
        while (head < run.text.size() && run.text.at(head).isSpace()) {
            ++head;
        }
        int tail = run.text.size();
        while (tail > head && run.text.at(tail - 1).isSpace()) {
            --tail;
        }
        const QString before = run.text.left(head);
        const QString after = run.text.mid(tail);
        QString core = run.text.mid(head, tail - head);
        if (core.isEmpty()) {
            out += cell ? QString(run.text).replace(QLatin1Char('\n'), QLatin1String("<br>")) : escapeMarkdown(run.text);
            continue;
        }
        if (run.mono) {
            core = QLatin1Char('`') + core + QLatin1Char('`');
        } else {
            core = escapeMarkdown(core);
        }
        if (run.strike) core = QStringLiteral("~~") + core + QStringLiteral("~~");
        if (run.italic) core = QLatin1Char('*') + core + QLatin1Char('*');
        if (run.bold) core = QStringLiteral("**") + core + QStringLiteral("**");
        if (run.underline && run.href.isEmpty()) core = QStringLiteral("<u>") + core + QStringLiteral("</u>");
        if (!run.href.isEmpty()) {
            core = QLatin1Char('[') + core + QStringLiteral("](") + QString(run.href).replace(QLatin1Char(' '), QLatin1String("%20")) + QLatin1Char(')');
        }
        QString piece = escapeMarkdown(before) + core + escapeMarkdown(after);
        if (cell) {
            piece.replace(QLatin1String("  \n"), QLatin1String("<br>"));
        }
        out += piece;
    }
    return out;
}

QString Doc::toMarkdown(int shift) const
{
    QString out;
    const QString deeper(qBound(0, shift, 3), QLatin1Char('#'));
    QHash<int, int> numbers;
    auto family = [](const Block &block) {
        return block.isList() ? 1 : block.type == Block::Quote ? 2 : block.type == Block::Code ? 3 : 0;
    };
    for (int i = 0; i < blocks.size(); ++i) {
        const Block &block = blocks.at(i);
        const int kin = family(block);
        const bool joined = i > 0 && kin != 0 && family(blocks.at(i - 1)) == kin;
        const bool last = i + 1 == blocks.size() || family(blocks.at(i + 1)) != kin || kin == 0;
        if (i > 0) {
            out += joined ? QStringLiteral("\n") : QStringLiteral("\n\n");
        }
        if (!block.isList()) {
            numbers.clear();
        }
        const QString pad(block.indent * 4, QLatin1Char(' '));
        switch (block.type) {
        case Block::Heading1:
            out += deeper + QStringLiteral("# ") + inlineMarkdown(block.runs);
            break;
        case Block::Heading2:
            out += deeper + QStringLiteral("## ") + inlineMarkdown(block.runs);
            break;
        case Block::Heading3:
            out += deeper + QStringLiteral("### ") + inlineMarkdown(block.runs);
            break;
        case Block::Bullet:
            out += pad + QStringLiteral("- ") + inlineMarkdown(block.runs);
            break;
        case Block::Task:
            out += pad + (block.done ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ")) + inlineMarkdown(block.runs);
            break;
        case Block::Number:
            // A deeper level starts again each time the list comes back up.
            for (auto it = numbers.begin(); it != numbers.end();) {
                it = it.key() > block.indent ? numbers.erase(it) : std::next(it);
            }
            out += pad + QString::number(++numbers[block.indent]) + QStringLiteral(". ") + inlineMarkdown(block.runs);
            break;
        case Block::Quote:
            out += QStringLiteral("> ") + inlineMarkdown(block.runs);
            if (!last) {
                out += QStringLiteral("\n>");
            }
            break;
        case Block::Code:
            if (!joined) {
                out += QStringLiteral("```\n");
            }
            out += runsText(block.runs);
            if (last) {
                out += QStringLiteral("\n```");
            }
            break;
        case Block::Table: {
            for (int row = 0; row < block.rows.size(); ++row) {
                QStringList cells;
                for (const Runs &cell : block.rows.at(row)) {
                    const QString text = inlineMarkdown(cell, true);
                    cells << (text.isEmpty() ? QStringLiteral(" ") : text);
                }
                out += QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |");
                if (row == 0) {
                    QStringList rule;
                    for (int c = 0; c < cells.size(); ++c) {
                        rule << QStringLiteral("---");
                    }
                    out += QStringLiteral("\n| ") + rule.join(QStringLiteral(" | ")) + QStringLiteral(" |");
                }
                if (row + 1 < block.rows.size()) {
                    out += QLatin1Char('\n');
                }
            }
            break;
        }
        default: {
            QString text = inlineMarkdown(block.runs);
            // A paragraph that would read as a heading, a list or a quote.
            static const QRegularExpression leading(QStringLiteral("^(#{1,6} |[-+>] |\\d+[.)] )"));
            if (leading.match(text).hasMatch()) {
                text.prepend(QLatin1Char('\\'));
            }
            out += text;
        }
        }
    }
    return out + QLatin1Char('\n');
}

// --- HTML -------------------------------------------------------------------------

static QString inlineHtml(const Runs &runs)
{
    QString out;
    for (const Run &run : runs) {
        if (run.isImage()) {
            out += QStringLiteral("<img src=\"%1\"").arg(run.image.toHtmlEscaped());
            if (run.width > 0 && run.height > 0) {
                out += QStringLiteral(" width=\"%1\" height=\"%2\"").arg(run.width).arg(run.height);
            }
            out += QStringLiteral(" alt=\"\">");
            continue;
        }
        QString text = run.text.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br>"));
        if (run.mono) text = QStringLiteral("<code>") + text + QStringLiteral("</code>");
        if (run.strike) text = QStringLiteral("<s>") + text + QStringLiteral("</s>");
        if (run.underline && run.href.isEmpty()) text = QStringLiteral("<u>") + text + QStringLiteral("</u>");
        if (run.italic) text = QStringLiteral("<em>") + text + QStringLiteral("</em>");
        if (run.bold) text = QStringLiteral("<strong>") + text + QStringLiteral("</strong>");
        if (!run.color.isEmpty() || !run.highlight.isEmpty()) {
            QStringList style;
            if (!run.color.isEmpty()) style << QStringLiteral("color:") + run.color;
            if (!run.highlight.isEmpty()) style << QStringLiteral("background-color:") + run.highlight;
            text = QStringLiteral("<span style=\"%1\">").arg(style.join(QLatin1Char(';'))) + text + QStringLiteral("</span>");
        }
        if (!run.href.isEmpty()) {
            text = QStringLiteral("<a href=\"%1\">").arg(run.href.toHtmlEscaped()) + text + QStringLiteral("</a>");
        }
        out += text;
    }
    return out;
}

QString Doc::toHtmlBody() const
{
    QString out;
    // The lists that are open, outermost first.
    QStringList open;
    auto closeLists = [&](int depth) {
        while (open.size() > depth) {
            out += QStringLiteral("</li>\n</") + open.takeLast() + QStringLiteral(">\n");
        }
    };
    for (int i = 0; i < blocks.size(); ++i) {
        const Block &block = blocks.at(i);
        if (block.isList()) {
            const QString tag = block.type == Block::Number ? QStringLiteral("ol") : QStringLiteral("ul");
            const int depth = block.indent + 1;
            closeLists(depth);
            if (open.size() == depth && open.last() != tag) {
                closeLists(depth - 1);
            }
            if (open.size() == depth) {
                out += QStringLiteral("</li>\n");
            }
            while (open.size() < depth) {
                out += QLatin1Char('<') + tag + QStringLiteral(">\n");
                open << tag;
                if (open.size() < depth) {
                    out += QStringLiteral("<li>");
                }
            }
            out += block.type == Block::Task ? QStringLiteral("<li class=\"%1\">").arg(block.done ? QLatin1String("checked") : QLatin1String("unchecked"))
                                             : QStringLiteral("<li>");
            out += inlineHtml(block.runs);
            continue;
        }
        closeLists(0);
        const bool joined = i > 0 && blocks.at(i - 1).type == block.type;
        const bool last = i + 1 == blocks.size() || blocks.at(i + 1).type != block.type;
        switch (block.type) {
        case Block::Heading1:
        case Block::Heading2:
        case Block::Heading3: {
            // The title of the page is the <h1>.
            const int level = block.type - Block::Heading1 + 2;
            out += QStringLiteral("<h%1>%2</h%1>\n").arg(level).arg(inlineHtml(block.runs));
            break;
        }
        case Block::Quote:
            if (!joined) out += QStringLiteral("<blockquote>\n");
            out += QStringLiteral("<p>") + inlineHtml(block.runs) + QStringLiteral("</p>\n");
            if (last) out += QStringLiteral("</blockquote>\n");
            break;
        case Block::Code:
            if (!joined) out += QStringLiteral("<pre><code>");
            out += runsText(block.runs).toHtmlEscaped();
            out += last ? QStringLiteral("</code></pre>\n") : QStringLiteral("\n");
            break;
        case Block::Table:
            out += QStringLiteral("<table>\n");
            for (const QList<Runs> &row : block.rows) {
                out += QStringLiteral("<tr>");
                for (const Runs &cell : row) {
                    out += QStringLiteral("<td>") + inlineHtml(cell) + QStringLiteral("</td>");
                }
                out += QStringLiteral("</tr>\n");
            }
            out += QStringLiteral("</table>\n");
            break;
        default:
            out += block.runs.isEmpty() ? QStringLiteral("<p><br></p>\n") : QStringLiteral("<p>") + inlineHtml(block.runs) + QStringLiteral("</p>\n");
        }
    }
    closeLists(0);
    return out;
}

QString Doc::toHtml(const QString &title) const
{
    static const QString style = QStringLiteral(
        "body{font-family:system-ui,-apple-system,\"Segoe UI\",sans-serif;font-size:16px;line-height:1.5;color:#1d1d1f;"
        "max-width:46em;margin:2em auto;padding:0 1.2em}"
        "h1{font-size:1.9em}h2{font-size:1.5em}h3{font-size:1.2em}"
        "a{color:#8e44d6}"
        "pre{background:#f2f2f4;padding:.8em 1em;border-radius:8px;overflow:auto}"
        "code{font-family:ui-monospace,monospace;background:#f2f2f4;border-radius:4px;padding:0 .2em}pre code{padding:0}"
        "blockquote{margin:1em 0;padding:.1em 1em;border-left:4px solid #c9a6ee;color:#555}"
        "table{border-collapse:collapse}td{border:1px solid #c8c8cc;padding:.3em .6em;vertical-align:top}"
        "li.checked{list-style-type:\"\\2611  \"}li.unchecked{list-style-type:\"\\2610  \"}"
        "img{max-width:100%;height:auto}");
    return QStringLiteral("<!DOCTYPE html>\n<html lang=\"es\">\n<head>\n<meta charset=\"utf-8\">\n<title>%1</title>\n<style>%2</style>\n</head>\n<body>\n<h1>%1</h1>\n%3</body>\n</html>\n")
        .arg(title.toHtmlEscaped(), style, toHtmlBody());
}

// --- Links and files ----------------------------------------------------------------

static void linkWikiRuns(Runs &runs)
{
    static const QRegularExpression wiki(QStringLiteral("\\[\\[([^\\[\\]\\n]+)\\]\\]"));
    Runs out;
    for (const Run &run : std::as_const(runs)) {
        if (run.isImage() || run.mono || !run.href.isEmpty() || !run.text.contains(QLatin1String("[["))) {
            out << run;
            continue;
        }
        int at = 0;
        auto it = wiki.globalMatch(run.text);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (match.capturedStart() > at) {
                Run before = run;
                before.text = run.text.mid(at, match.capturedStart() - at);
                out << before;
            }
            Run link = run;
            link.text = match.captured(1).trimmed();
            link.href = Doc::wikiHref(link.text);
            out << link;
            at = match.capturedEnd();
        }
        if (at < run.text.size()) {
            Run after = run;
            after.text = run.text.mid(at);
            out << after;
        }
    }
    runs = out;
}

template<typename Visit>
static void eachRuns(QList<Block> &blocks, Visit visit)
{
    for (Block &block : blocks) {
        if (block.type == Block::Code) {
            continue;
        }
        visit(block.runs);
        for (QList<Runs> &row : block.rows) {
            for (Runs &cell : row) {
                visit(cell);
            }
        }
    }
}

void Doc::linkWikiText()
{
    eachRuns(blocks, [](Runs &runs) { linkWikiRuns(runs); });
}

QStringList Doc::files() const
{
    QStringList found;
    Doc copy = *this;
    eachRuns(copy.blocks, [&found](Runs &runs) {
        for (const Run &run : std::as_const(runs)) {
            const QString &path = run.isImage() ? run.image : run.href;
            if (path.startsWith(QLatin1String("files/")) && !found.contains(path)) {
                found << path;
            }
        }
    });
    return found;
}

void Doc::mapPaths(const std::function<QString(const QString &, bool)> &map)
{
    eachRuns(blocks, [&map](Runs &runs) {
        for (Run &run : runs) {
            if (run.isImage()) {
                run.image = map(run.image, true);
            } else if (!run.href.isEmpty()) {
                run.href = map(run.href, false);
            }
        }
        // A picture that could not be kept leaves no hole.
        runs.removeIf([](const Run &run) { return run.image.isEmpty() && run.text.isEmpty(); });
    });
}
