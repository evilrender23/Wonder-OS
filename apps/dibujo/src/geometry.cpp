#include "geometry.h"

#include <QPainterPathStroker>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace
{
QVariantMap form(const char *id, const char *name, const char *section, int sides = 0, double radius = 0)
{
    return {
        {QStringLiteral("form"), QString::fromLatin1(id)},
        {QStringLiteral("name"), QString::fromUtf8(name)},
        {QStringLiteral("section"), QString::fromUtf8(section)},
        {QStringLiteral("sides"), sides},
        {QStringLiteral("radius"), radius},
    };
}

QPainterPath polygon(std::initializer_list<QPointF> unit, double w, double h)
{
    QPolygonF p;
    for (const QPointF &u : unit) {
        p << QPointF(u.x() * w, u.y() * h);
    }
    QPainterPath path;
    path.addPolygon(p);
    path.closeSubpath();
    return path;
}

/// A regular polygon, or a star, standing on its box: a point at the top.
QPainterPath regular(int sides, bool star, double w, double h)
{
    sides = qBound(3, sides, 64);
    QPolygonF p;
    const int count = star ? sides * 2 : sides;
    for (int i = 0; i < count; ++i) {
        const double a = -M_PI / 2 + i * 2 * M_PI / count;
        const double r = star && i % 2 == 1 ? 0.42 : 1.0;
        p << QPointF(std::cos(a) * r, std::sin(a) * r);
    }
    // Stretch what the points span to the whole box, so the box is honest.
    const QRectF span = p.boundingRect();
    QPainterPath path;
    for (int i = 0; i < p.size(); ++i) {
        const QPointF q((p.at(i).x() - span.x()) / span.width() * w, (p.at(i).y() - span.y()) / span.height() * h);
        if (i == 0) {
            path.moveTo(q);
        } else {
            path.lineTo(q);
        }
    }
    path.closeSubpath();
    return path;
}
}

QVariantList Geometry::forms()
{
    static const QVariantList list{
        form("rect", "Rectángulo", "Básicas"),
        form("rect", "Rectángulo redondeado", "Básicas", 0, 16),
        form("ellipse", "Elipse", "Básicas"),
        form("triangle", "Triángulo", "Básicas"),
        form("diamond", "Rombo", "Básicas"),
        form("polygon", "Pentágono", "Básicas", 5),
        form("polygon", "Hexágono", "Básicas", 6),
        form("star", "Estrella", "Básicas", 5),
        form("cross", "Cruz", "Básicas"),
        form("heart", "Corazón", "Básicas"),
        form("rect", "Proceso", "Diagrama de flujo", 0, 6),
        form("diamond", "Decisión", "Diagrama de flujo"),
        form("pill", "Inicio / fin", "Diagrama de flujo"),
        form("parallelogram", "Datos", "Diagrama de flujo"),
        form("document", "Documento", "Diagrama de flujo"),
        form("cylinder", "Base de datos", "Diagrama de flujo"),
        form("hexagon", "Preparación", "Diagrama de flujo"),
        form("trapezoid", "Entrada manual", "Diagrama de flujo"),
        form("arrowRight", "Flecha derecha", "Flechas"),
        form("arrowLeft", "Flecha izquierda", "Flechas"),
        form("arrowUp", "Flecha arriba", "Flechas"),
        form("arrowDown", "Flecha abajo", "Flechas"),
        form("arrowDouble", "Flecha doble", "Flechas"),
        form("chevron", "Galón", "Flechas"),
        form("bubble", "Bocadillo", "Bocadillos"),
        form("bubbleRound", "Bocadillo redondo", "Bocadillos"),
        form("cloud", "Pensamiento", "Bocadillos"),
    };
    return list;
}

QVariantList Geometry::framePresets()
{
    const auto preset = [](const char *key, const char *name, double w, double h) {
        return QVariantMap{{QStringLiteral("key"), QString::fromLatin1(key)}, {QStringLiteral("name"), QString::fromUtf8(name)}, {QStringLiteral("w"), w}, {QStringLiteral("h"), h}};
    };
    // At 96 px to the inch: an A4 is 210 × 297 mm.
    static const QVariantList list{
        preset("a4", "A4 vertical", 794, 1123),
        preset("a4h", "A4 horizontal", 1123, 794),
        preset("a3", "A3 vertical", 1123, 1587),
        preset("letter", "Carta", 816, 1056),
        preset("16:9", "Pantalla 16:9", 1280, 720),
        preset("4:3", "Pantalla 4:3", 1024, 768),
        preset("square", "Cuadrado", 1080, 1080),
        preset("story", "Historia 9:16", 1080, 1920),
    };
    return list;
}

