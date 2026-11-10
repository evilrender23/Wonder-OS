#pragma once

#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QtQml/qqmlregistration.h>

/// Colours the SQL of a TextArea as it is typed.
class Highlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ quickDocument WRITE setQuickDocument NOTIFY documentChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY darkChanged)

public:
    explicit Highlighter(QObject *parent = nullptr);

    QQuickTextDocument *quickDocument() const { return m_document; }
    void setQuickDocument(QQuickTextDocument *document);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

Q_SIGNALS:
    void documentChanged();
    void darkChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    QPointer<QQuickTextDocument> m_document;
    bool m_dark = false;
};
