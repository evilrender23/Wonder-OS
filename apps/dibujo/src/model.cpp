#include "model.h"

#include <QJsonArray>
#include <QUuid>
#include <QtMath>

#include <cmath>

// --- Small things -------------------------------------------------------------

QString Model::colorName(const QColor &color)
{
    if (!color.isValid()) {
        return QStringLiteral("#000000");
    }
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QColor Model::color(const QString &name, const QColor &fallback)
{
    const QColor parsed = QColor::fromString(name);
    return parsed.isValid() ? parsed : fallback;
}

QString Model::number(double value, int decimals)
{
    if (!std::isfinite(value)) {
        return QStringLiteral("0");
    }
    QString text = QString::number(value, 'f', decimals);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) {
            text.chop(1);
        }
        if (text.endsWith(QLatin1Char('.'))) {
            text.chop(1);
        }
    }
    return text == QLatin1String("-0") ? QStringLiteral("0") : text;
}

QString Model::pathToD(const QPainterPath &path, int decimals)
{
    QString d;
    QPointF start;
    bool open = false;
    const auto n = [decimals](double v) { return number(v, decimals); };
    const auto close = [&](const QPointF &last) {
        if (open && qAbs(last.x() - start.x()) < 1e-6 && qAbs(last.y() - start.y()) < 1e-6) {
            d += QStringLiteral(" Z");
        }
    };
    QPointF last;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            close(last);
            if (!d.isEmpty()) {
                d += QLatin1Char(' ');
            }
            d += QStringLiteral("M%1 %2").arg(n(e.x), n(e.y));
            start = QPointF(e.x, e.y);
            last = start;
            open = false;
        } else if (e.isLineTo()) {
            d += QStringLiteral(" L%1 %2").arg(n(e.x), n(e.y));
            last = QPointF(e.x, e.y);
            open = true;
        } else if (e.isCurveTo() && i + 2 < path.elementCount()) {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            d += QStringLiteral(" C%1 %2 %3 %4 %5 %6").arg(n(e.x), n(e.y), n(c2.x), n(c2.y), n(end.x), n(end.y));
            last = QPointF(end.x, end.y);
            open = true;
            i += 2;
        }
    }
    close(last);
    return d;
}

namespace
{
/// Reads the numbers and letters of a path one at a time.
struct PathReader {
    const QString &text;
    int at = 0;