QString Geometry::baseName(const Shape &shape)
{
    if (shape.kind == QLatin1String("line")) {
        if (!shape.from.isEmpty() || !shape.to.isEmpty() || shape.route != QLatin1String("straight")) {
            return QStringLiteral("Conector");
        }
        return shape.style.endArrow != QLatin1String("none") || shape.style.startArrow != QLatin1String("none") ? QStringLiteral("Flecha") : QStringLiteral("Línea");
    }
    if (shape.kind == QLatin1String("path")) {
        return QStringLiteral("Trazo");
    }
    if (shape.kind == QLatin1String("text")) {
        return QStringLiteral("Texto");
    }
    if (shape.kind == QLatin1String("image")) {
        return QStringLiteral("Imagen");
    }
    if (shape.kind == QLatin1String("note")) {
        return QStringLiteral("Nota");
    }
    if (shape.kind == QLatin1String("frame")) {
        return QStringLiteral("Marco");
    }
    for (const QVariant &v : forms()) {
        const QVariantMap f = v.toMap();
        if (f.value(QStringLiteral("form")).toString() == shape.form
            && (f.value(QStringLiteral("sides")).toInt() == 0 || f.value(QStringLiteral("sides")).toInt() == shape.sides)) {
            return f.value(QStringLiteral("name")).toString();
        }
    }
    if (shape.form == QLatin1String("polygon")) {
        return QStringLiteral("Polígono");
    }
    return shape.form == QLatin1String("star") ? QStringLiteral("Estrella") : QStringLiteral("Forma");
}

// --- Outlines -------------------------------------------------------------------

