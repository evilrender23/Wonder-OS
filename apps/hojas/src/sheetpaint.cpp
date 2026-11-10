#include "sheetpaint.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

int SheetPaint::band(const Workbook &book, int sheet, int row, int col)
{
    const int index = book.tableAt(sheet, row, col);
    if (index < 0) {
        return 0;
    }
    const Table &table = book.sheet(sheet).tables.at(index);
    if (row == table.area.r1) {
        return 1;
    }
    if (table.totals && row == table.area.r2) {
        return 3;
    }
    // The bands follow the rows in sight: a filter does not leave two alike together.
    int seen = 0;
    for (int r = table.area.r1 + 1; r <= row; ++r) {
        seen += book.rowHidden(sheet, r) ? 0 : 1;
    }
    return seen % 2 == 0 ? 2 : 4;
}

namespace
{
/// Dark ink on a light fill and the other way round, whatever the theme.
QColor inkOn(const QColor &fill)
{
    const double luminance = 0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF();
    return luminance > 0.6 ? QColor(0x1d, 0x1d, 0x1f) : QColor(0xff, 0xff, 0xff);
}

QColor mixed(const QColor &over, const QColor &under, double amount)
{
    return QColor::fromRgbF(float(under.redF() + (over.redF() - under.redF()) * amount), float(under.greenF() + (over.greenF() - under.greenF()) * amount),
                            float(under.blueF() + (over.blueF() - under.blueF()) * amount));
}
}