    void skip()
    {
        while (at < text.size() && (text.at(at).isSpace() || text.at(at) == QLatin1Char(','))) {
            ++at;
        }
    }
    bool atNumber()
    {
        skip();
        if (at >= text.size()) {
            return false;
        }
        const QChar c = text.at(at);
        return c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('+') || c == QLatin1Char('.');
    }
    double number()
    {
        skip();
        const int from = at;
        if (at < text.size() && (text.at(at) == QLatin1Char('-') || text.at(at) == QLatin1Char('+'))) {
            ++at;
        }
        bool dot = false;
        while (at < text.size()) {
            const QChar c = text.at(at);
            if (c.isDigit()) {
                ++at;
            } else if (c == QLatin1Char('.') && !dot) {
                dot = true;
                ++at;
            } else if ((c == QLatin1Char('e') || c == QLatin1Char('E')) && at + 1 < text.size()
                       && (text.at(at + 1).isDigit() || text.at(at + 1) == QLatin1Char('-') || text.at(at + 1) == QLatin1Char('+'))) {
                at += 2;
                while (at < text.size() && text.at(at).isDigit()) {
                    ++at;
                }
                break;
            } else {
                break;
            }
        }
        if (at == from) {
            // Not a number: step over it, so a broken path still ends.
            ++at;
            return 0;
        }
        return QStringView(text).mid(from, at - from).toDouble();
    }
    /// The flags of an arc may be glued to what follows: «a1 1 0 01.5 2».
    bool flag()
    {
        skip();
        if (at < text.size() && (text.at(at) == QLatin1Char('0') || text.at(at) == QLatin1Char('1'))) {
            return text.at(at++) == QLatin1Char('1');
        }
        return number() != 0;
    }
};

/// The arc of SVG, as Béziers (the centre parametrisation of the SVG notes).
void arcTo(QPainterPath &path, QPointF from, double rx, double ry, double turn, bool large, bool sweep, QPointF to)
{
    if (from == to) {
        return;
    }
    rx = qAbs(rx);
    ry = qAbs(ry);
    if (rx < 1e-9 || ry < 1e-9) {
        path.lineTo(to);
        return;
    }
    const double phi = qDegreesToRadians(turn);
    const double cosPhi = std::cos(phi);
    const double sinPhi = std::sin(phi);
    const double dx = (from.x() - to.x()) / 2;
    const double dy = (from.y() - to.y()) / 2;
    const double x1 = cosPhi * dx + sinPhi * dy;
    const double y1 = -sinPhi * dx + cosPhi * dy;
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        rx *= std::sqrt(lambda);
        ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double factor = den < 1e-12 ? 0 : std::sqrt(qMax(0.0, num / den));
    if (large == sweep) {
        factor = -factor;
    }
    const double cxp = factor * rx * y1 / ry;
    const double cyp = -factor * ry * x1 / rx;
    const double cx = cosPhi * cxp - sinPhi * cyp + (from.x() + to.x()) / 2;
    const double cy = sinPhi * cxp + cosPhi * cyp + (from.y() + to.y()) / 2;
    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double dot = ux * vx + uy * vy;
        const double len = std::hypot(ux, uy) * std::hypot(vx, vy);
        double a = std::acos(qBound(-1.0, len < 1e-12 ? 1.0 : dot / len, 1.0));
        if (ux * vy - uy * vx < 0) {
            a = -a;
        }
        return a;
    };
    const double theta = angle(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
    double delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0) {
        delta -= 2 * M_PI;
    } else if (sweep && delta < 0) {
        delta += 2 * M_PI;
    }
    const int pieces = qMax(1, int(std::ceil(qAbs(delta) / (M_PI / 2) - 1e-9)));
    const double step = delta / pieces;
    const double k = 4.0 / 3.0 * std::tan(step / 4);
    const auto point = [&](double a, double ox, double oy) {
        const double px = rx * (std::cos(a) + ox);
        const double py = ry * (std::sin(a) + oy);
        return QPointF(cosPhi * px - sinPhi * py + cx, sinPhi * px + cosPhi * py + cy);
    };
    for (int i = 0; i < pieces; ++i) {
        const double a = theta + i * step;
        const double b = a + step;
        const QPointF c1 = point(a, -k * std::sin(a), k * std::cos(a));
        const QPointF c2 = point(b, k * std::sin(b), -k * std::cos(b));
        path.cubicTo(c1, c2, i == pieces - 1 ? to : point(b, 0, 0));
    }
}
}

