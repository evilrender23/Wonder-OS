#include "highlighter.h"

#include "sqlutil.h"

#include <QTextCharFormat>
#include <QTextDocument>

Highlighter::Highlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

void Highlighter::setQuickDocument(QQuickTextDocument *document)
{
    if (document == m_document) {
        return;
    }
    m_document = document;
    setDocument(document ? document->textDocument() : nullptr);
    Q_EMIT documentChanged();
}

void Highlighter::setDark(bool dark)
{
    if (dark == m_dark) {
        return;
    }
    m_dark = dark;
    Q_EMIT darkChanged();
    rehighlight();
}

void Highlighter::highlightBlock(const QString &text)
{
    // A block comment or a string left open on a line above carries on here:
    // the opener is put back in front, and what it covers is shifted away.
    static const QStringList openers = {QString(), QStringLiteral("/*"), QStringLiteral("'"), QStringLiteral("\"")};
    const int carried = qBound(0, previousBlockState(), 3);
    const QString opener = openers.at(carried);
    const QString line = opener + text;
    const int shift = opener.size();

    int open = 0;
    const QList<Sql::Token> tokens = Sql::tokenize(line);
    for (const Sql::Token &token : tokens) {
        QTextCharFormat format;
        switch (token.type) {
        case Sql::Token::Keyword:
            format.setForeground(QColor(m_dark ? QStringLiteral("#ff7ab2") : QStringLiteral("#ad3da4")));
            format.setFontWeight(QFont::DemiBold);
            break;
        case Sql::Token::Function:
            format.setForeground(QColor(m_dark ? QStringLiteral("#6bdfff") : QStringLiteral("#0b6f9a")));
            break;
        case Sql::Token::String:
            format.setForeground(QColor(m_dark ? QStringLiteral("#ff8170") : QStringLiteral("#c41a16")));
            break;
        case Sql::Token::Number:
            format.setForeground(QColor(m_dark ? QStringLiteral("#d9c97c") : QStringLiteral("#1c00cf")));
            break;
        case Sql::Token::Comment:
            format.setForeground(QColor(m_dark ? QStringLiteral("#7f8c98") : QStringLiteral("#6c7986")));
            format.setFontItalic(true);
            break;
        case Sql::Token::Parameter:
            format.setForeground(QColor(m_dark ? QStringLiteral("#ffa14f") : QStringLiteral("#b25000")));
            break;
        default:
            continue;
        }
        const int start = qMax(0, token.start - shift);
        const int end = token.start + token.length - shift;
        if (end > start) {
            setFormat(start, end - start, format);
        }
    }
    // Does the line end inside something?
    if (!tokens.isEmpty()) {
        const Sql::Token &last = tokens.last();
        const QString piece = line.mid(last.start, last.length);
        if (last.type == Sql::Token::Comment && piece.startsWith(QLatin1String("/*")) && (piece.size() < 4 || !piece.endsWith(QLatin1String("*/")))) {
            open = 1;
        } else if (last.type == Sql::Token::String && piece.startsWith(QLatin1Char('\''))) {
            // Quotes come in pairs inside a string: an odd count is an open one.
            open = piece.count(QLatin1Char('\'')) % 2 == 1 ? 2 : 0;
        } else if (last.type == Sql::Token::Identifier && piece.startsWith(QLatin1Char('"'))) {
            open = piece.count(QLatin1Char('"')) % 2 == 1 ? 3 : 0;
        }
    }
    setCurrentBlockState(open);
}