QPainterPath Geometry::outline(const Shape &s)
{
    const double w = s.w;
    const double h = s.h;
    QPainterPath path;
    if (s.kind == QLatin1String("path")) {
        if (s.path.isEmpty()) {
            return path;
        }
        QTransform t;
        const double sx = s.ref.width() > 1e-9 ? w / s.ref.width() : 1;
        const double sy = s.ref.height() > 1e-9 ? h / s.ref.height() : 1;
        t.scale(sx, sy);
        t.translate(-s.ref.x(), -s.ref.y());
        path = t.map(s.path);
        path.setFillRule(s.path.fillRule());
        return path;
    }
    if (s.kind == QLatin1String("note")) {
        path.addRoundedRect(QRectF(0, 0, w, h), 3, 3);
        return path;
    }
    if (s.kind != QLatin1String("shape")) {
        path.addRect(QRectF(0, 0, w, h));
        return path;
    }
    const QString &f = s.form;
    if (f == QLatin1String("ellipse")) {
        path.addEllipse(QRectF(0, 0, w, h));
    } else if (f == QLatin1String("triangle")) {
        path = polygon({{0.5, 0}, {1, 1}, {0, 1}}, w, h);
    } else if (f == QLatin1String("diamond")) {
        path = polygon({{0.5, 0}, {1, 0.5}, {0.5, 1}, {0, 0.5}}, w, h);
    } else if (f == QLatin1String("polygon")) {
        path = regular(s.sides, false, w, h);
    } else if (f == QLatin1String("star")) {
        path = regular(s.sides, true, w, h);
    } else if (f == QLatin1String("hexagon")) {
        path = polygon({{0.2, 0}, {0.8, 0}, {1, 0.5}, {0.8, 1}, {0.2, 1}, {0, 0.5}}, w, h);
    } else if (f == QLatin1String("parallelogram")) {
        path = polygon({{0.2, 0}, {1, 0}, {0.8, 1}, {0, 1}}, w, h);
    } else if (f == QLatin1String("trapezoid")) {
        path = polygon({{0, 0.3}, {1, 0}, {1, 1}, {0, 1}}, w, h);
    } else if (f == QLatin1String("cross")) {
        path = polygon({{0.33, 0}, {0.67, 0}, {0.67, 0.33}, {1, 0.33}, {1, 0.67}, {0.67, 0.67}, {0.67, 1}, {0.33, 1}, {0.33, 0.67}, {0, 0.67}, {0, 0.33}, {0.33, 0.33}}, w, h);
    } else if (f == QLatin1String("arrowRight")) {
        path = polygon({{0, 0.3}, {0.6, 0.3}, {0.6, 0}, {1, 0.5}, {0.6, 1}, {0.6, 0.7}, {0, 0.7}}, w, h);
    } else if (f == QLatin1String("arrowLeft")) {
        path = polygon({{1, 0.3}, {0.4, 0.3}, {0.4, 0}, {0, 0.5}, {0.4, 1}, {0.4, 0.7}, {1, 0.7}}, w, h);
    } else if (f == QLatin1String("arrowUp")) {
        path = polygon({{0.3, 1}, {0.3, 0.4}, {0, 0.4}, {0.5, 0}, {1, 0.4}, {0.7, 0.4}, {0.7, 1}}, w, h);
    } else if (f == QLatin1String("arrowDown")) {
        path = polygon({{0.3, 0}, {0.3, 0.6}, {0, 0.6}, {0.5, 1}, {1, 0.6}, {0.7, 0.6}, {0.7, 0}}, w, h);
    } else if (f == QLatin1String("arrowDouble")) {
        path = polygon({{0, 0.5}, {0.25, 0}, {0.25, 0.3}, {0.75, 0.3}, {0.75, 0}, {1, 0.5}, {0.75, 1}, {0.75, 0.7}, {0.25, 0.7}, {0.25, 1}}, w, h);
    } else if (f == QLatin1String("chevron")) {
        path = polygon({{0, 0}, {0.7, 0}, {1, 0.5}, {0.7, 1}, {0, 1}, {0.3, 0.5}}, w, h);
    } else if (f == QLatin1String("pill")) {
        const double r = qMin(w, h) / 2;
        path.addRoundedRect(QRectF(0, 0, w, h), r, r);
    } else if (f == QLatin1String("heart")) {
        path.moveTo(0.5 * w, 0.28 * h);
        path.cubicTo(0.5 * w, 0.1 * h, 0.36 * w, 0, 0.25 * w, 0);
        path.cubicTo(0.1 * w, 0, 0, 0.14 * h, 0, 0.32 * h);
        path.cubicTo(0, 0.6 * h, 0.3 * w, 0.8 * h, 0.5 * w, h);
        path.cubicTo(0.7 * w, 0.8 * h, w, 0.6 * h, w, 0.32 * h);
        path.cubicTo(w, 0.14 * h, 0.9 * w, 0, 0.75 * w, 0);
        path.cubicTo(0.64 * w, 0, 0.5 * w, 0.1 * h, 0.5 * w, 0.28 * h);
        path.closeSubpath();
    } else if (f == QLatin1String("document")) {
        path.moveTo(0, 0);
        path.lineTo(w, 0);
        path.lineTo(w, 0.85 * h);
        path.cubicTo(0.75 * w, 0.7 * h, 0.65 * w, 0.75 * h, 0.5 * w, 0.87 * h);
        path.cubicTo(0.35 * w, h, 0.2 * w, 1.02 * h, 0, 0.9 * h);
        path.closeSubpath();
    } else if (f == QLatin1String("cylinder")) {
        const double cap = qMin(h * 0.3, w * 0.3);
        path.moveTo(0, cap / 2);
        path.cubicTo(0, -cap / 6, w, -cap / 6, w, cap / 2);
        path.lineTo(w, h - cap / 2);
        path.cubicTo(w, h + cap / 6, 0, h + cap / 6, 0, h - cap / 2);
        path.closeSubpath();
        // The rim of the lid.
        path.moveTo(0, cap / 2);
        path.cubicTo(0, cap * 7 / 6, w, cap * 7 / 6, w, cap / 2);
        path.cubicTo(w, cap * 7 / 6, 0, cap * 7 / 6, 0, cap / 2);
        path.setFillRule(Qt::WindingFill);
    } else if (f == QLatin1String("bubble")) {
        const double body = h * 0.78;
        const double r = qMin(qMin(w, body) / 2, 14.0);
        path.moveTo(r, 0);
        path.lineTo(w - r, 0);
        path.quadTo(w, 0, w, r);
        path.lineTo(w, body - r);
        path.quadTo(w, body, w - r, body);
        path.lineTo(0.42 * w, body);
        path.lineTo(0.2 * w, h);
        path.lineTo(0.24 * w, body);
        path.lineTo(r, body);
        path.quadTo(0, body, 0, body - r);
        path.lineTo(0, r);
        path.quadTo(0, 0, r, 0);
        path.closeSubpath();
    } else if (f == QLatin1String("bubbleRound")) {
        QPainterPath body;
        body.addEllipse(QRectF(0, 0, w, h * 0.84));
        QPainterPath tail;
        tail.moveTo(0.22 * w, 0.66 * h);
        tail.lineTo(0.14 * w, h);
        tail.lineTo(0.42 * w, 0.78 * h);
        tail.closeSubpath();
        path = body.united(tail);
    } else if (f == QLatin1String("cloud")) {
        QPainterPath cloud;
        cloud.addEllipse(QRectF(0.08 * w, 0.2 * h, 0.4 * w, 0.42 * h));
        cloud.addEllipse(QRectF(0.3 * w, 0.02 * h, 0.42 * w, 0.48 * h));
        cloud.addEllipse(QRectF(0.56 * w, 0.18 * h, 0.4 * w, 0.42 * h));
        cloud.addEllipse(QRectF(0.02 * w, 0.36 * h, 0.5 * w, 0.4 * h));
        cloud.addEllipse(QRectF(0.3 * w, 0.34 * h, 0.44 * w, 0.44 * h));
        cloud.addEllipse(QRectF(0.56 * w, 0.36 * h, 0.42 * w, 0.38 * h));
        cloud.setFillRule(Qt::WindingFill);
        path = cloud.simplified();
        path.addEllipse(QRectF(0.16 * w, 0.8 * h, 0.12 * w, 0.1 * h));
        path.addEllipse(QRectF(0.06 * w, 0.92 * h, 0.07 * w, 0.07 * h));
        path.setFillRule(Qt::WindingFill);
    } else {
        const double r = qMin(s.style.radius, qMin(w, h) / 2);
        if (r > 0) {
            path.addRoundedRect(QRectF(0, 0, w, h), r, r);
        } else {
            path.addRect(QRectF(0, 0, w, h));
        }
    }
    return path;
}

