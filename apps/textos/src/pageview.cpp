#include "pageview.h"

#include "editor.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QPainter>

PageView::PageView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void PageView::setEditor(Editor *editor)
{
    if (editor == m_editor) {
        return;
    }
    if (m_editor) {
        disconnect(m_editor, nullptr, this, nullptr);
    }
    m_editor = editor;
    if (m_editor) {
        connect(m_editor, &Editor::repaint, this, [this] { update(); });
        connect(m_editor, &Editor::setupChanged, this, [this] { update(); });
    }
    Q_EMIT editorChanged();
    update();
}

void PageView::setPage(int page)
{
    if (page != m_page) {
        m_page = page;
        Q_EMIT pageChanged();
        update();
    }
}

void PageView::setZoom(qreal zoom)
{
    if (!qFuzzyCompare(zoom, m_zoom)) {
        m_zoom = zoom;
        Q_EMIT zoomChanged();
        update();
    }
}

void PageView::setCaret(bool caret)
{
    if (caret != m_caret) {
        m_caret = caret;
        Q_EMIT caretChanged();
        // Only the sheet with the cursor has anything to blink.
        if (m_editor && m_editor->cursorPage() == m_page) {
            update();
        }
    }
}

void PageView::setDimmed(bool dimmed)
{
    if (dimmed != m_dimmed) {
        m_dimmed = dimmed;
        Q_EMIT dimmedChanged();
        update();
    }
}

void PageView::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), Qt::white);
    if (!m_editor) {
        return;
    }
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->scale(m_zoom, m_zoom);
    m_editor->paintPage(painter, m_page, m_caret);
    if (m_dimmed) {
        // A veil of paper over everything but the paragraph in hand.
        const QRectF sheet(0, 0, m_editor->pageWidth(), m_editor->pageHeight());
        const QRectF lit = m_editor->cursorPage() == m_page ? m_editor->paragraphRect() : QRectF();
        const QColor veil(255, 255, 255, 178);
        if (lit.isNull()) {
            painter->fillRect(sheet, veil);
        } else {
            painter->fillRect(QRectF(0, 0, sheet.width(), lit.top()), veil);
            painter->fillRect(QRectF(0, lit.bottom(), sheet.width(), sheet.height() - lit.bottom()), veil);
        }
    }
}

TypingArea::TypingArea(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setFlag(ItemIsFocusScope, false);
    setAcceptedMouseButtons(Qt::NoButton);
    connect(this, &TypingArea::caretChanged, this, [] {
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
    });
}

void TypingArea::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        Q_EMIT committed(event->commitString());
    }
    event->accept();
}

QVariant TypingArea::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle:
        return m_caret;
    case Qt::ImHints:
        return int(Qt::ImhMultiLine);
    default:
        return QQuickItem::inputMethodQuery(query);
    }
}