QPainterPath Model::pathFromD(const QString &d)
{
    QPainterPath path;
    PathReader reader{d};
    QPointF current;
    QPointF start;
    QPointF control;
    QChar command;
    QChar previous;
    while (true) {
        reader.skip();
        if (reader.at >= d.size()) {
            break;
        }
        const QChar c = d.at(reader.at);
        if (c.isLetter()) {
            command = c;
            ++reader.at;
        } else if (command.isNull()) {
            break;
        } else if (command == QLatin1Char('M')) {
            // More pairs after a move are lines.
            command = QLatin1Char('L');
        } else if (command == QLatin1Char('m')) {
            command = QLatin1Char('l');
        }
        const bool relative = command.isLower();
        const QPointF base = relative ? current : QPointF();
        switch (command.toUpper().unicode()) {
        case 'M': {
            const double x = reader.number();
            const double y = reader.number();
            current = base + QPointF(x, y);
            start = current;
            path.moveTo(current);
            break;
        }
        case 'L': {
            const double x = reader.number();
            const double y = reader.number();
            current = base + QPointF(x, y);
            path.lineTo(current);
            break;
        }
        case 'H': {
            const double x = reader.number();
            current.setX(relative ? current.x() + x : x);
            path.lineTo(current);
            break;
        }
        case 'V': {
            const double y = reader.number();
            current.setY(relative ? current.y() + y : y);
            path.lineTo(current);
            break;
        }
        case 'C': {
            const double x1 = reader.number(), y1 = reader.number(), x2 = reader.number(), y2 = reader.number();
            const double x = reader.number(), y = reader.number();
            control = base + QPointF(x2, y2);
            path.cubicTo(base + QPointF(x1, y1), control, base + QPointF(x, y));
            current = base + QPointF(x, y);
            break;
        }
        case 'S': {
            const double x2 = reader.number(), y2 = reader.number(), x = reader.number(), y = reader.number();
            const bool smooth = previous.toUpper() == QLatin1Char('C') || previous.toUpper() == QLatin1Char('S');
            const QPointF c1 = smooth ? current * 2 - control : current;
            control = base + QPointF(x2, y2);
            path.cubicTo(c1, control, base + QPointF(x, y));
            current = base + QPointF(x, y);
            break;
        }
        case 'Q': {
            const double x1 = reader.number(), y1 = reader.number(), x = reader.number(), y = reader.number();
            control = base + QPointF(x1, y1);
            path.quadTo(control, base + QPointF(x, y));
            current = base + QPointF(x, y);
            break;
        }
        case 'T': {
            const double x = reader.number(), y = reader.number();
            const bool smooth = previous.toUpper() == QLatin1Char('Q') || previous.toUpper() == QLatin1Char('T');
            control = smooth ? current * 2 - control : current;
            path.quadTo(control, base + QPointF(x, y));
            current = base + QPointF(x, y);
            break;
        }
        case 'A': {
            const double rx = reader.number(), ry = reader.number(), turn = reader.number();
            const bool large = reader.flag();
            const bool sweep = reader.flag();
            const double x = reader.number(), y = reader.number();
            const QPointF to = base + QPointF(x, y);
            arcTo(path, current, rx, ry, turn, large, sweep, to);
            current = to;
            break;
        }
        case 'Z':
            path.closeSubpath();
            current = start;
            break;
        default:
            // An unknown letter: nothing more can be trusted.
            return path;
        }
        previous = command;
    }
    return path;
}

// --- Style ----------------------------------------------------------------------

QJsonObject Style::toJson() const
{
    const Style plain;
    QJsonObject json;
    // Only what differs from a plain style is written: files stay small and
    // a new default does not rewrite old drawings.
    const auto put = [&json](const char *key, const QJsonValue &value, bool differs) {
        if (differs) {
            json.insert(QLatin1String(key), value);
        }
    };
    put("fillKind", fillKind, fillKind != plain.fillKind);
    put("fill", Model::colorName(fill), fill != plain.fill);
    put("fill2", Model::colorName(fill2), fill2 != plain.fill2);
    put("gradientAngle", gradientAngle, gradientAngle != plain.gradientAngle);
    put("stroke", Model::colorName(stroke), stroke != plain.stroke);
    put("strokeWidth", strokeWidth, strokeWidth != plain.strokeWidth);
    put("dash", dash, dash != plain.dash);
    put("startArrow", startArrow, startArrow != plain.startArrow);
    put("endArrow", endArrow, endArrow != plain.endArrow);
    put("radius", radius, radius != plain.radius);
    put("shadow", shadow, shadow != plain.shadow);
    put("opacity", opacity, opacity != plain.opacity);
    put("fontFamily", fontFamily, fontFamily != plain.fontFamily);
    put("fontSize", fontSize, fontSize != plain.fontSize);
    put("bold", bold, bold != plain.bold);
    put("italic", italic, italic != plain.italic);
    put("underline", underline, underline != plain.underline);
    put("textColor", Model::colorName(textColor), textColor != plain.textColor);
    put("hAlign", hAlign, hAlign != plain.hAlign);
    put("vAlign", vAlign, vAlign != plain.vAlign);
    return json;
}