QPainterPath Geometry::worldOutline(const Shape &shape)
{
    if (shape.isLine()) {
        return linePath(shape);
    }
    QPainterPath path = shape.transform().map(outline(shape));
    return path;
}

// --- Lines ----------------------------------------------------------------------

/// The way a line arrives at an end: a unit vector pointing out of the line.
static QPointF endDirection(const Shape &line, bool atEnd)
{
    const QPointF d = line.p2 - line.p1;
    QPointF v;
    if (line.route == QLatin1String("straight")) {
        v = d;
    } else if (line.horizontal) {
        v = qAbs(d.x()) > 1e-6 ? QPointF(d.x(), 0) : QPointF(0, d.y());
    } else {
        v = qAbs(d.y()) > 1e-6 ? QPointF(0, d.y()) : QPointF(d.x(), 0);
    }
    const double len = std::hypot(v.x(), v.y());
    if (len < 1e-9) {
        return QPointF(atEnd ? 1 : -1, 0);
    }
    v /= len;
    return atEnd ? v : -v;
}

static double headSize(const Shape &line)
{
    return qMax(9.0, line.style.strokeWidth * 4.5);
}

QPainterPath Geometry::linePath(const Shape &line)
{
    QPointF a = line.p1;
    QPointF b = line.p2;
    // A filled head covers the end of the stroke: stop the stroke inside it,
    // or a thick line pokes out of the tip.
    const auto pull = [&line](const QString &head) {
        return head == QLatin1String("triangle") || head == QLatin1String("diamond") ? headSize(line) * 0.6 : 0.0;
    };
    const double total = std::hypot(b.x() - a.x(), b.y() - a.y());
    const double pullA = pull(line.style.startArrow);
    const double pullB = pull(line.style.endArrow);
    if (total > pullA + pullB + 1) {
        a += -endDirection(line, false) * pullA;
        b += -endDirection(line, true) * pullB;
    }
    QPainterPath path;
    path.moveTo(a);
    if (line.route == QLatin1String("elbow")) {
        const double r = 8;
        const auto corner = [&path, r](const QPointF &from, const QPointF &at, const QPointF &to) {
            const double in = std::hypot(at.x() - from.x(), at.y() - from.y());
            const double out = std::hypot(to.x() - at.x(), to.y() - at.y());
            const double rr = qMin(r, qMin(in, out) / 2);
            if (rr < 0.5) {
                path.lineTo(at);
                return;
            }
            const QPointF before = at + (from - at) / in * rr;
            const QPointF after = at + (to - at) / out * rr;
            path.lineTo(before);
            path.quadTo(at, after);
        };
        if (line.horizontal) {
            const double mid = (a.x() + b.x()) / 2;
            if (qAbs(a.y() - b.y()) < 0.5) {
                path.lineTo(b);
            } else {
                corner(a, QPointF(mid, a.y()), QPointF(mid, b.y()));
                corner(QPointF(mid, a.y()), QPointF(mid, b.y()), b);
                path.lineTo(b);
            }
        } else {
            const double mid = (a.y() + b.y()) / 2;
            if (qAbs(a.x() - b.x()) < 0.5) {
                path.lineTo(b);
            } else {
                corner(a, QPointF(a.x(), mid), QPointF(b.x(), mid));
                corner(QPointF(a.x(), mid), QPointF(b.x(), mid), b);
                path.lineTo(b);
            }
        }
    } else if (line.route == QLatin1String("curve")) {
        if (line.horizontal) {
            const double reach = qMax(30.0, qAbs(b.x() - a.x()) / 2) * (b.x() >= a.x() ? 1 : -1);
            path.cubicTo(QPointF(a.x() + reach, a.y()), QPointF(b.x() - reach, b.y()), b);
        } else {
            const double reach = qMax(30.0, qAbs(b.y() - a.y()) / 2) * (b.y() >= a.y() ? 1 : -1);
            path.cubicTo(QPointF(a.x(), a.y() + reach), QPointF(b.x(), b.y() - reach), b);
        }
    } else {
        path.lineTo(b);
    }
    return path;
}

