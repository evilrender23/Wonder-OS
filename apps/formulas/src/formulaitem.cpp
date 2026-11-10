#include "formulaitem.h"

#include <QPainter>

namespace {
const qreal Padding = 4;
}

FormulaItem::FormulaItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    relayout();
}

void FormulaItem::setLatex(const QString &latex)
{
    if (latex != m_latex) {
        m_latex = latex;
        relayout();
        Q_EMIT latexChanged();
    }
}

void FormulaItem::setColor(const QColor &color)
{
    if (color != m_color) {
        m_color = color;
        update();
        Q_EMIT colorChanged();
    }
}

void FormulaItem::setSize(qreal size)
{
    if (!qFuzzyCompare(size, m_size) && size > 0) {
        m_size = size;
        relayout();
        Q_EMIT sizeChanged();
    }
}

void FormulaItem::setFamily(const QString &family)
{
    if (family != m_family) {
        m_family = family;
        relayout();
        Q_EMIT familyChanged();
    }
}

void FormulaItem::setFit(qreal fit)
{
    if (!qFuzzyCompare(fit + 1, m_fit + 1)) {
        m_fit = fit;
        relayout();
        Q_EMIT fitChanged();
    }
}

void FormulaItem::setMark(int mark)
{
    if (mark != m_mark) {
        m_mark = mark;
        update();
        Q_EMIT markChanged();
    }
}

void FormulaItem::setMarkColor(const QColor &color)
{
    if (color != m_markColor) {
        m_markColor = color;
        update();
        Q_EMIT markChanged();
    }
}

void FormulaItem::relayout()
{
    LayoutOptions options;
    options.family = m_family;
    options.size = m_size;
    m_layout = Formula::layout(m_latex, options);
    m_shrink = 1;
    const qreal room = m_fit - 2 * Padding;
    if (m_fit > 0 && m_layout.box.width > room && room > 20) {
        // Laid out again at the size that fits, rather than scaled: the rules stay as thick as they should.
        m_shrink = room / m_layout.box.width;
        options.size = m_size * m_shrink;
        m_layout = Formula::layout(m_latex, options);
    }
    const QSizeF natural = Formula::size(m_layout.box, Padding);
    setImplicitSize(std::ceil(natural.width()), std::ceil(natural.height()));
    update();
    Q_EMIT laidOut();
}

QPointF FormulaItem::origin() const
{
    // Centred in whatever room the item was given.
    const QSizeF natural = Formula::size(m_layout.box, 0);
    return QPointF((width() - natural.width()) / 2, (height() - natural.height()) / 2 + m_layout.box.height);
}

void FormulaItem::paint(QPainter *painter)
{
    painter->setRenderHint(QPainter::Antialiasing);
    const QPointF at = origin();
    if (m_mark >= 0) {
        const QRectF rect = Formula::rectOf(m_layout.box, m_mark);
        if (!rect.isEmpty()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_markColor);
            painter->drawRoundedRect(rect.translated(at).adjusted(-2, -2, 2, 2), 3, 3);
        }
    }
    Formula::paint(painter, m_layout.box, at, m_color);
}

int FormulaItem::sourceAt(qreal x, qreal y) const
{
    return Formula::sourceAt(m_layout.box, QPointF(x, y) - origin());
}