Style Style::fromJson(const QJsonObject &json)
{
    Style s;
    const auto text = [&json](const char *key, QString &into) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isString()) {
            into = v.toString();
        }
    };
    const auto real = [&json](const char *key, double &into) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isDouble()) {
            into = v.toDouble();
        }
    };
    const auto flag = [&json](const char *key, bool &into) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isBool()) {
            into = v.toBool();
        }
    };
    const auto color = [&json](const char *key, QColor &into) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isString()) {
            into = Model::color(v.toString(), into);
        }
    };
    text("fillKind", s.fillKind);
    color("fill", s.fill);
    color("fill2", s.fill2);
    real("gradientAngle", s.gradientAngle);
    color("stroke", s.stroke);
    real("strokeWidth", s.strokeWidth);
    text("dash", s.dash);
    text("startArrow", s.startArrow);
    text("endArrow", s.endArrow);
    real("radius", s.radius);
    flag("shadow", s.shadow);
    real("opacity", s.opacity);
    text("fontFamily", s.fontFamily);
    real("fontSize", s.fontSize);
    flag("bold", s.bold);
    flag("italic", s.italic);
    flag("underline", s.underline);
    color("textColor", s.textColor);
    text("hAlign", s.hAlign);
    text("vAlign", s.vAlign);
    s.strokeWidth = qBound(0.0, s.strokeWidth, 200.0);
    s.opacity = qBound(0.0, s.opacity, 1.0);
    s.fontSize = qBound(1.0, s.fontSize, 2000.0);
    s.radius = qMax(0.0, s.radius);
    return s;
}

// --- Shape ----------------------------------------------------------------------

QTransform Shape::transform() const
{
    QTransform t;
    if (rotation != 0) {
        const QPointF c = center();
        t.translate(c.x(), c.y());
        t.rotate(rotation);
        t.translate(-c.x(), -c.y());
    }
    t.translate(x, y);
    return t;
}

QRectF Shape::bounds() const
{
    if (isLine()) {
        return QRectF(p1, p2).normalized();
    }
    if (rotation == 0) {
        return box();
    }
    return transform().mapRect(QRectF(0, 0, w, h));
}

bool Shape::operator==(const Shape &o) const
{
    return id == o.id && kind == o.kind && form == o.form && sides == o.sides && name == o.name && x == o.x && y == o.y && w == o.w && h == o.h
        && rotation == o.rotation && text == o.text && locked == o.locked && hidden == o.hidden && group == o.group && style == o.style && p1 == o.p1
        && p2 == o.p2 && route == o.route && from == o.from && to == o.to && path == o.path && ref == o.ref && autoSize == o.autoSize && image == o.image;
}

