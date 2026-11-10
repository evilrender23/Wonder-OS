#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

/// The sheet in sight: its cells, its frozen panes and its headers, painted
/// in one go. What is drawn over them (selection, editor, charts) is QML's.
class GridItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    /// How far the cells that scroll have moved, in pixels.
    Q_PROPERTY(double scrollX MEMBER m_scrollX NOTIFY changed)
    Q_PROPERTY(double scrollY MEMBER m_scrollY NOTIFY changed)
    /// Bound to the document's revision, so a change paints again.
    Q_PROPERTY(int revision MEMBER m_revision NOTIFY changed)
    Q_PROPERTY(bool dark MEMBER m_dark NOTIFY changed)
    Q_PROPERTY(bool headers MEMBER m_headers NOTIFY changed)
    Q_PROPERTY(QColor paper MEMBER m_paper NOTIFY changed)
    Q_PROPERTY(QColor ink MEMBER m_ink NOTIFY changed)
    Q_PROPERTY(QColor dim MEMBER m_dim NOTIFY changed)
    Q_PROPERTY(QColor line MEMBER m_line NOTIFY changed)
    Q_PROPERTY(QColor tint MEMBER m_tint NOTIFY changed)
    Q_PROPERTY(QColor headerColor MEMBER m_header NOTIFY changed)
    /// The selection, to light up its headers.
    Q_PROPERTY(int selR1 MEMBER m_r1 NOTIFY changed)
    Q_PROPERTY(int selC1 MEMBER m_c1 NOTIFY changed)
    Q_PROPERTY(int selR2 MEMBER m_r2 NOTIFY changed)
    Q_PROPERTY(int selC2 MEMBER m_c2 NOTIFY changed)
    /// The size of the headers: constant, for the QML over the grid to share.
    Q_PROPERTY(double headerWidth READ headerWidth NOTIFY changed)
    Q_PROPERTY(double headerHeight READ headerHeight NOTIFY changed)

public:
    explicit GridItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;
    double headerWidth() const;
    double headerHeight() const;

Q_SIGNALS:
    void changed();

private:
    double m_scrollX = 0;
    double m_scrollY = 0;
    int m_revision = 0;
    bool m_dark = false;
    bool m_headers = true;
    QColor m_paper = Qt::white;
    QColor m_ink = Qt::black;
    QColor m_dim = Qt::gray;
    QColor m_line = Qt::lightGray;
    QColor m_tint = Qt::darkGreen;
    QColor m_header = Qt::lightGray;
    int m_r1 = 0;
    int m_c1 = 0;
    int m_r2 = 0;
    int m_c2 = 0;
};