QPainterPath Geometry::arrowHead(const Shape &line, bool atEnd, bool *filled)
{
    const QString &head = atEnd ? line.style.endArrow : line.style.startArrow;
    QPainterPath path;
    if (filled) {
        *filled = false;
    }
    if (head == QLatin1String("none") || head.isEmpty()) {
        return path;
    }
    const QPointF tip = atEnd ? line.p2 : line.p1;
    const QPointF dir = endDirection(line, atEnd);
    const QPointF side(-dir.y(), dir.x());
    const double size = headSize(line);
    if (head == QLatin1String("arrow")) {
        path.moveTo(tip - dir * size + side * size * 0.5);
        path.lineTo(tip);
        path.lineTo(tip - dir * size - side * size * 0.5);
    } else if (head == QLatin1String("triangle")) {
        path.moveTo(tip);
        path.lineTo(tip - dir * size + side * size * 0.42);
        path.lineTo(tip - dir * size - side * size * 0.42);
        path.closeSubpath();
        if (filled) {
            *filled = true;
        }
    } else if (head == QLatin1String("diamond")) {
        path.moveTo(tip);
        path.lineTo(tip - dir * size * 0.6 + side * size * 0.36);
        path.lineTo(tip - dir * size * 1.2);
        path.lineTo(tip - dir * size * 0.6 - side * size * 0.36);
        path.closeSubpath();
        if (filled) {
            *filled = true;
        }
    } else if (head == QLatin1String("circle")) {
        path.addEllipse(tip - dir * size * 0.35, size * 0.35, size * 0.35);
        if (filled) {
            *filled = true;
        }
    } else if (head == QLatin1String("bar")) {
        path.moveTo(tip + side * size * 0.5);
        path.lineTo(tip - side * size * 0.5);
    }
    return path;
}

/// Where the way from the middle of an object towards `target` leaves it.
static QPointF edgeToward(const Shape &shape, const QPointF &target)
{
    const QPointF c = shape.center();
    const QPainterPath outline = Geometry::worldOutline(shape);
    if (!outline.contains(c)) {
        return c;
    }
    if (outline.contains(target)) {
        return c;
    }
    double lo = 0;
    double hi = 1;
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) / 2;
        if (outline.contains(c + (target - c) * mid)) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return c + (target - c) * hi;
}

