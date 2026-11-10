#include "highlighter.h"

#include "latex.h"

#include <QTextDocument>

Highlighter::Highlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

void Highlighter::setQuickDocument(QQuickTextDocument *document)
{
    if (document != m_document) {
        m_document = document;
        setDocument(document ? document->textDocument() : nullptr);
        Q_EMIT documentChanged();
    }
}

void Highlighter::setDark(bool dark)
{
    if (dark != m_dark) {
        m_dark = dark;
        m_painting = true;
        rehighlight();
        m_painting = false;
        Q_EMIT lookChanged();
    }
}

void Highlighter::pair()
{
    m_open = m_close = -1;
    if (!document() || m_cursor < 0) {
        return;
    }
    const QString text = document()->toPlainText();
    // The brace after the cursor, or the one just before it.
    for (const int at : {m_cursor, m_cursor - 1}) {
        if (at < 0 || at >= text.size()) {
            continue;
        }
        const QChar c = text.at(at);
        if (c != QLatin1Char('{') && c != QLatin1Char('}')) {
            continue;
        }
        const int other = Latex::matchingBrace(text, at);
        if (other >= 0) {
            m_open = qMin(at, other);
            m_close = qMax(at, other);
            return;
        }
    }
}

void Highlighter::setCursor(int cursor)
{
    if (cursor == m_cursor) {
        return;
    }
    m_cursor = cursor;
    const int open = m_open;
    const int close = m_close;
    pair();
    if (open != m_open || close != m_close) {
        m_painting = true;
        rehighlight();
        m_painting = false;
    }
    Q_EMIT lookChanged();
}

void Highlighter::setErrors(const QVariantList &errors)
{
    if (errors != m_errors) {
        m_errors = errors;
        pair();
        m_painting = true;
        rehighlight();
        m_painting = false;
        Q_EMIT lookChanged();
    }
}

QList<Highlighter::Span> Highlighter::spans(const QString &text)
{
    QList<Span> out;
    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\')) {
            int end = i + 1;
            while (end < text.size() && text.at(end).isLetter() && text.at(end).unicode() < 128) {
                ++end;
            }
            if (end == i + 1) {
                // `\\`, `\,`, `\{`: one character, whatever it is.
                end = qMin<int>(text.size(), i + 2);
                out.append({i, end - i, Command});
            } else {
                out.append({i, end - i, Symbols::known(text.mid(i + 1, end - i - 1)) ? Command : Unknown});
            }
            i = end;
        } else if (c == QLatin1Char('%')) {
            out.append({i, int(text.size()) - i, Comment});
            break;
        } else if (c == QLatin1Char('{') || c == QLatin1Char('}') || c == QLatin1Char('[') || c == QLatin1Char(']')) {
            out.append({i, 1, Brace});
            ++i;
        } else if (c.isDigit()) {
            int end = i + 1;
            while (end < text.size() && (text.at(end).isDigit() || ((text.at(end) == QLatin1Char('.') || text.at(end) == QLatin1Char(',')) && end + 1 < text.size() && text.at(end + 1).isDigit()))) {
                ++end;
            }
            out.append({i, end - i, Number});
            i = end;
        } else if (c == QLatin1Char('^') || c == QLatin1Char('_') || c == QLatin1Char('&')) {
            out.append({i, 1, Operator});
            ++i;
        } else {
            ++i;
        }
    }
    return out;
}

void Highlighter::highlightBlock(const QString &text)
{
    const QColor command = m_dark ? QColor(0x8e, 0x8c, 0xff) : QColor(0x5b, 0x5b, 0xd6);
    const QColor brace = m_dark ? QColor(0xa0, 0xa0, 0xa5) : QColor(0x8a, 0x8a, 0x90);
    const QColor number = m_dark ? QColor(0x6f, 0xcf, 0x97) : QColor(0x1f, 0x8a, 0x4c);
    const QColor comment = m_dark ? QColor(0x6a, 0x6a, 0x6e) : QColor(0xae, 0xae, 0xb2);
    const QColor oper = m_dark ? QColor(0xff, 0x9f, 0x5a) : QColor(0xc2, 0x5e, 0x12);
    const QColor wrong = m_dark ? QColor(0xff, 0x69, 0x61) : QColor(0xd7, 0x00, 0x15);
    for (const Span &span : spans(text)) {
        QTextCharFormat format;
        switch (span.kind) {
        case Command: format.setForeground(command); break;
        case Unknown: format.setForeground(wrong); break;
        case Brace: format.setForeground(brace); break;
        case Number: format.setForeground(number); break;
        case Comment: format.setForeground(comment); format.setFontItalic(true); break;
        case Operator: format.setForeground(oper); break;
        case Plain: break;
        }
        setFormat(span.start, span.length, format);
    }
    const int base = currentBlock().position();
    const auto inBlock = [&](int position) { return position >= base && position < base + text.size(); };
    for (const QVariant &value : std::as_const(m_errors)) {
        const QVariantMap error = value.toMap();
        const int start = error.value(QStringLiteral("position")).toInt() - base;
        const int length = qMax(1, error.value(QStringLiteral("length")).toInt());
        for (int i = qMax(0, start); i < qMin<int>(text.size(), start + length); ++i) {
            QTextCharFormat f = format(i);
            f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            f.setUnderlineColor(wrong);
            f.setFontUnderline(true);
            setFormat(i, 1, f);
        }
    }
    for (const int at : {m_open, m_close}) {
        if (at >= 0 && inBlock(at)) {
            QTextCharFormat f;
            f.setForeground(command);
            f.setFontWeight(QFont::Bold);
            f.setBackground(m_dark ? QColor(0x8e, 0x8c, 0xff, 0x40) : QColor(0x5b, 0x5b, 0xd6, 0x2c));
            setFormat(at - base, 1, f);
        }
    }
}
