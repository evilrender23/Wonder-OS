#include "ink.h"

#include <QLineF>
#include <QPainter>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

static QString number(qreal value)
{
    // Tenths of a pixel are plenty, and keep the file small.
    QString text = QString::number(value, 'f', 1);
    if (text.endsWith(QLatin1String(".0"))) {
        text.chop(2);
    }
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

QString Stroke::pathData() const
{
    if (points.isEmpty()) {
        return {};
    }
    const QPointF first = points.first();
    QString d = QStringLiteral("M%1 %2").arg(number(first.x()), number(first.y()));
    if (points.size() == 1) {
        // A dot: a line onto itself, which round caps make a circle.
        return d + QStringLiteral("L%1 %2").arg(number(first.x()), number(first.y()));
    }
    if (points.size() == 2) {
        return d + QStringLiteral("L%1 %2").arg(number(points.at(1).x()), number(points.at(1).y()));
    }
    for (int i = 1; i + 1 < points.size(); ++i) {
        const QPointF control = points.at(i);
        const QPointF mid = (control + points.at(i + 1)) / 2;
        d += QStringLiteral("Q%1 %2 %3 %4").arg(number(control.x()), number(control.y()), number(mid.x()), number(mid.y()));
    }
    const QPointF last = points.last();
    return d + QStringLiteral("L%1 %2").arg(number(last.x()), number(last.y()));
}

QPainterPath Stroke::path() const
{
    QPainterPath path;
    if (points.isEmpty()) {
        return path;
    }
    path.moveTo(points.first());
    if (points.size() < 3) {
        path.lineTo(points.last());
        return path;
    }
    for (int i = 1; i + 1 < points.size(); ++i) {
        path.quadTo(points.at(i), (points.at(i) + points.at(i + 1)) / 2);
    }
    path.lineTo(points.last());
    return path;
}

QRectF Stroke::bounds() const
{
    if (points.isEmpty()) {
        return {};
    }
    qreal left = points.first().x(), right = left, top = points.first().y(), bottom = top;
    for (const QPointF &point : points) {
        left = qMin(left, point.x());
        right = qMax(right, point.x());
        top = qMin(top, point.y());
        bottom = qMax(bottom, point.y());
    }
    const qreal half = width / 2;
    return QRectF(left - half, top - half, right - left + width, bottom - top + width);
}

static qreal distance(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal length = ab.x() * ab.x() + ab.y() * ab.y();
    if (length <= 0) {
        return QLineF(point, a).length();
    }
    const qreal t = qBound(0.0, ((point.x() - a.x()) * ab.x() + (point.y() - a.y()) * ab.y()) / length, 1.0);
    return QLineF(point, a + ab * t).length();
}

bool Stroke::near(const QPointF &point, qreal radius) const
{
    const qreal reach = radius + width / 2;
    if (points.size() == 1) {
        return QLineF(point, points.first()).length() <= reach;
    }
    for (int i = 0; i + 1 < points.size(); ++i) {
        if (distance(point, points.at(i), points.at(i + 1)) <= reach) {
            return true;
        }
    }
    return false;
}

static void simplify(const QList<QPointF> &points, int first, int last, qreal tolerance, QList<bool> &keep)
{
    if (last <= first + 1) {
        return;
    }
    qreal worst = 0;
    int index = -1;
    for (int i = first + 1; i < last; ++i) {
        const qreal d = distance(points.at(i), points.at(first), points.at(last));
        if (d > worst) {
            worst = d;
            index = i;
        }
    }
    if (index >= 0 && worst > tolerance) {
        keep[index] = true;
        simplify(points, first, index, tolerance, keep);
        simplify(points, index, last, tolerance, keep);
    }
}

QList<QPointF> Drawing::smooth(const QList<QPointF> &raw)
{
    // Points on top of each other say nothing.
    QList<QPointF> spaced;
    for (const QPointF &point : raw) {
        if (spaced.isEmpty() || QLineF(spaced.last(), point).length() >= 1.5) {
            spaced << point;
        }
    }
    if (!raw.isEmpty() && spaced.last() != raw.last() && spaced.size() > 1) {
        spaced.last() = raw.last();
    }
    if (spaced.size() < 3) {
        return spaced;
    }
    // Each point eased towards its neighbours; the ends stay where they are.
    QList<QPointF> eased = spaced;
    for (int pass = 0; pass < 2; ++pass) {
        const QList<QPointF> from = eased;
        for (int i = 1; i + 1 < from.size(); ++i) {
            eased[i] = from.at(i - 1) * 0.25 + from.at(i) * 0.5 + from.at(i + 1) * 0.25;
        }
    }
    QList<bool> keep(eased.size(), false);
    keep.first() = true;
    keep.last() = true;
    simplify(eased, 0, eased.size() - 1, 0.35, keep);
    QList<QPointF> out;
    for (int i = 0; i < eased.size(); ++i) {
        if (keep.at(i)) {
            out << QPointF(std::round(eased.at(i).x() * 10) / 10, std::round(eased.at(i).y() * 10) / 10);
        }
    }
    return out;
}

int Drawing::eraseAt(const QPointF &point, qreal radius)
{
    return int(strokes.removeIf([&](const Stroke &stroke) { return stroke.near(point, radius); }));
}

void Drawing::crop(int margin)
{
    if (strokes.isEmpty()) {
        return;
    }
    QRectF all;
    for (const Stroke &stroke : std::as_const(strokes)) {
        all = all.isNull() ? stroke.bounds() : all.united(stroke.bounds());
    }
    const QPointF shift(std::floor(all.left()) - margin, std::floor(all.top()) - margin);
    for (Stroke &stroke : strokes) {
        for (QPointF &point : stroke.points) {
            point -= shift;
        }
    }
    width = qMax(24, int(std::ceil(all.width())) + 2 * margin + 1);
    height = qMax(24, int(std::ceil(all.height())) + 2 * margin + 1);
}

void Drawing::paint(QPainter *painter) const
{
    painter->setRenderHint(QPainter::Antialiasing);
    for (const Stroke &stroke : strokes) {
        QColor color(stroke.color);
        if (stroke.tool == QLatin1String("marker")) {
            color.setAlphaF(0.4);
        }
        painter->setPen(QPen(color, stroke.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        if (stroke.points.size() == 1) {
            painter->drawPoint(stroke.points.first());
        } else {
            painter->drawPath(stroke.path());
        }
    }
}

QByteArray Drawing::toSvg() const
{
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("width"), QString::number(width));
    xml.writeAttribute(QStringLiteral("height"), QString::number(height));
    xml.writeAttribute(QStringLiteral("viewBox"), QStringLiteral("0 0 %1 %2").arg(width).arg(height));
    xml.writeAttribute(QStringLiteral("data-wonderland"), QStringLiteral("ink"));
    // A sheet of paper, so the ink reads the same on a dark page.
    xml.writeEmptyElement(QStringLiteral("rect"));
    xml.writeAttribute(QStringLiteral("width"), QString::number(width));
    xml.writeAttribute(QStringLiteral("height"), QString::number(height));
    xml.writeAttribute(QStringLiteral("rx"), QStringLiteral("8"));
    xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("#ffffff"));
    for (const Stroke &stroke : strokes) {
        if (stroke.points.isEmpty()) {
            continue;
        }
        xml.writeEmptyElement(QStringLiteral("path"));
        xml.writeAttribute(QStringLiteral("d"), stroke.pathData());
        xml.writeAttribute(QStringLiteral("fill"), QStringLiteral("none"));
        xml.writeAttribute(QStringLiteral("stroke"), stroke.color);
        xml.writeAttribute(QStringLiteral("stroke-width"), number(stroke.width));
        xml.writeAttribute(QStringLiteral("stroke-linecap"), QStringLiteral("round"));
        xml.writeAttribute(QStringLiteral("stroke-linejoin"), QStringLiteral("round"));
        if (stroke.tool == QLatin1String("marker")) {
            xml.writeAttribute(QStringLiteral("stroke-opacity"), QStringLiteral("0.4"));
        }
        xml.writeAttribute(QStringLiteral("data-tool"), stroke.tool);
        QStringList points;
        for (const QPointF &point : stroke.points) {
            points << number(point.x()) + QLatin1Char(',') + number(point.y());
        }
        xml.writeAttribute(QStringLiteral("data-points"), points.join(QLatin1Char(' ')));
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    return out;
}

Drawing Drawing::fromSvg(const QByteArray &svg, bool *ok)
{
    Drawing drawing;
    bool ours = false;
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QXmlStreamAttributes attributes = xml.attributes();
        if (xml.name() == QLatin1String("svg")) {
            ours = attributes.value(QLatin1String("data-wonderland")) == QLatin1String("ink");
            drawing.width = qBound(1, attributes.value(QLatin1String("width")).toInt(), 20000);
            drawing.height = qBound(1, attributes.value(QLatin1String("height")).toInt(), 20000);
        } else if (xml.name() == QLatin1String("path") && attributes.hasAttribute(QLatin1String("data-points"))) {
            Stroke stroke;
            stroke.tool = attributes.value(QLatin1String("data-tool")).toString() == QLatin1String("marker") ? QStringLiteral("marker") : QStringLiteral("pen");
            const QColor color(attributes.value(QLatin1String("stroke")).toString());
            stroke.color = color.isValid() ? color.name() : QStringLiteral("#1d1d1f");
            stroke.width = qBound(0.5, attributes.value(QLatin1String("stroke-width")).toDouble(), 80.0);
            const QList<QStringView> pairs = attributes.value(QLatin1String("data-points")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (const QStringView &pair : pairs) {
                const QList<QStringView> xy = pair.split(QLatin1Char(','));
                if (xy.size() == 2) {
                    stroke.points << QPointF(xy.at(0).toDouble(), xy.at(1).toDouble());
                }
            }
            if (!stroke.points.isEmpty()) {
                drawing.strokes << stroke;
            }
        }
    }
    if (ok) {
        *ok = ours && !xml.hasError();
    }
    return drawing;
}