bool Geometry::updateConnectors(Document &doc)
{
    bool moved = false;
    for (int i = 0; i < doc.shapes.size(); ++i) {
        if (!doc.shapes.at(i).isLine()) {
            continue;
        }
        Shape line = doc.shapes.at(i);
        const Shape *a = line.from.isEmpty() ? nullptr : doc.find(line.from);
        const Shape *b = line.to.isEmpty() ? nullptr : doc.find(line.to);
        if (a && a->isLine()) {
            a = nullptr;
        }
        if (b && b->isLine()) {
            b = nullptr;
        }
        if (!a) {
            line.from.clear();
        }
        if (!b) {
            line.to.clear();
        }
        const QPointF ca = a ? a->center() : line.p1;
        const QPointF cb = b ? b->center() : line.p2;
        line.horizontal = qAbs(cb.x() - ca.x()) >= qAbs(cb.y() - ca.y());
        if (line.route == QLatin1String("straight")) {
            if (a) {
                line.p1 = edgeToward(*a, cb);
            }
            if (b) {
                line.p2 = edgeToward(*b, ca);
            }
        } else {
            // Elbows and curves leave by the middle of a side.
            const auto side = [&line](const Shape &s, const QPointF &other) {
                const QRectF r = s.bounds();
                const QPointF c = r.center();
                const QPointF out = line.horizontal ? QPointF(other.x() >= c.x() ? r.right() + 1 : r.left() - 1, c.y())
                                                    : QPointF(c.x(), other.y() >= c.y() ? r.bottom() + 1 : r.top() - 1);
                // On the outline itself: a slanted side is not where its box ends.
                return edgeToward(s, out);
            };
            if (a) {
                line.p1 = side(*a, cb);
            }
            if (b) {
                line.p2 = side(*b, ca);
            }
        }
        const QRectF r = QRectF(line.p1, line.p2).normalized();
        line.x = r.x();
        line.y = r.y();
        line.w = r.width();
        line.h = r.height();
        line.rotation = 0;
        const Shape &old = doc.shapes.at(i);
        if (!(line == old) || line.horizontal != old.horizontal) {
            doc.shapes[i] = line;
            moved = true;
        }
    }
    return moved;
}

// --- Touching -------------------------------------------------------------------

bool Geometry::hit(const Shape &s, const QPointF &point, double slack)
{
    if (s.isLine()) {
        QPainterPathStroker stroker;
        stroker.setWidth(qMax(s.style.strokeWidth, 1.0) + slack * 2);
        stroker.setCapStyle(Qt::RoundCap);
        return stroker.createStroke(linePath(s)).contains(point);
    }
    bool ok = false;
    const QTransform inverse = s.transform().inverted(&ok);
    if (!ok) {
        return false;
    }
    const QPointF local = inverse.map(point);
    if (s.isFrame()) {
        // A frame is taken by its name or its edge: its inside belongs to
        // what is drawn on it.
        const QRectF label(0, -22 - slack, qMin(s.w, 220.0), 22 + slack);
        if (label.contains(local)) {
            return true;
        }
        const QRectF outer = QRectF(0, 0, s.w, s.h).adjusted(-slack, -slack, slack, slack);
        const QRectF inner = QRectF(0, 0, s.w, s.h).adjusted(slack, slack, -slack, -slack);
        return outer.contains(local) && !inner.contains(local);
    }
    if (!QRectF(0, 0, s.w, s.h).adjusted(-slack - s.style.strokeWidth, -slack - s.style.strokeWidth, slack + s.style.strokeWidth, slack + s.style.strokeWidth).contains(local)) {
        return false;
    }
    if (s.kind == QLatin1String("text") || s.kind == QLatin1String("image") || s.kind == QLatin1String("note")) {
        return QRectF(0, 0, s.w, s.h).adjusted(-slack, -slack, slack, slack).contains(local);
    }
    const QPainterPath path = outline(s);
    const bool hollow = s.style.fillKind == QLatin1String("none") && s.text.isEmpty();
    if (!hollow && path.contains(local)) {
        return true;
    }
    QPainterPathStroker stroker;
    stroker.setWidth(qMax(s.style.strokeWidth, 1.0) + slack * 2);
    return stroker.createStroke(path).contains(local);
}

// --- Freehand -------------------------------------------------------------------

