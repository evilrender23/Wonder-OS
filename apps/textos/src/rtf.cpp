#include "formats.h"

#include <QBuffer>
#include <QHash>
#include <QTextFrame>
#include <QTextList>
#include <QTextTable>

/// Rich Text Format, as much of it as Textos has a use for: runs and
/// paragraphs with their formatting, old-style lists (\pn), simple tables,
/// PNG and JPEG pictures, links, the page and its header and footer.
namespace Formats {

namespace {

constexpr double twipsPerPx = 15.0;

int twips(double px)
{
    return qRound(px * twipsPerPx);
}

// --- Writing ----------------------------------------------------------------------

class RtfWriter
{
public:
    RtfWriter(const QTextDocument *document, const Doc::PageSetup &setup)
        : m_document(document)
        , m_setup(setup)
    {
        m_fonts << Doc::defaultFamily();
        m_colors << Doc::ink();
    }

    QByteArray run()
    {
        QTextFrame *root = m_document->rootFrame();
        blocks(root->begin(), root->end(), false);

        QByteArray out = "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0\n{\\fonttbl";
        for (int i = 0; i < m_fonts.size(); ++i) {
            out += "{\\f" + QByteArray::number(i) + "\\fnil " + text(m_fonts.at(i)) + ";}";
        }
        out += "}\n{\\colortbl;";
        for (const QColor &color : std::as_const(m_colors)) {
            out += "\\red" + QByteArray::number(color.red()) + "\\green" + QByteArray::number(color.green()) + "\\blue" + QByteArray::number(color.blue()) + ";";
        }
        out += "}\n{\\*\\generator Wonderland Textos;}\n";
        const QSizeF page = m_setup.pagePx();
        out += "\\paperw" + QByteArray::number(twips(page.width())) + "\\paperh" + QByteArray::number(twips(page.height()));
        if (m_setup.landscape) {
            out += "\\landscape";
        }
        out += "\\margl" + QByteArray::number(twips(m_setup.left * Doc::pxPerCm)) + "\\margr" + QByteArray::number(twips(m_setup.right * Doc::pxPerCm))
            + "\\margt" + QByteArray::number(twips(m_setup.top * Doc::pxPerCm)) + "\\margb" + QByteArray::number(twips(m_setup.bottom * Doc::pxPerCm)) + "\n";
        if (!m_setup.header.trimmed().isEmpty()) {
            out += "{\\header\\pard\\qc\\fs18 " + text(m_setup.header.trimmed()) + "\\par}\n";
        }
        if (!m_setup.footer.trimmed().isEmpty() || m_setup.pageNumbers) {
            out += "{\\footer\\pard\\qc\\fs18 " + text(m_setup.footer.trimmed());
            if (m_setup.pageNumbers) {
                out += QByteArray(m_setup.footer.trimmed().isEmpty() ? "" : "\\tab ") + "{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}";
            }
            out += "\\par}\n";
        }
        out += m_body + "}\n";
        return out;
    }

private:
    static QByteArray text(const QString &words)
    {
        QByteArray out;
        for (const QChar c : words) {
            const ushort u = c.unicode();
            if (u == '\\' || u == '{' || u == '}') {
                out += '\\';
                out += char(u);
            } else if (u == '\t') {
                out += "\\tab ";
            } else if (u == QChar::LineSeparator || u == '\n') {
                out += "\\line ";
            } else if (u == QChar::Nbsp) {
                out += "\\~";
            } else if (u >= 0x20 && u < 0x80) {
                out += char(u);
            } else if (u >= 0x80 && u != QChar::ObjectReplacementCharacter) {
                out += "\\u" + QByteArray::number(int(qint16(u))) + "?";
            }
        }
        return out;
    }

    int font(const QString &family)
    {
        int index = int(m_fonts.indexOf(family));
        if (index < 0) {
            m_fonts << family;
            index = int(m_fonts.size()) - 1;
        }
        return index;
    }

    /// 1-based: 0 is the reader's own default.
    int color(const QColor &wanted)
    {
        int index = int(m_colors.indexOf(wanted));
        if (index < 0) {
            m_colors << wanted;
            index = int(m_colors.size()) - 1;
        }
        return index + 1;
    }