QJsonObject Shape::toJson() const
{
    QJsonObject json;
    json.insert(QLatin1String("id"), id);
    json.insert(QLatin1String("kind"), kind);
    if (!name.isEmpty()) {
        json.insert(QLatin1String("name"), name);
    }
    if (kind == QLatin1String("shape")) {
        json.insert(QLatin1String("form"), form);
        if (form == QLatin1String("polygon") || form == QLatin1String("star")) {
            json.insert(QLatin1String("sides"), sides);
        }
    }
    if (isLine()) {
        json.insert(QLatin1String("p1"), QJsonArray{p1.x(), p1.y()});
        json.insert(QLatin1String("p2"), QJsonArray{p2.x(), p2.y()});
        json.insert(QLatin1String("route"), route);
        if (!from.isEmpty()) {
            json.insert(QLatin1String("from"), from);
        }
        if (!to.isEmpty()) {
            json.insert(QLatin1String("to"), to);
        }
    } else {
        json.insert(QLatin1String("x"), x);
        json.insert(QLatin1String("y"), y);
        json.insert(QLatin1String("w"), w);
        json.insert(QLatin1String("h"), h);
        if (rotation != 0) {
            json.insert(QLatin1String("rotation"), rotation);
        }
    }
    if (kind == QLatin1String("path")) {
        json.insert(QLatin1String("d"), Model::pathToD(path, 3));
        json.insert(QLatin1String("ref"), QJsonArray{ref.x(), ref.y(), ref.width(), ref.height()});
        if (path.fillRule() == Qt::WindingFill) {
            json.insert(QLatin1String("winding"), true);
        }
    }
    if (!text.isEmpty()) {
        json.insert(QLatin1String("text"), text);
    }
    if (autoSize) {
        json.insert(QLatin1String("autoSize"), true);
    }
    if (!image.isEmpty()) {
        json.insert(QLatin1String("image"), image);
    }
    if (locked) {
        json.insert(QLatin1String("locked"), true);
    }
    if (hidden) {
        json.insert(QLatin1String("hidden"), true);
    }
    if (!group.isEmpty()) {
        json.insert(QLatin1String("group"), group);
    }
    const QJsonObject styleJson = style.toJson();
    if (!styleJson.isEmpty()) {
        json.insert(QLatin1String("style"), styleJson);
    }
    return json;
}

static QPointF pointFrom(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return a.size() == 2 ? QPointF(a.at(0).toDouble(), a.at(1).toDouble()) : QPointF();
}

Shape Shape::fromJson(const QJsonObject &json)
{
    Shape s;
    s.id = json.value(QLatin1String("id")).toString();
    s.kind = json.value(QLatin1String("kind")).toString(QStringLiteral("shape"));
    static const QStringList kinds{QStringLiteral("shape"), QStringLiteral("line"), QStringLiteral("path"), QStringLiteral("text"),
                                   QStringLiteral("image"), QStringLiteral("note"), QStringLiteral("frame")};
    if (!kinds.contains(s.kind)) {
        s.kind = QStringLiteral("shape");
    }
    s.name = json.value(QLatin1String("name")).toString();
    s.form = json.value(QLatin1String("form")).toString(QStringLiteral("rect"));
    s.sides = qBound(3, json.value(QLatin1String("sides")).toInt(5), 64);
    s.x = json.value(QLatin1String("x")).toDouble();
    s.y = json.value(QLatin1String("y")).toDouble();
    s.w = qMax(0.0, json.value(QLatin1String("w")).toDouble());
    s.h = qMax(0.0, json.value(QLatin1String("h")).toDouble());
    s.rotation = json.value(QLatin1String("rotation")).toDouble();
    s.text = json.value(QLatin1String("text")).toString();
    s.autoSize = json.value(QLatin1String("autoSize")).toBool();
    s.image = json.value(QLatin1String("image")).toString();
    s.locked = json.value(QLatin1String("locked")).toBool();
    s.hidden = json.value(QLatin1String("hidden")).toBool();
    s.group = json.value(QLatin1String("group")).toString();
    s.style = Style::fromJson(json.value(QLatin1String("style")).toObject());
    if (s.isLine()) {
        s.p1 = pointFrom(json.value(QLatin1String("p1")));
        s.p2 = pointFrom(json.value(QLatin1String("p2")));
        s.route = json.value(QLatin1String("route")).toString(QStringLiteral("straight"));
        s.from = json.value(QLatin1String("from")).toString();
        s.to = json.value(QLatin1String("to")).toString();
        const QRectF r = QRectF(s.p1, s.p2).normalized();
        s.x = r.x();
        s.y = r.y();
        s.w = r.width();
        s.h = r.height();
    }
    if (s.kind == QLatin1String("path")) {
        s.path = Model::pathFromD(json.value(QLatin1String("d")).toString());
        s.path.setFillRule(json.value(QLatin1String("winding")).toBool() ? Qt::WindingFill : Qt::OddEvenFill);
        const QJsonArray r = json.value(QLatin1String("ref")).toArray();
        s.ref = r.size() == 4 ? QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble()) : s.path.boundingRect();
    }
    return s;
}

