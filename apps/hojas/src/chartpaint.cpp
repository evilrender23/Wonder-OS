#include "chartpaint.h"

#include "numfmt.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

QStringList ChartPaint::palette()
{
    return {QStringLiteral("#30b566"), QStringLiteral("#0a84ff"), QStringLiteral("#ff9f0a"), QStringLiteral("#bf5af2"),
            QStringLiteral("#ff375f"), QStringLiteral("#30b0c7"), QStringLiteral("#e6b800"), QStringLiteral("#8e8e93")};
}

namespace
{
/// A step that reads well (1, 2, 2.5 or 5 times a power of ten) for about `count` ticks.
double niceStep(double span, int count)
{
    if (!(span > 0)) {
        return 1;
    }
    const double raw = span / qMax(1, count);
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double unit = raw / power;
    const double nice = unit <= 1 ? 1 : unit <= 2 ? 2 : unit <= 2.5 ? 2.5 : unit <= 5 ? 5 : 10;
    return nice * power;
}

QString label(double value)
{
    const double size = std::fabs(value);
    if (size >= 1e9) {
        return NumFmt::general(std::round(value / 1e7) / 100) + QStringLiteral(" MM");
    }
    if (size >= 1e6) {
        return NumFmt::general(std::round(value / 1e4) / 100) + QStringLiteral(" M");
    }
    if (size >= 1000 && value == std::round(value)) {
        return NumFmt::format(Value(value), QStringLiteral("#,##0"));
    }
    return NumFmt::general(std::round(value * 1e4) / 1e4);
}

struct Scale {
    double low = 0;
    double high = 1;
    double step = 1;
    double at(double value, double from, double to) const { return from + (value - low) / (high - low) * (to - from); }
};

Scale scaleFor(double low, double high, bool fromZero)
{
    if (!(low <= high)) {
        low = 0;
        high = 1;
    }
    if (fromZero) {
        low = qMin(low, 0.0);
        high = qMax(high, 0.0);
    }
    if (low == high) {
        high = low + 1;
    }
    Scale s;
    s.step = niceStep(high - low, 5);
    s.low = std::floor(low / s.step) * s.step;
    s.high = std::ceil(high / s.step) * s.step;
    if (s.high == s.low) {
        s.high = s.low + s.step;
    }
    return s;
}
}

