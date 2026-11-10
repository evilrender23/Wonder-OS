#pragma once

#include "editor.h"

#include <QQuickPaintedItem>

/// The endless canvas: paints the part of the drawing in view and what goes
/// over it (grips, guides, the rubber band), and hands the pointer to the editor.
class Canvas : public QQuickPaintedItem
{
    Q_OBJECT
    // QtQuick has a Canvas of its own.
    QML_NAMED_ELEMENT(Board)
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QColor paper MEMBER m_paper NOTIFY lookChanged)
    Q_PROPERTY(QColor dots MEMBER m_dots NOTIFY lookChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY lookChanged)
    Q_PROPERTY(QColor chrome MEMBER m_chrome NOTIFY lookChanged)

public:
    explicit Canvas(QQuickItem *parent = nullptr);

    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    void paint(QPainter *painter) override;

Q_SIGNALS:
    void editorChanged();
    void lookChanged();
    /// The other button, over the canvas: where a menu goes.
    void menuRequested(double x, double y);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void applyCursor();

    Editor *m_editor = nullptr;
    QColor m_paper = QColor(QStringLiteral("#f4f5f7"));
    QColor m_dots = QColor(0, 0, 0, 40);
    QColor m_accent = QColor(QStringLiteral("#0f9aa8"));
    QColor m_chrome = QColor(0, 0, 0, 120);
};

/// The whole drawing, small, with the part in view marked; a press goes there.
class Minimap : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Editor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QColor accent MEMBER m_accent NOTIFY lookChanged)
    Q_PROPERTY(QColor paper MEMBER m_paper NOTIFY lookChanged)

public:
    explicit Minimap(QQuickItem *parent = nullptr);

    Editor *editor() const { return m_editor; }
    void setEditor(Editor *editor);
    void paint(QPainter *painter) override;

Q_SIGNALS:
    void editorChanged();
    void lookChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    /// What is shown: everything, and the view, with a margin.
    QRectF world() const;
    QTransform toItem() const;
    void go(const QPointF &at);

    Editor *m_editor = nullptr;
    QColor m_accent = QColor(QStringLiteral("#0f9aa8"));
    QColor m_paper = QColor(QStringLiteral("#f4f5f7"));
};
