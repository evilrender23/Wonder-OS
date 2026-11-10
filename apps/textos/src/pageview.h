#pragma once

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class Editor;

/// One sheet of the document: the paper, and on it what the editor draws.
class PageView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    /// From 0.
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    /// The cursor is in its lit half of a blink.
    Q_PROPERTY(bool caret READ caret WRITE setCaret NOTIFY caretChanged)
    /// Fades every paragraph but the one being written.
    Q_PROPERTY(bool dimmed READ dimmed WRITE setDimmed NOTIFY dimmedChanged)

public:
    explicit PageView(QQuickItem *parent = nullptr);

    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    int page() const { return m_page; }
    void setPage(int page);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    bool caret() const { return m_caret; }
    void setCaret(bool caret);
    bool dimmed() const { return m_dimmed; }
    void setDimmed(bool dimmed);

    void paint(QPainter *painter) override;

Q_SIGNALS:
    void editorChanged();
    void pageChanged();
    void zoomChanged();
    void caretChanged();
    void dimmedChanged();

private:
    Editor *m_editor = nullptr;
    int m_page = 0;
    qreal m_zoom = 1;
    bool m_caret = false;
    bool m_dimmed = false;
};

/// Where the keyboard lands. Accents and other composed letters do not come as
/// keys but through the input method, which only a C++ item can listen to.
class TypingArea : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    /// Where the cursor is, in this item: the input method puts its window by it.
    Q_PROPERTY(QRectF caret MEMBER m_caret NOTIFY caretChanged)

public:
    explicit TypingArea(QQuickItem *parent = nullptr);

Q_SIGNALS:
    void caretChanged();
    /// Text that came composed.
    void committed(const QString &text);

protected:
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    QRectF m_caret;
};