static void simplifyRange(const QList<QPointF> &points, int first, int last, double tolerance, QList<bool> &keep)
{
    if (last <= first + 1) {
        return;
    }
    const QLineF chord(points.at(first), points.at(last));
    const double length = chord.length();
    double worst = 0;
    int index = -1;
    for (int i = first + 1; i < last; ++i) {
        const QPointF p = points.at(i);
        const double distance = length < 1e-9 ? QLineF(points.at(first), p).length()
            : qAbs((chord.dx()) * (chord.y1() - p.y()) - (chord.x1() - p.x()) * chord.dy()) / length;
        if (distance > worst) {
            worst = distance;
            index = i;
        }
    }
    if (index >= 0 && worst > tolerance) {
        keep[index] = true;
        simplifyRange(points, first, index, tolerance, keep);
        simplifyRange(points, index, last, tolerance, keep);
    }
}

QList<QPointF> Geometry::simplify(const QList<QPointF> &points, double tolerance)
{
    if (points.size() < 3) {
        return points;
    }
    QList<bool> keep(points.size(), false);
    keep[0] = true;
    keep[points.size() - 1] = true;
    simplifyRange(points, 0, points.size() - 1, tolerance, keep);
    QList<QPointF> out;
    for (int i = 0; i < points.size(); ++i) {
        if (keep.at(i)) {
            out << points.at(i);
        }
    }
    return out;
}

QPainterPath Geometry::smooth(const QList<QPointF> &raw, double tolerance)
{
    QPainterPath path;
    const QList<QPointF> p = simplify(raw, tolerance);
    if (p.isEmpty()) {
        return path;
    }
    path.moveTo(p.first());
    if (p.size() == 1) {
        path.lineTo(p.first() + QPointF(0.01, 0.01));
        return path;
    }
    if (p.size() == 2) {
        path.lineTo(p.at(1));
        return path;
    }
    // Catmull-Rom through the points that are left, as Béziers.
    for (int i = 0; i + 1 < p.size(); ++i) {
        const QPointF p0 = p.at(qMax(0, i - 1));
        const QPointF p1 = p.at(i);
        const QPointF p2 = p.at(i + 1);
        const QPointF p3 = p.at(qMin(p.size() - 1, i + 2));
        path.cubicTo(p1 + (p2 - p0) / 6, p2 - (p3 - p1) / 6, p2);
    }
    return path;
}

// --- Snapping -------------------------------------------------------------------

namespace
{
struct Axis {
    double best = 0;
    bool found = false;
    QList<Geometry::Guide> guides;
};

void offer(Axis &axis, double delta, double tolerance, const QList<Geometry::Guide> &guides)
{
    if (qAbs(delta) > tolerance) {
        return;
    }
    if (!axis.found || qAbs(delta) < qAbs(axis.best) - 1e-9) {
        axis.best = delta;
        axis.found = true;
        axis.guides = guides;
    } else if (qAbs(delta - axis.best) < 1e-9) {
        axis.guides += guides;
    }
}

QString gapLabel(double gap)
{
    return Model::number(gap, 0);
}

/// Equal gaps along x (or along y, with everything transposed).
void equalGaps(Axis &axis, const QRectF &moving, const QList<QRectF> &others, double tolerance, bool vertical)
{
    const auto lo = [vertical](const QRectF &r) { return vertical ? r.top() : r.left(); };
    const auto hi = [vertical](const QRectF &r) { return vertical ? r.bottom() : r.right(); };
    const auto crossLo = [vertical](const QRectF &r) { return vertical ? r.left() : r.top(); };
    const auto crossHi = [vertical](const QRectF &r) { return vertical ? r.right() : r.bottom(); };
    const auto mark = [vertical](double from, double to, double cross) {
        Geometry::Guide g;
        g.line = vertical ? QLineF(cross, from, cross, to) : QLineF(from, cross, to, cross);
        g.label = gapLabel(to - from);
        return g;
    };
    // Only what shares a band with the moving box is a neighbour.
    QList<QRectF> row;
    for (const QRectF &r : others) {
        if (crossHi(r) > crossLo(moving) && crossLo(r) < crossHi(moving)) {
            row << r;
        }
    }
    if (row.size() < 2) {
        return;
    }
    std::sort(row.begin(), row.end(), [&lo](const QRectF &a, const QRectF &b) { return lo(a) < lo(b); });
    const double size = hi(moving) - lo(moving);
    const double cross = (qMax(crossLo(moving), crossLo(row.first())) + qMin(crossHi(moving), crossHi(row.first()))) / 2;
    for (int i = 0; i + 1 < row.size(); ++i) {
        const QRectF &a = row.at(i);
        const QRectF &b = row.at(i + 1);
        const double gap = lo(b) - hi(a);
        if (gap <= 0) {
            continue;
        }
        // After the pair, at the same distance.
        const double after = hi(b) + gap;
        offer(axis, after - lo(moving), tolerance, {mark(hi(a), lo(b), cross), mark(hi(b), after, cross)});
        // Before the pair.
        const double before = lo(a) - gap - size;
        offer(axis, before - lo(moving), tolerance, {mark(before + size, lo(a), cross), mark(hi(a), lo(b), cross)});
        // Between the two, in the middle.
        if (gap > size) {
            const double between = hi(a) + (gap - size) / 2;
            offer(axis, between - lo(moving), tolerance, {mark(hi(a), between, cross), mark(between + size, lo(b), cross)});
        }
    }
}
}