void ChartPaint::paint(QPainter *p, const QRectF &rect, const Chart &chart, const ChartData &data, bool dark)
{
    const QColor ink = dark ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x1d, 0x1d, 0x1f);
    const QColor dim = dark ? QColor(0xa0, 0xa0, 0xa5) : QColor(0x6e, 0x6e, 0x73);
    const QColor grid = dark ? QColor(255, 255, 255, 28) : QColor(0, 0, 0, 22);
    const QStringList colors = palette();
    const auto colorOf = [&colors](int i) { return QColor(colors.at(i % colors.size())); };

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QFont font = p->font();
    font.setPixelSize(11);
    QFont titleFont = font;
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    const QFontMetricsF fm(font);

    QRectF area = rect.adjusted(14, 12, -14, -12);
    if (!chart.title.isEmpty()) {
        p->setFont(titleFont);
        p->setPen(ink);
        p->drawText(QRectF(area.left(), area.top(), area.width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
                    QFontMetricsF(titleFont).elidedText(chart.title, Qt::ElideRight, area.width()));
        area.setTop(area.top() + 28);
    }
    p->setFont(font);

    const int seriesCount = data.series.size();
    const int points = data.categories.size();
    const bool pie = chart.type == QLatin1String("pie");
    if (seriesCount == 0 || points == 0) {
        p->setPen(dim);
        p->drawText(area, Qt::AlignCenter, QStringLiteral("Sin datos numéricos en el rango"));
        p->restore();
        return;
    }

    // The legend: the series, or the slices of a pie.
    const QStringList names = pie ? data.categories : data.seriesNames;
    if (chart.legend && (pie || seriesCount > 1 || !names.value(0).isEmpty())) {
        double x = area.left();
        double y = area.bottom() - 14;
        // Laid out first to know how many lines it takes.
        QList<QPair<QPointF, int>> places;
        double lineY = 0;
        for (int i = 0; i < names.size() && i < 24; ++i) {
            const QString text = names.at(i).isEmpty() ? QStringLiteral("Serie %1").arg(i + 1) : names.at(i);
            const double width = 16 + qMin(fm.horizontalAdvance(text), 140.0) + 14;
            if (x + width > area.right() && x > area.left()) {
                x = area.left();
                lineY += 16;
            }
            places.append({QPointF(x, lineY), i});
            x += width;
        }
        const double height = lineY + 16;
        y = area.bottom() - height;
        for (const auto &place : std::as_const(places)) {
            const QString text = names.at(place.second).isEmpty() ? QStringLiteral("Serie %1").arg(place.second + 1) : names.at(place.second);
            const QPointF at(place.first.x(), y + place.first.y());
            p->setPen(Qt::NoPen);
            p->setBrush(colorOf(place.second));
            p->drawRoundedRect(QRectF(at.x(), at.y() + 3, 10, 10), 3, 3);
            p->setPen(dim);
            p->drawText(QRectF(at.x() + 16, at.y(), 140, 16), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(text, Qt::ElideRight, 140));
        }
        area.setBottom(y - 8);
    }

    if (pie) {
        const QList<double> &values = data.series.at(0);
        double total = 0;
        for (double v : values) {
            if (v > 0) {
                total += v;
            }
        }
        const double side = qMin(area.width(), area.height());
        const QRectF circle(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
        if (total <= 0 || side < 20) {
            p->setPen(dim);
            p->drawText(area, Qt::AlignCenter, QStringLiteral("Sin valores positivos"));
            p->restore();
            return;
        }
        double angle = 90 * 16;
        const QColor gap = dark ? QColor(0x1e, 0x1e, 0x1e) : QColor(0xff, 0xff, 0xff);
        for (int i = 0; i < values.size(); ++i) {
            if (!(values.at(i) > 0)) {
                continue;
            }
            const double span = -values.at(i) / total * 360 * 16;
            p->setPen(QPen(gap, 1.5));
            p->setBrush(colorOf(i));
            p->drawPie(circle, int(std::round(angle)), int(std::round(span)));
            const double share = values.at(i) / total;
            if (share >= 0.06 && side > 110) {
                const double middle = (angle + span / 2) / 16 * M_PI / 180;
                const QPointF at(circle.center().x() + std::cos(middle) * side * 0.33, circle.center().y() - std::sin(middle) * side * 0.33);
                p->setPen(Qt::white);
                p->drawText(QRectF(at.x() - 24, at.y() - 8, 48, 16), Qt::AlignCenter, QString::number(int(std::round(share * 100))) + QLatin1Char('%'));
            }
            angle += span;
        }
        p->restore();
        return;
    }

    const bool bars = chart.type == QLatin1String("bar");
    const bool scatter = chart.type == QLatin1String("scatter");
    const bool columns = chart.type == QLatin1String("column");
    const bool filled = chart.type == QLatin1String("area");

    double low = qInf(), high = -qInf();
    for (const QList<double> &series : data.series) {
        for (double v : series) {
            if (!std::isnan(v)) {
                low = qMin(low, v);
                high = qMax(high, v);
            }
        }
    }
    const Scale values = scaleFor(low, high, !scatter && chart.type != QLatin1String("line"));
    Scale across;
    if (scatter) {
        double xLow = qInf(), xHigh = -qInf();
        for (int i = 0; i < data.xs.size(); ++i) {
            const double v = std::isnan(data.xs.at(i)) ? i + 1 : data.xs.at(i);
            xLow = qMin(xLow, v);
            xHigh = qMax(xHigh, v);
        }
        across = scaleFor(xLow, xHigh, false);
    }

    // Room for the numbers of the value axis and the names of the other.
    double widest = 0;
    QStringList ticks;
    for (double v = values.low; v <= values.high + values.step / 2; v += values.step) {
        ticks.append(label(v));
        widest = qMax(widest, fm.horizontalAdvance(ticks.last()));
    }
    QRectF plot = area;
    if (bars) {
        double names = 0;
        for (const QString &category : data.categories) {
            names = qMax(names, fm.horizontalAdvance(category));
        }
        plot.setLeft(plot.left() + qMin(names, area.width() * 0.3) + 8);
        plot.setBottom(plot.bottom() - 18);
        plot.setRight(plot.right() - 10);
    } else {
        plot.setLeft(plot.left() + widest + 8);
        plot.setBottom(plot.bottom() - 18);
        plot.setTop(plot.top() + 6);
        plot.setRight(plot.right() - 6);
    }
    if (plot.width() < 20 || plot.height() < 20) {
        p->restore();
        return;
    }

    // The grid and the numbers.
    for (int i = 0; i < ticks.size(); ++i) {
        const double v = values.low + i * values.step;
        if (bars) {
            const double x = values.at(v, plot.left(), plot.right());
            p->setPen(QPen(v == 0 ? dim : grid, 1));
            p->drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p->setPen(dim);
            p->drawText(QRectF(x - 40, plot.bottom() + 3, 80, 14), Qt::AlignCenter, ticks.at(i));
        } else {
            const double y = values.at(v, plot.bottom(), plot.top());
            p->setPen(QPen(v == 0 ? dim : grid, 1));
            p->drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p->setPen(dim);
            p->drawText(QRectF(area.left(), y - 8, plot.left() - area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, ticks.at(i));
        }
    }

    if (scatter) {
        for (double v = across.low; v <= across.high + across.step / 2; v += across.step) {
            const double x = across.at(v, plot.left(), plot.right());
            p->setPen(dim);
            p->drawText(QRectF(x - 40, plot.bottom() + 3, 80, 14), Qt::AlignCenter, label(v));
        }
        for (int s = 0; s < seriesCount; ++s) {
            p->setPen(Qt::NoPen);
            QColor color = colorOf(s);
            color.setAlphaF(0.85);
            p->setBrush(color);
            for (int i = 0; i < data.series.at(s).size(); ++i) {
                const double v = data.series.at(s).at(i);
                if (std::isnan(v)) {
                    continue;
                }
                const double xv = i < data.xs.size() && !std::isnan(data.xs.at(i)) ? data.xs.at(i) : i + 1;
                p->drawEllipse(QPointF(across.at(xv, plot.left(), plot.right()), values.at(v, plot.bottom(), plot.top())), 3.5, 3.5);
            }
        }
        p->restore();
        return;
    }

    const double slot = (bars ? plot.height() : plot.width()) / points;
    // The names of the categories, as many as fit.
    if (bars) {
        const int every = qMax(1, int(std::ceil(14.0 / slot)));
        for (int i = 0; i < points; i += every) {
            p->setPen(dim);
            const QRectF box(area.left(), plot.top() + i * slot, plot.left() - area.left() - 6, slot);
            p->drawText(box, Qt::AlignRight | Qt::AlignVCenter, fm.elidedText(data.categories.at(i), Qt::ElideRight, box.width()));
        }
    } else {
        double longest = 0;
        for (const QString &category : data.categories) {
            longest = qMax(longest, fm.horizontalAdvance(category));
        }
        const double room = qMin(longest + 10, 90.0);
        const int every = qMax(1, int(std::ceil(room / slot)));
        for (int i = 0; i < points; i += every) {
            p->setPen(dim);
            const double width = slot * every;
            const QRectF box(plot.left() + i * slot + (slot - width) / 2, plot.bottom() + 3, width, 14);
            p->drawText(box, Qt::AlignCenter, fm.elidedText(data.categories.at(i), Qt::ElideRight, width - 4));
        }
    }

    const double zero = qBound(values.low, 0.0, values.high);
    if (columns || bars) {
        const double group = slot * 0.72;
        const double each = group / seriesCount;
        for (int s = 0; s < seriesCount; ++s) {
            p->setPen(Qt::NoPen);
            p->setBrush(colorOf(s));
            for (int i = 0; i < data.series.at(s).size() && i < points; ++i) {
                const double v = data.series.at(s).at(i);
                if (std::isnan(v)) {
                    continue;
                }
                const double radius = qMin(3.0, each / 3);
                if (bars) {
                    const double x0 = values.at(zero, plot.left(), plot.right());
                    const double x1 = values.at(v, plot.left(), plot.right());
                    const double y = plot.top() + i * slot + (slot - group) / 2 + s * each;
                    p->drawRoundedRect(QRectF(qMin(x0, x1), y + 0.5, std::fabs(x1 - x0), qMax(1.0, each - 1)), radius, radius);
                } else {
                    const double y0 = values.at(zero, plot.bottom(), plot.top());
                    const double y1 = values.at(v, plot.bottom(), plot.top());
                    const double x = plot.left() + i * slot + (slot - group) / 2 + s * each;
                    p->drawRoundedRect(QRectF(x + 0.5, qMin(y0, y1), qMax(1.0, each - 1), std::fabs(y1 - y0)), radius, radius);
                }
            }
        }
    } else {
        for (int s = 0; s < seriesCount; ++s) {
            QList<QPolygonF> runs;
            QPolygonF run;
            for (int i = 0; i < data.series.at(s).size() && i < points; ++i) {
                const double v = data.series.at(s).at(i);
                if (std::isnan(v)) {
                    if (!run.isEmpty()) {
                        runs.append(run);
                        run.clear();
                    }
                    continue;
                }
                run.append(QPointF(plot.left() + (i + 0.5) * slot, values.at(v, plot.bottom(), plot.top())));
            }
            if (!run.isEmpty()) {
                runs.append(run);
            }
            const QColor color = colorOf(s);
            for (const QPolygonF &line : std::as_const(runs)) {
                if (filled) {
                    QPolygonF shape = line;
                    const double base = values.at(zero, plot.bottom(), plot.top());
                    shape.append(QPointF(line.last().x(), base));
                    shape.append(QPointF(line.first().x(), base));
                    QColor wash = color;
                    wash.setAlphaF(0.28);
                    p->setPen(Qt::NoPen);
                    p->setBrush(wash);
                    p->drawPolygon(shape);
                }
                p->setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p->setBrush(Qt::NoBrush);
                p->drawPolyline(line);
                if (!filled && slot > 14) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(color);
                    for (const QPointF &point : line) {
                        p->drawEllipse(point, 3, 3);
                    }
                }
            }
        }
    }
    p->restore();
}