    QByteArray looks(const QTextCharFormat &format)
    {
        const Doc::Look look = Doc::lookOf(format);
        QByteArray out = "\\plain\\f" + QByteArray::number(font(look.family)) + "\\fs" + QByteArray::number(qRound(look.size * 2))
            + "\\cf" + QByteArray::number(color(look.color));
        if (look.bold) {
            out += "\\b";
        }
        if (look.italic) {
            out += "\\i";
        }
        if (look.underline) {
            out += "\\ul";
        }
        if (look.strike) {
            out += "\\strike";
        }
        if (look.vertical == 1) {
            out += "\\super";
        } else if (look.vertical == 2) {
            out += "\\sub";
        }
        if (look.background.isValid()) {
            const QByteArray index = QByteArray::number(color(look.background));
            out += "\\chshdng0\\chcbpat" + index + "\\cb" + index;
        }
        return out + ' ';
    }

    void picture(const QTextImageFormat &format)
    {
        const QImage image = Doc::image(m_document, format.name());
        if (image.isNull()) {
            return;
        }
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        const double width = format.width() > 0 ? format.width() : image.width();
        const double height = format.height() > 0 ? format.height() : width * image.height() / qMax(1, image.width());
        m_body += "{\\pict\\pngblip\\picw" + QByteArray::number(image.width()) + "\\pich" + QByteArray::number(image.height())
            + "\\picwgoal" + QByteArray::number(twips(width)) + "\\pichgoal" + QByteArray::number(twips(height)) + "\n" + bytes.toHex() + "}";
    }

    void paragraph(const QTextBlock &block, bool inTable)
    {
        const QTextBlockFormat format = block.blockFormat();
        const QString id = Doc::styleOf(block);
        m_body += "\\pard";
        if (inTable) {
            m_body += "\\intbl";
        }
        m_body += "{\\*\\textosstyle " + id.toLatin1() + "}";
        const int level = Doc::outlineLevel(block);
        if (level > 0) {
            m_body += "\\outlinelevel" + QByteArray::number(level - 1);
        }
        if (format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            m_body += "\\pagebb";
        }
        const Qt::Alignment alignment = format.alignment();
        m_body += alignment & Qt::AlignHCenter ? "\\qc" : alignment & Qt::AlignRight ? "\\qr" : alignment & Qt::AlignJustify ? "\\qj" : "\\ql";
        double left = format.leftMargin() + format.indent() * m_document->indentWidth();
        double first = format.textIndent();
        QByteArray mark;
        if (QTextList *list = block.textList()) {
            const int depth = qMax(1, list->format().indent());
            left += depth * m_document->indentWidth();
            first = -18;
            const QTextListFormat::Style style = list->format().style();
            if (style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha || style == QTextListFormat::ListUpperAlpha
                || style == QTextListFormat::ListLowerRoman || style == QTextListFormat::ListUpperRoman) {
                const char *kind = style == QTextListFormat::ListLowerAlpha ? "\\pnlcltr" : style == QTextListFormat::ListUpperAlpha ? "\\pnucltr"
                    : style == QTextListFormat::ListLowerRoman ? "\\pnlcrm" : style == QTextListFormat::ListUpperRoman ? "\\pnucrm" : "\\pndec";
                mark = "{\\pntext " + text(list->itemText(block)) + "\\tab}{\\*\\pn\\pnlvlbody" + kind + "\\pnstart1\\pnindent270{\\pntxta.}}";
            } else {
                mark = "{\\pntext \\u8226?\\tab}{\\*\\pn\\pnlvlblt\\pnindent270{\\pntxtb\\u8226?}}";
            }
            m_body += "{\\*\\textoslist " + QByteArray::number(int(-style)) + " " + QByteArray::number(depth) + "}";
        }
        m_body += "\\li" + QByteArray::number(twips(left)) + "\\ri" + QByteArray::number(twips(format.rightMargin())) + "\\fi" + QByteArray::number(twips(first))
            + "\\sb" + QByteArray::number(twips(format.topMargin())) + "\\sa" + QByteArray::number(twips(format.bottomMargin()));
        if (format.lineHeightType() == QTextBlockFormat::ProportionalHeight && format.lineHeight() > 0) {
            m_body += "\\sl" + QByteArray::number(qRound(format.lineHeight() * 2.4)) + "\\slmult1";
        }
        m_body += mark;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat chars = fragment.charFormat();
            if (chars.isImageFormat()) {
                for (int i = 0; i < fragment.length(); ++i) {
                    picture(chars.toImageFormat());
                }
                continue;
            }
            const QByteArray run = "{" + looks(chars) + text(fragment.text()) + "}";
            if (chars.isAnchor() && !chars.anchorHref().isEmpty()) {
                m_body += "{\\field{\\*\\fldinst HYPERLINK \"" + text(chars.anchorHref()) + "\"}{\\fldrslt " + run + "}}";
            } else {
                m_body += run;
            }
        }
        // An empty paragraph still has the size of its style.
        if (block.length() <= 1) {
            m_body += "{" + looks(block.charFormat()) + "}";
        }
    }