Geometry::Snap Geometry::snapMove(const QRectF &moving, const QList<QRectF> &others, double tolerance)
{
    Axis ax;
    Axis ay;
    const double mx[3] = {moving.left(), moving.center().x(), moving.right()};
    const double my[3] = {moving.top(), moving.center().y(), moving.bottom()};
    for (const QRectF &o : others) {
        const double ox[3] = {o.left(), o.center().x(), o.right()};
        const double oy[3] = {o.top(), o.center().y(), o.bottom()};
        for (double m : mx) {
            for (double t : ox) {
                Guide g;
                g.line = QLineF(t, qMin(moving.top(), o.top()), t, qMax(moving.bottom(), o.bottom()));
                offer(ax, t - m, tolerance, {g});
            }
        }
        for (double m : my) {
            for (double t : oy) {
                Guide g;
                g.line = QLineF(qMin(moving.left(), o.left()), t, qMax(moving.right(), o.right()), t);
                offer(ay, t - m, tolerance, {g});
            }
        }
    }
    if (!ax.found) {
        equalGaps(ax, moving, others, tolerance, false);
    }
    if (!ay.found) {
        equalGaps(ay, moving, others, tolerance, true);
    }
    Snap snap;
    snap.delta = QPointF(ax.found ? ax.best : 0, ay.found ? ay.best : 0);
    // The guides were measured before the push: move them along with the box.
    const auto settle = [&snap, &moving](QList<Guide> guides, bool vertical) {
        for (Guide &g : guides) {
            if (g.label.isEmpty()) {
                // An alignment guide spans the moved box too.
                const QRectF moved = moving.translated(snap.delta);
                if (vertical) {
                    g.line.setP1(QPointF(g.line.x1(), qMin(g.line.y1(), moved.top())));
                    g.line.setP2(QPointF(g.line.x2(), qMax(g.line.y2(), moved.bottom())));
                } else {
                    g.line.setP1(QPointF(qMin(g.line.x1(), moved.left()), g.line.y1()));
                    g.line.setP2(QPointF(qMax(g.line.x2(), moved.right()), g.line.y2()));
                }
            } else {
                // A gap guide keeps to the band of the box across its axis.
                if (vertical) {
                    g.line.translate(snap.delta.x(), 0);
                } else {
                    g.line.translate(0, snap.delta.y());
                }
            }
            snap.guides << g;
        }
    };
    if (ax.found) {
        settle(ax.guides, true);
    }
    if (ay.found) {
        settle(ay.guides, false);
    }
    return snap;
}

Geometry::Snap Geometry::snapPoint(const QPointF &point, const QList<QRectF> &others, double tolerance)
{
    Axis ax;
    Axis ay;
    for (const QRectF &o : others) {
        for (double t : {o.left(), o.center().x(), o.right()}) {
            Guide g;
            g.line = QLineF(t, qMin(point.y(), o.top()), t, qMax(point.y(), o.bottom()));
            offer(ax, t - point.x(), tolerance, {g});
        }
        for (double t : {o.top(), o.center().y(), o.bottom()}) {
            Guide g;
            g.line = QLineF(qMin(point.x(), o.left()), t, qMax(point.x(), o.right()), t);
            offer(ay, t - point.y(), tolerance, {g});
        }
    }
    Snap snap;
    snap.delta = QPointF(ax.found ? ax.best : 0, ay.found ? ay.best : 0);
    if (ax.found) {
        snap.guides += ax.guides;
    }
    if (ay.found) {
        snap.guides += ay.guides;
    }
    return snap;
}
