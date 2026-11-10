#pragma once

#include "layout.h"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

/// A formula, drawn: the live preview and every equation of the list.
class FormulaItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString latex READ latex WRITE setLatex NOTIFY latexChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    /// Pixels to the em.
    Q_PROPERTY(qreal size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(QString family READ family WRITE setFamily NOTIFY familyChanged)
    /// Wider than this, the formula is drawn smaller so that all of it shows; 0 for no limit.
    Q_PROPERTY(qreal fit READ fit WRITE setFit NOTIFY fitChanged)
    /// A position in the source: what it belongs to is marked. Negative for nothing.
    Q_PROPERTY(int mark READ mark WRITE setMark NOTIFY markChanged)
    Q_PROPERTY(QColor markColor READ markColor WRITE setMarkColor NOTIFY markChanged)
    /// How much smaller than asked it is drawn (1: as asked).
    Q_PROPERTY(qreal shrink READ shrink NOTIFY laidOut)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY laidOut)

public:
    explicit FormulaItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QString latex() const { return m_latex; }
    void setLatex(const QString &latex);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    qreal size() const { return m_size; }
    void setSize(qreal size);
    QString family() const { return m_family; }
    void setFamily(const QString &family);
    qreal fit() const { return m_fit; }
    void setFit(qreal fit);
    int mark() const { return m_mark; }
    void setMark(int mark);
    QColor markColor() const { return m_markColor; }
    void setMarkColor(const QColor &color);
    qreal shrink() const { return m_shrink; }
    int errorCount() const { return m_layout.errors.size(); }

    /// The position in the source of what is drawn at a point of the item, or -1.
    Q_INVOKABLE int sourceAt(qreal x, qreal y) const;

Q_SIGNALS:
    void latexChanged();
    void colorChanged();
    void sizeChanged();
    void familyChanged();
    void fitChanged();
    void markChanged();
    void laidOut();

private:
    void relayout();
    QPointF origin() const;

    QString m_latex;
    QColor m_color = Qt::black;
    qreal m_size = 32;
    QString m_family;
    qreal m_fit = 0;
    int m_mark = -1;
    QColor m_markColor = QColor(91, 91, 214, 40);
    qreal m_shrink = 1;
    Layout m_layout;
};