    void table(QTextTable *table)
    {
        const double width = m_setup.contentPx().width();
        for (int row = 0; row < table->rows(); ++row) {
            m_body += "\\trowd\\trgaph75";
            for (int column = 0; column < table->columns(); ++column) {
                m_body += "\\clbrdrt\\brdrs\\brdrw10\\clbrdrl\\brdrs\\brdrw10\\clbrdrb\\brdrs\\brdrw10\\clbrdrr\\brdrs\\brdrw10\\cellx"
                    + QByteArray::number(twips(width * (column + 1) / table->columns()));
            }
            m_body += "\n";
            for (int column = 0; column < table->columns(); ++column) {
                const QTextTableCell cell = table->cellAt(row, column);
                if (cell.row() == row && cell.column() == column) {
                    bool first = true;
                    for (auto it = cell.begin(); !it.atEnd(); ++it) {
                        if (!it.currentBlock().isValid()) {
                            continue;
                        }
                        if (!first) {
                            m_body += "\\par\n";
                        }
                        first = false;
                        paragraph(it.currentBlock(), true);
                    }
                } else {
                    m_body += "\\pard\\intbl";
                }
                m_body += "\\cell\n";
            }
            m_body += "\\row\n";
        }
    }

    void blocks(QTextFrame::iterator it, const QTextFrame::iterator &end, bool inTable)
    {
        for (; it != end && !it.atEnd(); ++it) {
            if (QTextFrame *frame = it.currentFrame()) {
                if (auto *inner = qobject_cast<QTextTable *>(frame)) {
                    table(inner);
                } else {
                    blocks(frame->begin(), frame->end(), inTable);
                }
            } else if (it.currentBlock().isValid()) {
                paragraph(it.currentBlock(), inTable);
                m_body += "\\par\n";
            }
        }
    }

    const QTextDocument *m_document;
    Doc::PageSetup m_setup;
    QStringList m_fonts;
    QList<QColor> m_colors;
    QByteArray m_body;
};

// --- Reading ----------------------------------------------------------------------

class RtfReader
{
public:
    RtfReader(const QByteArray &rtf, QTextDocument *document, Doc::PageSetup *setup)
        : m_rtf(rtf)
        , m_document(document)
        , m_setup(setup)
        , m_cursor(document)
    {
    }

    bool run()
    {
        if (!m_rtf.startsWith("{\\rtf")) {
            return false;
        }
        m_state.push_back(State());
        m_cursor.beginEditBlock();
        parse();
        flushText();
        flushTable();
        if (m_dirtyBlock) {
            endParagraph(false);
        }
        m_cursor.endEditBlock();
        if (m_setup) {
            if (m_paperWidth > 0 && m_paperHeight > 0) {
                m_setup->setSizeCm(m_paperWidth / twipsPerPx / Doc::pxPerCm, m_paperHeight / twipsPerPx / Doc::pxPerCm);
            }
            const auto margin = [](int value, double fallback) {
                return value >= 0 ? qBound(0.0, qRound(value / twipsPerPx / Doc::pxPerCm * 100) / 100.0, 10.0) : fallback;
            };
            m_setup->left = margin(m_margins[0], m_setup->left);
            m_setup->right = margin(m_margins[1], m_setup->right);
            m_setup->top = margin(m_margins[2], m_setup->top);
            m_setup->bottom = margin(m_margins[3], m_setup->bottom);
            m_setup->header = m_header.simplified();
            m_setup->footer = m_footer.simplified();
            m_setup->pageNumbers = m_pageNumbers;
        }
        return true;
    }

private:
    enum class Dest { Body, Skip, Fonts, Colors, Style, List, Pn, FieldInst, Pict, Header, Footer };
    struct State {
        Dest dest = Dest::Body;
        int font = 0;
        double size = 12;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        int vertical = 0;
        int color = 0;
        int background = 0;
        int skip = 1;
        QString href;
    };
    struct Para {
        Qt::Alignment alignment = Qt::AlignLeft;
        int left = 0;
        int right = 0;
        int first = 0;
        int before = 0;
        int after = 0;
        int line = 0;
        bool lineMultiple = false;
        bool inTable = false;
        bool breakBefore = false;
        int outline = -1;
        /// 0 none, 1 bullets, 2 numbers (from \pn); or a QTextListFormat style (negative) from Textos itself.
        int list = 0;
        int listDepth = 0;
        QString style;
    };
    struct Run {
        QString text;
        QTextCharFormat format;
        bool breakAfter = false;
    };

    State &now() { return m_state.back(); }

