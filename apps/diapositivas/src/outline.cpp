#include "outline.h"

using namespace Model;

namespace Outline {

static bool isTitle(const Item &item)
{
    return item.role == QLatin1String("title") || item.role == QLatin1String("ctitle");
}

static bool isBody(const Item &item)
{
    return item.role == QLatin1String("body") || item.role == QLatin1String("subtitle") || item.role == QLatin1String("caption");
}

static int find(const Slide &slide, bool (*wanted)(const Item &))
{
    for (int i = 0; i < slide.items.size(); ++i) {
        if (wanted(slide.items.at(i))) {
            return i;
        }
    }
    return -1;
}

QList<Block> parse(const QString &markdown)
{
    QList<Block> out;
    Block block;
    QStringList notes;
    const auto close = [&] {
        block.notes = notes.join(QLatin1Char('\n'));
        out << block;
        block = Block();
        notes.clear();
    };
    bool titled = false;
    for (const QString &raw : markdown.split(QLatin1Char('\n'))) {
        QString line = raw;
        while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\r')) || line.endsWith(QLatin1Char('\t'))) {
            line.chop(1);
        }
        const QString trimmed = line.trimmed();
        if (trimmed == QLatin1String("---") || trimmed == QLatin1String("***") || trimmed == QLatin1String("___")) {
            close();
            titled = false;
            continue;
        }
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('#'))) {
            int marks = 0;
            while (marks < trimmed.size() && trimmed.at(marks) == QLatin1Char('#')) {
                ++marks;
            }
            const QString text = trimmed.mid(marks).trimmed();
            // A second «# » without a rule between starts a slide all the same.
            if (marks == 1 && titled) {
                close();
            }
            if (marks == 1) {
                block.title = text;
                titled = true;
            } else {
                block.lines << Line{text, false, 0};
            }
            continue;
        }
        if (trimmed.startsWith(QLatin1Char('>'))) {
            notes << trimmed.mid(1).trimmed();
            continue;
        }
        const bool marked = trimmed.size() >= 2 && (trimmed.at(0) == QLatin1Char('-') || trimmed.at(0) == QLatin1Char('*') || trimmed.at(0) == QLatin1Char('+'))
            && trimmed.at(1) == QLatin1Char(' ');
        if (marked) {
            int indent = 0;
            for (const QChar c : line) {
                if (c == QLatin1Char(' ')) {
                    ++indent;
                } else if (c == QLatin1Char('\t')) {
                    indent += 2;
                } else {
                    break;
                }
            }
            block.lines << Line{trimmed.mid(2).trimmed(), true, qMin(indent / 2, 8)};
        } else {
            block.lines << Line{trimmed, false, 0};
        }
    }
    close();
    return out;
}

QString write(const QList<Block> &blocks)
{
    QStringList parts;
    for (const Block &block : blocks) {
        QStringList lines;
        lines << (QStringLiteral("# ") + block.title).trimmed();
        for (const Line &line : block.lines) {
            lines << (line.bullet ? QString(line.level * 2, QLatin1Char(' ')) + QStringLiteral("- ") + line.text : line.text);
        }
        if (!block.notes.isEmpty()) {
            for (const QString &note : block.notes.split(QLatin1Char('\n'))) {
                lines << (QStringLiteral("> ") + note).trimmed();
            }
        }
        parts << lines.join(QLatin1Char('\n'));
    }
    return parts.join(QStringLiteral("\n\n---\n\n")) + QLatin1Char('\n');
}

static QList<Line> linesOf(const Item &item)
{
    QList<Line> out;
    for (const Paragraph &paragraph : item.text) {
        const QString text = paragraph.plain().simplified();
        if (!text.isEmpty()) {
            out << Line{text, paragraph.bullet, paragraph.bullet ? paragraph.level : 0};
        }
    }
    return out;
}

QList<Block> blocks(const Presentation &deck)
{
    QList<Block> out;
    for (const Slide &slide : deck.slides) {
        Block block;
        const int title = find(slide, isTitle);
        if (title >= 0) {
            block.title = slide.items.at(title).plainText().simplified();
        }
        const int body = find(slide, isBody);
        if (body >= 0) {
            block.lines = linesOf(slide.items.at(body));
        }
        QStringList notes;
        for (const QString &note : slide.notes.split(QLatin1Char('\n'))) {
            if (!note.trimmed().isEmpty()) {
                notes << note.trimmed();
            }
        }
        block.notes = notes.join(QLatin1Char('\n'));
        out << block;
    }
    return out;
}

QString fromDeck(const Presentation &deck)
{
    return write(blocks(deck));
}

void apply(Presentation &deck, const QString &markdown)
{
    const QList<Block> wanted = parse(markdown);
    const QList<Block> have = blocks(deck);
    for (int i = 0; i < wanted.size(); ++i) {
        const Block &block = wanted.at(i);
        if (i >= deck.slides.size()) {
            const bool opening = i == 0 && std::none_of(block.lines.begin(), block.lines.end(), [](const Line &line) { return line.bullet; });
            deck.slides << newSlide(opening ? QStringLiteral("title") : QStringLiteral("content"), deck.size());
        }
        Slide &slide = deck.slides[i];
        const Block old = i < have.size() ? have.at(i) : Block();
        if (block.title != old.title) {
            int at = find(slide, isTitle);
            if (at < 0 && !block.title.isEmpty()) {
                applyLayout(slide, slide.layout == QLatin1String("blank") || find(slide, isBody) < 0 ? QStringLiteral("titleOnly") : slide.layout, deck.size());
                at = find(slide, isTitle);
                if (at < 0) {
                    applyLayout(slide, QStringLiteral("titleOnly"), deck.size());
                    at = find(slide, isTitle);
                }
            }
            if (at >= 0) {
                slide.items[at].setPlainText(block.title);
            }
        }
        if (block.lines != old.lines) {
            int at = find(slide, isBody);
            if (at < 0 && !block.lines.isEmpty()) {
                applyLayout(slide, QStringLiteral("content"), deck.size());
                at = find(slide, isBody);
            }
            if (at >= 0) {
                QList<Paragraph> paragraphs;
                for (const Line &line : block.lines) {
                    Run run;
                    run.text = line.text;
                    Paragraph paragraph;
                    paragraph.runs << run;
                    paragraph.bullet = line.bullet;
                    paragraph.level = line.level;
                    paragraphs << paragraph;
                }
                slide.items[at].text = paragraphs;
            }
        }
        if (block.notes != old.notes) {
            slide.notes = block.notes;
        }
    }
    while (deck.slides.size() > qMax(1, int(wanted.size()))) {
        deck.slides.removeLast();
    }
}

}
