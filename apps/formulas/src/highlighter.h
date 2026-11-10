#pragma once

#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

/// Colours the LaTeX of the editor: commands, braces, numbers, comments, the
/// pair of braces the cursor is at, and what the parser could not understand.
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY lookChanged)
    Q_PROPERTY(int cursor READ cursor WRITE setCursor NOTIFY lookChanged)
    /// [{position, length}] of the parser.
    Q_PROPERTY(bool painting READ isPainting NOTIFY lookChanged)
    Q_PROPERTY(QVariantList errors READ errors WRITE setErrors NOTIFY lookChanged)

public:
    explicit Highlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const { return m_document; }
    void setQuickDocument(QQuickTextDocument *document);
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    int cursor() const { return m_cursor; }
    void setCursor(int cursor);
    QVariantList errors() const { return m_errors; }
    void setErrors(const QVariantList &errors);
    /// Whether the colours are being laid again right now: the document reports that as a change.
    bool isPainting() const { return m_painting; }

    /// What a piece of source is, for the colours and for the tests.
    enum Kind { Plain, Command, Unknown, Brace, Number, Comment, Operator };
    struct Span {
        int start;
        int length;
        Kind kind;
    };
    static QList<Span> spans(const QString &text);

Q_SIGNALS:
    void documentChanged();
    void lookChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void pair();

    QQuickTextDocument *m_document = nullptr;
    bool m_dark = false;
    int m_cursor = -1;
    int m_open = -1;
    int m_close = -1;
    QVariantList m_errors;
    bool m_painting = false;
};