    QTextCharFormat format() const
    {
        const State &state = m_state.back();
        QTextCharFormat format;
        const QString family = m_fonts.value(state.font);
        format.setFontFamilies({family.isEmpty() ? Doc::defaultFamily() : family});
        format.setFontPointSize(state.size > 0 ? state.size : 12);
        format.setFontWeight(state.bold ? QFont::Bold : QFont::Normal);
        format.setFontItalic(state.italic);
        format.setFontUnderline(state.underline);
        format.setFontStrikeOut(state.strike);
        format.setVerticalAlignment(state.vertical == 1 ? QTextCharFormat::AlignSuperScript
            : state.vertical == 2 ? QTextCharFormat::AlignSubScript : QTextCharFormat::AlignNormal);
        // The table's first entry is «automatic», so the numbers are the list's own.
        format.setForeground(state.color > 0 && state.color < m_colors.size() ? m_colors.at(state.color) : Doc::ink());
        if (state.background > 0 && state.background < m_colors.size()) {
            format.setBackground(m_colors.at(state.background));
        }
        if (!state.href.isEmpty()) {
            format.merge(Doc::linkFormat(state.href));
        }
        return format;
    }

    void put(const QString &words)
    {
        State &state = now();
        switch (state.dest) {
        case Dest::Body:
            if (m_pending.isEmpty()) {
                m_pendingFormat = format();
            } else if (m_pendingFormat != format()) {
                flushText();
                m_pendingFormat = format();
            }
            m_pending += words;
            break;
        case Dest::Fonts:
        case Dest::Style:
        case Dest::List:
        case Dest::FieldInst:
            m_scratch += words;
            break;
        case Dest::Colors:
            if (words.contains(QLatin1Char(';'))) {
                m_colors << (m_colorSet ? m_color : Doc::ink());
                m_color = QColor(0, 0, 0);
                m_colorSet = false;
            }
            break;
        case Dest::Pict:
            m_scratch += words;
            break;
        case Dest::Header:
            m_header += words;
            break;
        case Dest::Footer:
            m_footer += words;
            break;
        case Dest::Pn:
        case Dest::Skip:
            break;
        }
    }

    void flushText()
    {
        if (m_pending.isEmpty()) {
            return;
        }
        if (m_para.inTable) {
            m_cell.append({m_pending, m_pendingFormat, false});
        } else {
            flushTable();
            ensureBlock();
            m_cursor.insertText(m_pending, m_pendingFormat);
            m_dirtyBlock = true;
        }
        m_pending.clear();
    }

    /// \par only promises a paragraph: it is made when something goes in it,
    /// so that a table can follow a paragraph without an empty one between.
    void ensureBlock()
    {
        if (m_needBlock) {
            m_cursor.insertBlock(QTextBlockFormat(), format());
            m_needBlock = false;
        }
    }

