#include "richtext.h"

#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextOption>

namespace RichText {

using namespace Model;

static Qt::Alignment alignment(int align)
{
    switch (align) {
    case 1:
        return Qt::AlignHCenter;
    case 2:
        return Qt::AlignRight;
    case 3:
        return Qt::AlignJustify;
    default:
        return Qt::AlignLeft;
    }
}

static int alignIndex(Qt::Alignment alignment)
{
    if (alignment & Qt::AlignHCenter) return 1;
    if (alignment & Qt::AlignRight) return 2;
    if (alignment & Qt::AlignJustify) return 3;
    return 0;
}

static QTextCharFormat charFormat(const Run &run, const TextDefaults &d, const Look &look)
{
    QTextCharFormat format;
    format.setFontFamilies({run.font.isEmpty() ? d.font : run.font});
    // A whole number, as Qt's own HTML has it: a real one is written out as «0px».
    format.setProperty(QTextFormat::FontPixelSize, qMax(1, qRound(run.size > 0 ? run.size : d.size)));
    const bool bold = run.bold < 0 ? d.bold : run.bold > 0;
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(run.italic);
    format.setFontUnderline(run.underline);
    format.setFontStrikeOut(run.strike);
    const QColor color = look.color(run.color);
    format.setForeground(color.isValid() ? color : d.color);
    return format;
}

void fill(QTextDocument &document, const QList<Paragraph> &paragraphs, const TextDefaults &d, const Look &look, double width)
{
    document.clear();
    document.setDocumentMargin(0);
    QFont font(d.font);
    font.setPixelSize(qMax(1, qRound(d.size)));
    font.setBold(d.bold);
    document.setDefaultFont(font);
    QTextOption option;
    option.setWrapMode(width > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    document.setDefaultTextOption(option);
    document.setTextWidth(width > 0 ? width : -1);

    QTextCursor cursor(&document);
    QTextList *list = nullptr;
    int listLevel = -1;
    bool first = true;
    for (const Paragraph &paragraph : paragraphs) {
        QTextBlockFormat block;
        block.setAlignment(alignment(paragraph.align < 0 ? d.align : paragraph.align));
        block.setBottomMargin(d.spacing);
        const Run lead = paragraph.runs.isEmpty() ? Run() : paragraph.runs.first();
        const QTextCharFormat leadFormat = charFormat(lead, d, look);
        if (first) {
            cursor.setBlockFormat(block);
            cursor.setBlockCharFormat(leadFormat);
            first = false;
        } else {
            cursor.insertBlock(block, leadFormat);
        }
        if (paragraph.bullet) {
            if (list && listLevel == paragraph.level) {
                list->add(cursor.block());
            } else {
                QTextListFormat format;
                format.setStyle(paragraph.level % 2 == 0 ? QTextListFormat::ListDisc : QTextListFormat::ListCircle);
                format.setIndent(paragraph.level + 1);
                list = cursor.createList(format);
                listLevel = paragraph.level;
            }
        } else {
            list = nullptr;
            listLevel = -1;
        }
        cursor.setCharFormat(leadFormat);
        for (const Run &run : paragraph.runs) {
            if (!run.text.isEmpty()) {
                cursor.insertText(run.text, charFormat(run, d, look));
            }
        }
    }
}

QString toHtml(const QList<Paragraph> &paragraphs, const TextDefaults &d, const Look &look)
{
    QTextDocument document;
    fill(document, paragraphs, d, look, 0);
    return document.toHtml();
}

static Run runOf(const QTextCharFormat &format, const TextDefaults &d)
{
    Run run;
    const QStringList families = format.fontFamilies().toStringList();
    const QString family = families.isEmpty() ? QString() : families.first();
    if (!family.isEmpty() && family.compare(d.font, Qt::CaseInsensitive) != 0) {
        run.font = family;
    }
    double size = format.property(QTextFormat::FontPixelSize).toDouble();
    if (size <= 0) {
        // Text pasted from elsewhere comes in points: on a slide they are the same thing.
        size = format.fontPointSize();
    }
    if (size > 0 && qAbs(size - d.size) > 0.5) {
        run.size = size;
    }
    if (format.hasProperty(QTextFormat::FontWeight)) {
        const bool bold = format.fontWeight() >= QFont::DemiBold;
        if (bold != d.bold) {
            run.bold = bold ? 1 : 0;
        }
    }
    run.italic = format.fontItalic();
    run.underline = format.fontUnderline();
    run.strike = format.fontStrikeOut();
    if (format.hasProperty(QTextFormat::ForegroundBrush)) {
        const QColor color = format.foreground().color();
        if (color.isValid() && color.rgb() != d.color.rgb()) {
            run.color = color.name(QColor::HexRgb);
        }
    }
    return run;
}

QList<Paragraph> tidied(const QList<Paragraph> &paragraphs)
{
    QList<Paragraph> out;
    for (const Paragraph &paragraph : paragraphs) {
        Paragraph tidy = paragraph;
        tidy.runs.clear();
        for (const Run &run : paragraph.runs) {
            if (run.text.isEmpty() && paragraph.runs.size() > 1) {
                continue;
            }
            if (!tidy.runs.isEmpty() && tidy.runs.last().sameFormat(run)) {
                tidy.runs.last().text += run.text;
            } else {
                tidy.runs << run;
            }
        }
        if (tidy.runs.isEmpty() && !paragraph.runs.isEmpty()) {
            tidy.runs << paragraph.runs.first();
        }
        out << tidy;
    }
    return out;
}

QList<Paragraph> fromDocument(const QTextDocument &document, const TextDefaults &d)
{
    QList<Paragraph> out;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        Paragraph paragraph;
        const int align = alignIndex(block.blockFormat().alignment());
        paragraph.align = align == d.align ? -1 : align;
        if (const QTextList *list = block.textList()) {
            paragraph.bullet = true;
            paragraph.level = qBound(0, list->format().indent() - 1, 8);
        }
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            Run run = runOf(fragment.charFormat(), d);
            run.text = fragment.text();
            run.text.replace(QChar(0xfffc), QString());
            paragraph.runs << run;
        }
        if (paragraph.runs.isEmpty()) {
            // An empty line keeps the format it would be typed in.
            paragraph.runs << runOf(block.charFormat(), d);
        }
        out << paragraph;
    }
    return tidied(out);
}

QList<Paragraph> fromHtml(const QString &html, const TextDefaults &d)
{
    QTextDocument document;
    QFont font(d.font);
    font.setPixelSize(qMax(1, qRound(d.size)));
    font.setBold(d.bold);
    document.setDefaultFont(font);
    document.setHtml(html);
    return fromDocument(document, d);
}

}