// --- Document -------------------------------------------------------------------

int Document::indexOf(const QString &id) const
{
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

const Shape *Document::find(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &shapes.at(i);
}

Shape *Document::find(const QString &id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &shapes[i];
}

QString Document::freshId()
{
    QString id;
    do {
        id = QStringLiteral("o%1").arg(++m_serial);
    } while (contains(id));
    return id;
}

QString Document::freshName(const QString &base) const
{
    int highest = 0;
    for (const Shape &s : shapes) {
        if (s.name.startsWith(base + QLatin1Char(' '))) {
            highest = qMax(highest, s.name.mid(base.size() + 1).toInt());
        }
    }
    return QStringLiteral("%1 %2").arg(base).arg(highest + 1);
}

QList<const Shape *> Document::frames() const
{
    QList<const Shape *> list;
    for (const Shape &s : shapes) {
        if (s.isFrame()) {
            list << &s;
        }
    }
    return list;
}

QRectF Document::bounds() const
{
    QRectF all;
    for (const Shape &s : shapes) {
        if (!s.hidden) {
            const QRectF b = s.bounds().adjusted(-s.style.strokeWidth / 2, -s.style.strokeWidth / 2, s.style.strokeWidth / 2, s.style.strokeWidth / 2);
            all = all.isNull() ? b : all.united(b);
        }
    }
    return all;
}

QString Document::addResource(const QByteArray &bytes, const QString &suffix)
{
    for (auto it = resources.constBegin(); it != resources.constEnd(); ++it) {
        if (it.value() == bytes) {
            return it.key();
        }
    }
    QString name;
    int n = resources.size();
    do {
        name = QStringLiteral("image%1.%2").arg(++n).arg(suffix.isEmpty() ? QStringLiteral("png") : suffix);
    } while (resources.contains(name));
    resources.insert(name, bytes);
    return name;
}

void Document::pruneResources()
{
    QStringList used;
    for (const Shape &s : shapes) {
        if (!s.image.isEmpty()) {
            used << s.image;
        }
    }
    for (auto it = resources.begin(); it != resources.end();) {
        it = used.contains(it.key()) ? std::next(it) : resources.erase(it);
    }
}

QJsonObject Document::toJson() const
{
    QJsonObject json;
    json.insert(QLatin1String("format"), 1);
    json.insert(QLatin1String("uid"), uid);
    json.insert(QLatin1String("title"), title);
    json.insert(QLatin1String("grid"), grid);
    json.insert(QLatin1String("snap"), snap);
    json.insert(QLatin1String("serial"), m_serial);
    json.insert(QLatin1String("palette"), QJsonArray::fromStringList(palette));
    QJsonArray list;
    for (const Shape &s : shapes) {
        list.append(s.toJson());
    }
    json.insert(QLatin1String("shapes"), list);
    return json;
}

Document Document::fromJson(const QJsonObject &json)
{
    Document doc;
    doc.uid = json.value(QLatin1String("uid")).toString();
    if (doc.uid.isEmpty()) {
        doc.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    doc.title = json.value(QLatin1String("title")).toString();
    doc.grid = json.value(QLatin1String("grid")).toBool();
    doc.snap = json.value(QLatin1String("snap")).toBool(true);
    doc.m_serial = json.value(QLatin1String("serial")).toInt();
    for (const QJsonValue &v : json.value(QLatin1String("palette")).toArray()) {
        if (Model::color(v.toString()).isValid()) {
            doc.palette << v.toString();
        }
    }
    for (const QJsonValue &v : json.value(QLatin1String("shapes")).toArray()) {
        Shape s = Shape::fromJson(v.toObject());
        // Two objects with one id would be picked and undone as one.
        if (s.id.isEmpty() || doc.contains(s.id)) {
            s.id = doc.freshId();
        }
        doc.shapes << s;
    }
    return doc;
}
