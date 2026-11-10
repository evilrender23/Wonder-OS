#include "griditem.h"

#include "app.h"
#include "providers.h"
#include "sheetpaint.h"

#include <QPainter>
#include <cmath>

GridItem::GridItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    connect(this, &GridItem::changed, this, [this] { update(); });
}

double GridItem::headerWidth() const
{
    Book *book = App::instance() ? App::instance()->book() : nullptr;
    if (!m_headers || !book) {
        return 0;
    }
    // Wider when the rows in sight run to five digits or more.
    return book->rows() > 99999 ? 62 : book->rows() > 9999 ? 54 : 46;
}

double GridItem::headerHeight() const
{
    return m_headers ? 24 : 0;
}

void GridItem::paint(QPainter *p)
{
    Book *book = App::instance() ? App::instance()->book() : nullptr;
    p->fillRect(QRectF(0, 0, width(), height()), m_paper);
    if (!book || !book->isOpen()) {
        return;
    }
    const double hw = headerWidth();
    const double hh = headerHeight();
    const int fr = book->freezeRows();
    const int fc = book->freezeCols();
    const double fw = book->colPos(fc);
    const double fh = book->rowPos(fr);
    const double bodyW = width() - hw;
    const double bodyH = height() - hh;
    if (bodyW <= 0 || bodyH <= 0) {
        return;
    }

    SheetPaint::Look look;
    look.paper = m_paper;
    look.ink = m_ink;
    look.dim = m_dim;
    look.line = m_line;
    look.tint = m_tint;
    look.dark = m_dark;
    look.error = m_dark ? QColor(0xff, 0x6b, 0x61) : QColor(0xd7, 0x00, 0x15);
    look.zoom = book->zoom();

    QFont font = p->font();
    font.setPixelSize(13);
    p->setFont(font);

    // The rows and columns of the pane that scrolls.
    const int firstCol = qMax(fc, book->colAt(fw + m_scrollX));
    const int lastCol = qMin(book->cols() - 1, book->colAt(m_scrollX + bodyW));
    const int firstRow = qMax(fr, book->rowAt(fh + m_scrollY));
    const int lastRow = qMin(book->rows() - 1, book->rowAt(m_scrollY + bodyH));

    struct Pane {
        int r1, c1, r2, c2;
        double dx, dy;
        QRectF clip;
    };
    const QList<Pane> panes = {
        {firstRow, firstCol, lastRow, lastCol, -m_scrollX, -m_scrollY, QRectF(hw + fw, hh + fh, bodyW - fw, bodyH - fh)},
        {0, firstCol, fr - 1, lastCol, -m_scrollX, 0, QRectF(hw + fw, hh, bodyW - fw, fh)},
        {firstRow, 0, lastRow, fc - 1, 0, -m_scrollY, QRectF(hw, hh + fh, fw, bodyH - fh)},
        {0, 0, fr - 1, fc - 1, 0, 0, QRectF(hw, hh, fw, fh)},
    };
    Workbook &workbook = book->workbook();
    for (const Pane &pane : panes) {
        if (pane.r2 < pane.r1 || pane.c2 < pane.c1 || pane.clip.width() <= 0 || pane.clip.height() <= 0) {
            continue;
        }
        p->save();
        p->setClipRect(pane.clip);
        SheetPaint::paint(p, workbook, book->sheet(), pane.r1, pane.c1, pane.r2, pane.c2,
                          [&](int c) { return hw + book->colPos(c) + pane.dx; }, [&](int r) { return hh + book->rowPos(r) + pane.dy; }, look);
        p->restore();
    }

    // Where the frozen panes end.
    const QColor edge = m_dark ? QColor(255, 255, 255, 70) : QColor(0, 0, 0, 60);
    if (fc > 0) {
        p->fillRect(QRectF(hw + fw - 1, hh, 1, bodyH), edge);
    }
    if (fr > 0) {
        p->fillRect(QRectF(hw, hh + fh - 1, bodyW, 1), edge);
    }
    if (!m_headers) {
        return;
    }

    // --- Headers
    const QColor border = m_dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 28);
    QColor lit = m_tint;
    lit.setAlphaF(m_dark ? 0.30f : 0.16f);
    const QColor litInk = m_dark ? m_tint.lighter(125) : m_tint.darker(125);
    QFont small = font;
    small.setPixelSize(11);
    QFont smallBold = small;
    smallBold.setWeight(QFont::DemiBold);

    p->fillRect(QRectF(0, 0, width(), hh), m_header);
    p->fillRect(QRectF(0, 0, hw, height()), m_header);

    const auto columns = [&](int from, int to, double dx, const QRectF &clip) {
        if (to < from || clip.width() <= 0) {
            return;
        }
        p->save();
        p->setClipRect(clip);
        for (int c = from; c <= to; ++c) {
            const double x = hw + book->colPos(c) + dx;
            const double w = book->colWidth(c);
            if (w <= 0) {
                continue;
            }
            const bool on = c >= m_c1 && c <= m_c2;
            if (on) {
                p->fillRect(QRectF(x, 0, w, hh), lit);
                p->fillRect(QRectF(x, hh - 2, w, 2), m_tint);
            }
            p->fillRect(QRectF(x + w - 1, 5, 1, hh - 10), border);
            p->setFont(on ? smallBold : small);
            p->setPen(on ? litInk : m_dim);
            const QString type = w >= 64 ? book->columnType(c) : QString();
            if (!type.isEmpty()) {
                // What the column holds, beside its letter.
                const QString glyph = type == QLatin1String("number") ? QStringLiteral("hash") : type == QLatin1String("date") ? QStringLiteral("calendar")
                    : type == QLatin1String("check") ? QStringLiteral("square-check") : QStringLiteral("type");
                const double dpr = p->device()->devicePixelRatioF();
                QColor faint = on ? litInk : m_dim;
                faint.setAlphaF(0.75f);
                QPixmap pixmap = SymbolProvider::render(glyph, faint, int(std::ceil(11 * dpr)));
                pixmap.setDevicePixelRatio(dpr);
                const double textWidth = QFontMetricsF(p->font()).horizontalAdvance(book->columnName(c));
                const double start = x + (w - textWidth - 15) / 2;
                p->drawPixmap(QPointF(start, (hh - 11) / 2), pixmap);
                p->drawText(QRectF(start + 15, 0, textWidth + 2, hh), Qt::AlignLeft | Qt::AlignVCenter, book->columnName(c));
            } else {
                p->drawText(QRectF(x, 0, w, hh), Qt::AlignCenter, book->columnName(c));
            }
        }
        p->restore();
    };
    columns(firstCol, lastCol, -m_scrollX, QRectF(hw + fw, 0, bodyW - fw, hh));
    columns(0, fc - 1, 0, QRectF(hw, 0, fw, hh));

    const auto rows = [&](int from, int to, double dy, const QRectF &clip) {
        if (to < from || clip.height() <= 0) {
            return;
        }
        p->save();
        p->setClipRect(clip);
        for (int r = from; r <= to; ++r) {
            const double y = hh + book->rowPos(r) + dy;
            const double h = book->rowHeight(r);
            if (h <= 0) {
                continue;
            }
            const bool on = r >= m_r1 && r <= m_r2;
            if (on) {
                p->fillRect(QRectF(0, y, hw, h), lit);
                p->fillRect(QRectF(hw - 2, y, 2, h), m_tint);
            }
            p->fillRect(QRectF(6, y + h - 1, hw - 12, 1), border);
            p->setFont(on ? smallBold : small);
            p->setPen(on ? litInk : m_dim);
            p->drawText(QRectF(0, y, hw, h), Qt::AlignCenter, QString::number(r + 1));
        }
        p->restore();
    };
    rows(firstRow, lastRow, -m_scrollY, QRectF(0, hh + fh, hw, bodyH - fh));
    rows(0, fr - 1, 0, QRectF(0, hh, hw, fh));

    p->fillRect(QRectF(0, 0, hw, hh), m_header);
    p->fillRect(QRectF(0, hh - 1, width(), 1), border);
    p->fillRect(QRectF(hw - 1, 0, 1, height()), border);
}