void SheetPaint::paint(QPainter *p, Workbook &book, int sheetIndex, int r1, int c1, int r2, int c2, const std::function<double(int)> &colPos,
                       const std::function<double(int)> &rowPos, const Look &look)
{
    if (r2 < r1 || c2 < c1) {
        return;
    }
    const Sheet &sheet = book.sheet(sheetIndex);
    const bool plainSheet = sheet.tables.isEmpty() && sheet.conditionals.isEmpty() && !sheet.hasFilter;
    const double z = look.zoom;

    // Where the columns and rows in sight are, once.
    QList<double> xs, ys;
    xs.reserve(c2 - c1 + 2);
    ys.reserve(r2 - r1 + 2);
    for (int c = c1; c <= c2 + 1; ++c) {
        xs.append(colPos(c));
    }
    for (int r = r1; r <= r2 + 1; ++r) {
        ys.append(rowPos(r));
    }
    const QRectF whole(xs.first(), ys.first(), xs.last() - xs.first(), ys.last() - ys.first());
    const auto colX = [&](int c) { return c >= c1 && c <= c2 + 1 ? xs.at(c - c1) : colPos(c); };
    const auto rowY = [&](int r) { return r >= r1 && r <= r2 + 1 ? ys.at(r - r1) : rowPos(r); };

    struct Shown {
        int row = 0;
        int col = 0;
        QRectF box;
        Style style;
        Value value;
        QString text;
        QString color;
        int band = 0;
        int filter = 0;
        bool merged = false;
    };
    QList<Shown> shown;
    QSet<quint64> mergesSeen;

    const auto collect = [&](int r, int c, bool anchorOnly) {
        Area merged;
        const bool inMerge = !sheet.merges.isEmpty() && book.mergeAt(sheetIndex, r, c, &merged);
        if (inMerge) {
            const quint64 id = (quint64(merged.r1) << 20) | quint64(merged.c1);
            if (mergesSeen.contains(id)) {
                return;
            }
            mergesSeen.insert(id);
            r = merged.r1;
            c = merged.c1;
        } else if (anchorOnly) {
            return;
        }
        const Cell *cell = book.cell(sheetIndex, r, c);
        if (!cell && !inMerge && plainSheet) {
            return;
        }
        Shown s;
        s.row = r;
        s.col = c;
        s.merged = inMerge;
        s.box = inMerge ? QRectF(colX(merged.c1), rowY(merged.r1), colX(merged.c2 + 1) - colX(merged.c1), rowY(merged.r2 + 1) - rowY(merged.r1))
                        : QRectF(colX(c), rowY(r), colX(c + 1) - colX(c), rowY(r + 1) - rowY(r));
        if (s.box.width() <= 0 || s.box.height() <= 0) {
            return;
        }
        s.band = sheet.tables.isEmpty() ? 0 : band(book, sheetIndex, r, c);
        if (look.controls && sheet.hasFilter && r == sheet.filterArea.r1 && c >= sheet.filterArea.c1 && c <= sheet.filterArea.c2) {
            s.filter = sheet.filterAllowed.contains(c - sheet.filterArea.c1) ? 2 : 1;
        }
        if (!cell && !inMerge && s.band == 0 && s.filter == 0 && sheet.conditionals.isEmpty()) {
            return;
        }
        s.style = cell || !sheet.conditionals.isEmpty() ? book.effectiveStyle(sheetIndex, r, c) : Style();
        if (cell) {
            s.value = cell->formula ? book.value(sheetIndex, r, c) : cell->value;
            if (!s.value.isBool() && !s.value.isEmpty()) {
                s.text = book.display(sheetIndex, r, c, &s.color);
            }
        }
        shown.append(s);
    };
    for (int r = r1; r <= r2; ++r) {
        if (ys.at(r - r1 + 1) - ys.at(r - r1) <= 0) {
            continue;
        }
        for (int c = c1; c <= c2; ++c) {
            collect(r, c, false);
        }
    }

    p->save();
    p->setClipRect(whole, Qt::IntersectClip);

    // 1. Fills.
    const auto fillOf = [&](const Shown &s) {
        if (!s.style.fill.isEmpty()) {
            return QColor(s.style.fill);
        }
        switch (s.band) {
        case 1:
            return look.tint;
        case 2:
            return mixed(look.tint, look.paper, look.dark ? 0.16 : 0.08);
        case 3:
            return mixed(look.tint, look.paper, look.dark ? 0.26 : 0.15);
        default:
            return QColor();
        }
    };
    for (const Shown &s : std::as_const(shown)) {
        const QColor fill = fillOf(s);
        if (fill.isValid()) {
            p->fillRect(s.box, fill);
        }
    }

    // 2. The lines between cells.
    if (look.grid) {
        p->setPen(Qt::NoPen);
        for (int c = c1; c <= c2; ++c) {
            const double x = xs.at(c - c1 + 1);
            if (x > xs.at(c - c1)) {
                p->fillRect(QRectF(x - 1, whole.top(), 1, whole.height()), look.line);
            }
        }
        for (int r = r1; r <= r2; ++r) {
            const double y = ys.at(r - r1 + 1);
            if (y > ys.at(r - r1)) {
                p->fillRect(QRectF(whole.left(), y - 1, whole.width(), 1), look.line);
            }
        }
        // A merge is one cell: no lines through it.
        for (const Shown &s : std::as_const(shown)) {
            if (s.merged) {
                const QColor fill = fillOf(s);
                p->fillRect(s.box.adjusted(0, 0, -1, -1), fill.isValid() ? fill : look.paper);
            }
        }
    }

    // 3. Text, casillas and the buttons of a filter.
    for (const Shown &s : std::as_const(shown)) {
        const QColor fill = fillOf(s);
        QColor ink = look.ink;
        if (!s.style.color.isEmpty()) {
            ink = QColor(s.style.color);
        } else if (!s.color.isEmpty()) {
            ink = QColor(s.color);
        } else if (s.value.isError()) {
            ink = look.error;
        } else if (s.band == 1 && s.style.fill.isEmpty()) {
            ink = QColor(0xff, 0xff, 0xff);
        } else if (!s.style.fill.isEmpty()) {
            ink = inkOn(fill);
        }
        double room = s.filter > 0 ? 20 * z : 0;
        if (s.filter > 0) {
            // A chevron in a small round button; filled when the column is filtered.
            const QRectF button(s.box.right() - 19 * z, s.box.center().y() - 8 * z, 16 * z, 16 * z);
            p->setRenderHint(QPainter::Antialiasing, true);
            QColor back = ink;
            back.setAlphaF(s.filter == 2 ? 0.9f : 0.16f);
            p->setPen(Qt::NoPen);
            p->setBrush(back);
            p->drawRoundedRect(button, 4 * z, 4 * z);
            QColor mark = s.filter == 2 ? (fill.isValid() ? fill : look.paper) : ink;
            p->setPen(QPen(mark, 1.5 * z, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p->setBrush(Qt::NoBrush);
            QPainterPath chevron;
            chevron.moveTo(button.center().x() - 3.5 * z, button.center().y() - 1.5 * z);
            chevron.lineTo(button.center().x(), button.center().y() + 2 * z);
            chevron.lineTo(button.center().x() + 3.5 * z, button.center().y() - 1.5 * z);
            p->drawPath(chevron);
            p->setRenderHint(QPainter::Antialiasing, false);
        }
        if (s.value.isBool()) {
            // A real casilla: a rounded square, ticked when true.
            const double side = qMin(15.0 * z, qMin(s.box.width(), s.box.height()) - 4);
            if (side < 6) {
                continue;
            }
            const int align = s.style.align;
            const double x = align == 1 ? s.box.left() + 6 * z : align == 3 ? s.box.right() - 6 * z - side - room : s.box.center().x() - side / 2 - room / 2;
            const QRectF square(x, s.box.center().y() - side / 2, side, side);
            p->setRenderHint(QPainter::Antialiasing, true);
            if (s.value.truth()) {
                p->setPen(Qt::NoPen);
                p->setBrush(look.tint);
                p->drawRoundedRect(square, side * 0.26, side * 0.26);
                p->setPen(QPen(QColor(0xff, 0xff, 0xff), qMax(1.4, side * 0.13), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                QPainterPath tick;
                tick.moveTo(square.left() + side * 0.26, square.top() + side * 0.53);
                tick.lineTo(square.left() + side * 0.43, square.top() + side * 0.70);
                tick.lineTo(square.left() + side * 0.75, square.top() + side * 0.32);
                p->setBrush(Qt::NoBrush);
                p->drawPath(tick);
            } else {
                QColor edge = fill.isValid() ? inkOn(fill) : look.dim;
                edge.setAlphaF(0.7f);
                p->setPen(QPen(edge, qMax(1.0, 1.3 * z)));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(square.adjusted(0.65, 0.65, -0.65, -0.65), side * 0.26, side * 0.26);
            }
            p->setRenderHint(QPainter::Antialiasing, false);
            continue;
        }
        if (s.text.isEmpty()) {
            continue;
        }
        QFont font = p->font();
        font.setPixelSize(qMax(4, int(std::round((s.style.size > 0 ? s.style.size * 96.0 / 72.0 : 13.0) * z))));
        font.setBold(s.style.bold || s.band == 1 || s.band == 3);
        font.setItalic(s.style.italic);
        font.setUnderline(s.style.underline);
        font.setStrikeOut(s.style.strike);
        p->setFont(font);
        const QFontMetricsF fm(font);
        const double pad = 6 * z;
        int side = s.style.align;
        if (side == 0) {
            side = s.value.isNumber() ? 3 : (s.value.isError() ? 2 : 1);
        }
        QString text = s.text;
        QRectF box = s.box.adjusted(pad, 1, -pad - room, -1);
        const double needed = fm.horizontalAdvance(text);
        if (needed > box.width() && !s.style.wrap) {
            if (s.value.isNumber()) {
                // A number that does not fit must not read as another one.
                const int count = qMax(1, int(box.width() / qMax(1.0, fm.horizontalAdvance(QLatin1Char('#')))));
                text = QString(count, QLatin1Char('#'));
            } else if (side == 1 && !s.merged && s.filter == 0) {
                // Text runs over the empty cells beside it.
                int next = s.col + 1;
                double reach = s.box.right();
                while (reach - s.box.left() - 2 * pad < needed && next < Limits::Cols && next <= c2 + 40) {
                    const Cell *beside = book.cell(sheetIndex, s.row, next);
                    if ((beside && (!beside->value.isEmpty() || beside->formula)) || book.mergeAt(sheetIndex, s.row, next, nullptr)
                        || (!sheet.tables.isEmpty() && book.tableAt(sheetIndex, s.row, next) != book.tableAt(sheetIndex, s.row, s.col))) {
                        break;
                    }
                    reach = colX(next + 1);
                    ++next;
                }
                if (reach > s.box.right()) {
                    if (look.grid) {
                        // The lines it runs over go, as on paper.
                        const QColor under = fill.isValid() ? fill : look.paper;
                        p->fillRect(QRectF(s.box.right() - 1, s.box.top(), reach - s.box.right(), s.box.height() - 1), under);
                    }
                    box.setRight(reach - pad);
                }
            }
        }
        int flags = (side == 3 ? Qt::AlignRight : side == 2 ? Qt::AlignHCenter : Qt::AlignLeft)
            | (s.style.valign == 2 ? Qt::AlignTop : s.style.valign == 1 || s.box.height() <= 30 * z ? Qt::AlignVCenter : Qt::AlignBottom);
        if (s.style.wrap) {
            flags |= Qt::TextWordWrap;
        }
        if (flags & Qt::AlignBottom) {
            box.adjust(0, 0, 0, -3 * z);
        } else if (flags & Qt::AlignTop) {
            box.adjust(0, 3 * z, 0, 0);
        }
        p->save();
        p->setClipRect(box.adjusted(-1, -1, 1, 1 + (flags & Qt::AlignBottom ? 3 * z : 0)), Qt::IntersectClip);
        p->setPen(ink);
        p->drawText(box, flags, text);
        p->restore();
    }

    // 4. Borders, over everything.
    for (const Shown &s : std::as_const(shown)) {
        const bool totals = s.band == 3;
        if (s.style.borders == 0 && !totals) {
            continue;
        }
        const QColor color = s.style.borderColor.isEmpty() ? look.ink : QColor(s.style.borderColor);
        const double w = qMax(1.0, std::round(z));
        if (s.style.borders & 1) {
            p->fillRect(QRectF(s.box.left() - (s.box.left() > whole.left() ? 1 : 0), s.box.top() - 1, w, s.box.height() + 1), color);
        }
        if (s.style.borders & 2) {
            p->fillRect(QRectF(s.box.left() - 1, s.box.top() - (s.box.top() > whole.top() ? 1 : 0), s.box.width() + 1, w), color);
        }
        if (s.style.borders & 4) {
            p->fillRect(QRectF(s.box.right() - 1, s.box.top() - 1, w, s.box.height() + 1), color);
        }
        if (s.style.borders & 8) {
            p->fillRect(QRectF(s.box.left() - 1, s.box.bottom() - 1, s.box.width() + 1, w), color);
        }
        if (totals) {
            p->fillRect(QRectF(s.box.left(), s.box.top() - 1, s.box.width(), w), look.tint);
        }
    }
    p->restore();
}