    QTextBlockFormat blockFormat(QString *styleId) const
    {
        QString id = m_para.style;
        if (id.isEmpty() && m_para.outline >= 0 && m_para.outline < 3) {
            id = QLatin1Char('h') + QString::number(m_para.outline + 1);
        }
        if (id.isEmpty() || Doc::style(id).id != id) {
            id = QStringLiteral("body");
        }
        *styleId = id;
        QTextBlockFormat format;
        format.setProperty(Doc::StyleProperty, id);
        format.setHeadingLevel(Doc::style(id).heading);
        format.setAlignment(m_para.alignment);
        const bool listed = m_para.list != 0;
        format.setLeftMargin(listed ? 0 : m_para.left / twipsPerPx);
        format.setRightMargin(m_para.right / twipsPerPx);
        format.setTextIndent(listed ? 0 : m_para.first / twipsPerPx);
        format.setTopMargin(m_para.before / twipsPerPx);
        format.setBottomMargin(m_para.after / twipsPerPx);
        if (m_para.lineMultiple && m_para.line > 0) {
            format.setLineHeight(qRound(m_para.line / 2.4), QTextBlockFormat::ProportionalHeight);
        }
        if (id == QLatin1String("code")) {
            format.setBackground(Doc::style(id).block.background());
        }
        if (m_para.breakBefore || m_breakPending) {
            format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
        return format;
    }

    void endParagraph(bool startAnother)
    {
        flushText();
        if (m_para.inTable) {
            if (!m_cell.isEmpty()) {
                m_cell.last().breakAfter = true;
            } else {
                m_cell.append({QString(), format(), true});
            }
            return;
        }
        flushTable();
        ensureBlock();
        QString id;
        const QTextBlockFormat block = blockFormat(&id);
        m_breakPending = false;
        m_cursor.setBlockFormat(block);
        if (m_cursor.block().length() <= 1) {
            m_cursor.setBlockCharFormat(format());
        }
        if (m_para.list != 0) {
            QTextListFormat::Style style = QTextListFormat::ListDisc;
            int depth = qMax(1, m_para.listDepth);
            if (m_para.list < 0) {
                style = QTextListFormat::Style(m_para.list);
            } else {
                // Half an inch a level, as everyone else indents them.
                depth = qBound(1, qRound(m_para.left / 720.0), 9);
                style = m_para.list == 2 ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
            }
            QTextList *open = m_lists.value(depth);
            if (open && open->format().style() == style && m_listed) {
                open->add(m_cursor.block());
            } else {
                QTextListFormat listFormat;
                listFormat.setStyle(style);
                listFormat.setIndent(depth);
                m_lists.insert(depth, m_cursor.createList(listFormat));
            }
            for (int deeper = depth + 1; deeper <= 9; ++deeper) {
                m_lists.remove(deeper);
            }
            m_listed = true;
        } else {
            m_lists.clear();
            m_listed = false;
        }
        // What \pn said holds for one paragraph; \pard says the rest anew.
        m_dirtyBlock = false;
        m_needBlock = startAnother;
    }

    void endCell()
    {
        flushText();
        m_row.append(m_cell);
        m_cell.clear();
    }

    void endRow()
    {
        if (!m_cell.isEmpty()) {
            endCell();
        }
        m_rows.append(m_row);
        m_row.clear();
    }

    void flushTable()
    {
        if (m_rows.isEmpty()) {
            return;
        }
        const QList<QList<QList<Run>>> rows = m_rows;
        m_rows.clear();
        int columns = 0;
        for (const auto &row : rows) {
            columns = qMax(columns, int(row.size()));
        }
        if (columns == 0) {
            return;
        }
        // A table takes the place of the empty paragraph the cursor is in.
        QTextTable *table = m_cursor.insertTable(int(rows.size()), columns, Doc::tableFormat(columns));
        const Doc::Style &body = Doc::style(QStringLiteral("body"));
        for (int r = 0; r < rows.size(); ++r) {
            for (int c = 0; c < rows.at(r).size(); ++c) {
                QTextCursor at = table->cellAt(r, c).firstCursorPosition();
                QTextBlockFormat block = body.block;
                block.setBottomMargin(0);
                at.setBlockFormat(block);
                const QList<Run> &runs = rows.at(r).at(c);
                for (int i = 0; i < runs.size(); ++i) {
                    at.insertText(runs.at(i).text, runs.at(i).format);
                    if (runs.at(i).breakAfter && i + 1 < runs.size()) {
                        at.insertBlock(block, runs.at(i).format);
                    }
                }
            }
        }
        m_cursor = QTextCursor(m_document);
        m_cursor.setPosition(table->lastPosition() + 1);
        m_lists.clear();
        m_dirtyBlock = false;
        m_needBlock = false;
    }

    void picture()
    {
        const QImage image = QImage::fromData(QByteArray::fromHex(m_scratch.toLatin1()));
        m_scratch.clear();
        if (image.isNull() || m_para.inTable) {
            return;
        }
        flushText();
        flushTable();
        ensureBlock();
        QTextImageFormat format;
        format.setName(Doc::addImage(m_document, image));
        const double width = m_pictWidth > 0 ? m_pictWidth / twipsPerPx : image.width();
        const double height = m_pictHeight > 0 ? m_pictHeight / twipsPerPx : image.height();
        format.setWidth(width);
        format.setHeight(height);
        m_cursor.insertImage(format);
        m_dirtyBlock = true;
    }

    void closeGroup()
    {
        const State closed = m_state.back();
        if (m_state.size() > 1) {
            flushText();
            m_state.pop_back();
        }
        const Dest outer = now().dest;
        if (closed.dest == outer) {
            if (closed.dest == Dest::Fonts) {
                fontEntry(closed.font);
            }
            return;
        }
        switch (closed.dest) {
        case Dest::Fonts:
            fontEntry(closed.font);
            break;
        case Dest::Style:
            m_para.style = m_scratch.trimmed();
            m_scratch.clear();
            break;
        case Dest::List: {
            const QStringList parts = m_scratch.simplified().split(QLatin1Char(' '));
            m_scratch.clear();
            if (parts.size() == 2 && parts.at(0).toInt() > 0) {
                m_para.list = -parts.at(0).toInt();
                m_para.listDepth = parts.at(1).toInt();
            }
            break;
        }
        case Dest::FieldInst: {
            const QString instruction = m_scratch.simplified();
            m_scratch.clear();
            if (instruction.startsWith(QLatin1String("HYPERLINK"))) {
                QString target = instruction.mid(9).trimmed();
                if (target.startsWith(QLatin1Char('"'))) {
                    target = target.mid(1, target.indexOf(QLatin1Char('"'), 1) - 1);
                }
                // The result of the field is a sibling group: it finds this on its way.
                m_fieldHref = target;
            } else if (instruction.startsWith(QLatin1String("PAGE")) && outer == Dest::Footer) {
                m_pageNumbers = true;
            }
            break;
        }
        case Dest::Pict:
            picture();
            break;
        default:
            break;
        }
    }

    void fontEntry(int id)
    {
        QString name = m_scratch.trimmed();
        m_scratch.clear();
        if (name.endsWith(QLatin1Char(';'))) {
            name.chop(1);
        }
        if (!name.isEmpty()) {
            m_fonts.insert(id, name.trimmed());
        }
    }

    void control(const QByteArray &word, bool hasNumber, int number)
    {
        State &state = now();
        const auto on = [&] {
            return !hasNumber || number != 0;
        };
        // Destinations first: they say what the rest of the group is.
        static const QHash<QByteArray, Dest> destinations = {
            {"fonttbl", Dest::Fonts}, {"colortbl", Dest::Colors}, {"textosstyle", Dest::Style}, {"textoslist", Dest::List},
            {"pn", Dest::Pn}, {"pntext", Dest::Skip}, {"listtext", Dest::Skip}, {"fldinst", Dest::FieldInst}, {"pict", Dest::Pict},
            {"header", Dest::Header}, {"footer", Dest::Footer}, {"headerl", Dest::Skip}, {"headerr", Dest::Skip}, {"headerf", Dest::Skip},
            {"footerl", Dest::Skip}, {"footerr", Dest::Skip}, {"footerf", Dest::Skip}, {"stylesheet", Dest::Skip}, {"info", Dest::Skip},
            {"footnote", Dest::Skip}, {"object", Dest::Skip}, {"nonshppict", Dest::Skip}, {"themedata", Dest::Skip},
            {"colorschememapping", Dest::Skip}, {"datastore", Dest::Skip}, {"latentstyles", Dest::Skip}, {"listtable", Dest::Skip},
            {"listoverridetable", Dest::Skip}, {"generator", Dest::Skip}, {"xmlnstbl", Dest::Skip}, {"rsidtbl", Dest::Skip},
            {"mmathPr", Dest::Skip}, {"shpinst", Dest::Skip}, {"bkmkstart", Dest::Skip}, {"bkmkend", Dest::Skip},
        };
        const auto destination = destinations.constFind(word);
        if (destination != destinations.constEnd()) {
            if (state.dest != Dest::Body && state.dest != Dest::Header && state.dest != Dest::Footer) {
                if (state.dest != Dest::Pn) {
                    state.dest = Dest::Skip;
                }
                m_ignorable = false;
                return;
            }
            flushText();
            state.dest = destination.value();
            m_ignorable = false;
            if (state.dest == Dest::Pict) {
                m_scratch.clear();
                m_pictWidth = 0;
                m_pictHeight = 0;
            } else if (state.dest == Dest::Header) {
                m_header.clear();
            } else if (state.dest == Dest::Footer) {
                m_footer.clear();
            } else if (state.dest != Dest::Skip && state.dest != Dest::Pn) {
                m_scratch.clear();
            }
            return;
        }
        if (word == "shppict") {
            // Word's wrapper around a picture: the picture is what counts.
            m_ignorable = false;
            return;
        }
        if (m_ignorable) {
            // {\*\something we do not know}: none of it is text.
            m_ignorable = false;
            state.dest = Dest::Skip;
            return;
        }
        if (state.dest == Dest::Skip) {
            return;
        }
        if (state.dest == Dest::Pn) {
            if (word == "pnlvlblt") {
                m_para.list = m_para.list < 0 ? m_para.list : 1;
            } else if (word == "pnlvlbody" || word == "pndec") {
                m_para.list = m_para.list < 0 ? m_para.list : 2;
            }
            return;
        }
        if (state.dest == Dest::Colors) {
            if (word == "red") {
                m_color.setRed(qBound(0, number, 255));
                m_colorSet = true;
            } else if (word == "green") {
                m_color.setGreen(qBound(0, number, 255));
                m_colorSet = true;
            } else if (word == "blue") {
                m_color.setBlue(qBound(0, number, 255));
                m_colorSet = true;
            }
            return;
        }
        if (state.dest == Dest::Pict) {
            if (word == "picwgoal") {
                m_pictWidth = number;
            } else if (word == "pichgoal") {
                m_pictHeight = number;
            }
            return;
        }
        if (state.dest == Dest::Header || state.dest == Dest::Footer) {
            // Only the words of a header: how they look is Textos's business.
            if (word == "u") {
                put(QString(QChar(ushort(number < 0 ? number + 65536 : number))));
                m_skipChars = state.skip;
            } else if (word == "par" || word == "tab" || word == "line") {
                put(QStringLiteral(" "));
            } else if (word == "fldrslt") {
                state.dest = Dest::Skip;
            } else if (word == "uc") {
                state.skip = qMax(0, number);
            }
            return;
        }
        if (word == "f") {
            state.font = number;
            return;
        }
        if (state.dest == Dest::Fonts || state.dest == Dest::Style || state.dest == Dest::List || state.dest == Dest::FieldInst) {
            return;
        }
        if (word == "u") {
            put(QString(QChar(ushort(number < 0 ? number + 65536 : number))));
            m_skipChars = state.skip;
        } else if (word == "uc") {
            state.skip = qMax(0, number);
        } else if (word == "par" || word == "sect") {
            if (state.dest == Dest::Body) {
                endParagraph(true);
            } else {
                put(QStringLiteral(" "));
            }
        } else if (word == "line") {
            put(QString(QChar::LineSeparator));
        } else if (word == "tab") {
            put(QStringLiteral("\t"));
        } else if (word == "page") {
            flushText();
            m_breakPending = true;
        } else if (word == "cell") {
            endParagraph(false);
            endCell();
        } else if (word == "row") {
            flushText();
            endRow();
        } else if (word == "emdash") {
            put(QStringLiteral("—"));
        } else if (word == "endash") {
            put(QStringLiteral("–"));
        } else if (word == "bullet") {
            put(QStringLiteral("•"));
        } else if (word == "lquote") {
            put(QStringLiteral("‘"));
        } else if (word == "rquote") {
            put(QStringLiteral("’"));
        } else if (word == "ldblquote") {
            put(QStringLiteral("“"));
        } else if (word == "rdblquote") {
            put(QStringLiteral("”"));
        } else if (word == "plain") {
            const State fresh;
            state.size = fresh.size;
            state.font = 0;
            state.bold = state.italic = state.underline = state.strike = false;
            state.vertical = 0;
            state.color = 0;
            state.background = 0;
        } else if (word == "pard") {
            flushText();
            m_para = Para();
        } else if (word == "b") {
            state.bold = on();
        } else if (word == "i") {
            state.italic = on();
        } else if (word == "ul") {
            state.underline = on();
        } else if (word == "ulnone") {
            state.underline = false;
        } else if (word == "strike") {
            state.strike = on();
        } else if (word == "super") {
            state.vertical = on() ? 1 : 0;
        } else if (word == "sub") {
            state.vertical = on() ? 2 : 0;
        } else if (word == "nosupersub") {
            state.vertical = 0;
        } else if (word == "fs") {
            state.size = number / 2.0;
        } else if (word == "cf") {
            state.color = number;
        } else if (word == "chcbpat" || word == "cb" || word == "highlight") {
            state.background = number;
        } else if (word == "ql") {
            m_para.alignment = Qt::AlignLeft;
        } else if (word == "qc") {
            m_para.alignment = Qt::AlignHCenter;
        } else if (word == "qr") {
            m_para.alignment = Qt::AlignRight;
        } else if (word == "qj") {
            m_para.alignment = Qt::AlignJustify;
        } else if (word == "li") {
            m_para.left = number;
        } else if (word == "ri") {
            m_para.right = number;
        } else if (word == "fi") {
            m_para.first = number;
        } else if (word == "sb") {
            m_para.before = number;
        } else if (word == "sa") {
            m_para.after = number;
        } else if (word == "sl") {
            m_para.line = number;
        } else if (word == "slmult") {
            m_para.lineMultiple = number == 1;
        } else if (word == "intbl") {
            flushText();
            m_para.inTable = true;
        } else if (word == "pagebb") {
            m_para.breakBefore = true;
        } else if (word == "outlinelevel") {
            m_para.outline = number;
        } else if (word == "paperw") {
            m_paperWidth = number;
        } else if (word == "paperh") {
            m_paperHeight = number;
        } else if (word == "margl") {
            m_margins[0] = number;
        } else if (word == "margr") {
            m_margins[1] = number;
        } else if (word == "margt") {
            m_margins[2] = number;
        } else if (word == "margb") {
            m_margins[3] = number;
        } else if (word == "fldrslt") {
            state.href = m_fieldHref;
            m_fieldHref.clear();
        }
    }

    static QChar cp1252(uchar byte)
    {
        static const ushort high[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
                                        0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
        return byte >= 0x80 && byte < 0xA0 ? QChar(high[byte - 0x80]) : QChar(ushort(byte));
    }

    void parse()
    {
        const qsizetype size = m_rtf.size();
        qsizetype i = 0;
        QByteArray literal;
        const auto flushLiteral = [&] {
            if (!literal.isEmpty()) {
                put(QString::fromLatin1(literal));
                literal.clear();
            }
        };
        while (i < size) {
            const char c = m_rtf.at(i);
            if (c == '{') {
                flushLiteral();
                flushText();
                m_state.push_back(m_state.back());
                ++i;
            } else if (c == '}') {
                flushLiteral();
                closeGroup();
                m_ignorable = false;
                ++i;
            } else if (c == '\\') {
                flushLiteral();
                ++i;
                if (i >= size) {
                    break;
                }
                const char next = m_rtf.at(i);
                if (next == '\'') {
                    const QChar decoded = cp1252(uchar(m_rtf.mid(i + 1, 2).toInt(nullptr, 16)));
                    i += 3;
                    if (m_skipChars > 0) {
                        --m_skipChars;
                    } else {
                        put(QString(decoded));
                    }
                } else if (next == '*') {
                    m_ignorable = true;
                    ++i;
                } else if (next == '~') {
                    put(QString(QChar(QChar::Nbsp)));
                    ++i;
                } else if (next == '\\' || next == '{' || next == '}') {
                    if (m_skipChars > 0) {
                        --m_skipChars;
                    } else {
                        put(QString(QLatin1Char(next)));
                    }
                    ++i;
                } else if (next == '\n' || next == '\r') {
                    control("par", false, 0);
                    ++i;
                } else if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')) {
                    const qsizetype start = i;
                    while (i < size && ((m_rtf.at(i) >= 'a' && m_rtf.at(i) <= 'z') || (m_rtf.at(i) >= 'A' && m_rtf.at(i) <= 'Z'))) {
                        ++i;
                    }
                    const QByteArray word = m_rtf.mid(start, i - start);
                    const qsizetype digits = i;
                    if (i < size && m_rtf.at(i) == '-') {
                        ++i;
                    }
                    while (i < size && m_rtf.at(i) >= '0' && m_rtf.at(i) <= '9') {
                        ++i;
                    }
                    const bool hasNumber = i > digits && m_rtf.at(i - 1) != '-';
                    const int number = hasNumber ? m_rtf.mid(digits, i - digits).toInt() : 0;
                    if (i < size && m_rtf.at(i) == ' ') {
                        ++i;
                    }
                    if (word == "bin") {
                        i += qMax(0, number);
                    } else {
                        m_skipChars = 0;
                        control(word, hasNumber, number);
                    }
                } else {
                    // \- \_ \| and the like: nothing Textos shows.
                    ++i;
                }
            } else if (c == '\r' || c == '\n') {
                flushLiteral();
                ++i;
            } else {
                if (m_skipChars > 0) {
                    --m_skipChars;
                } else {
                    literal += c;
                }
                ++i;
            }
        }
        flushLiteral();
    }

    QByteArray m_rtf;
    QTextDocument *m_document;
    Doc::PageSetup *m_setup;
    QTextCursor m_cursor;
    std::vector<State> m_state;
    Para m_para;
    QHash<int, QString> m_fonts;
    QList<QColor> m_colors;
    QColor m_color = QColor(0, 0, 0);
    bool m_colorSet = false;
    QString m_pending;
    QTextCharFormat m_pendingFormat;
    QString m_scratch;
    QString m_fieldHref;
    QString m_header;
    QString m_footer;
    QHash<int, QTextList *> m_lists;
    QList<Run> m_cell;
    QList<QList<Run>> m_row;
    QList<QList<QList<Run>>> m_rows;
    int m_skipChars = 0;
    int m_pictWidth = 0;
    int m_pictHeight = 0;
    int m_paperWidth = 0;
    int m_paperHeight = 0;
    int m_margins[4] = {-1, -1, -1, -1};
    bool m_ignorable = false;
    bool m_dirtyBlock = false;
    bool m_needBlock = false;
    bool m_breakPending = false;
    bool m_listed = false;
    bool m_pageNumbers = false;
};

} // namespace

QByteArray toRtf(const QTextDocument *document, const Doc::PageSetup &setup)
{
    return RtfWriter(document, setup).run();
}

bool fromRtf(const QByteArray &rtf, QTextDocument *document, Doc::PageSetup *setup)
{
    Doc::prepare(document);
    return RtfReader(rtf, document, setup).run();
}

} // namespace Formats
