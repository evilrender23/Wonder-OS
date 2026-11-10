#include "svg.h"

#include "geometry.h"
#include "render.h"

#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QHash>
#include <QImage>
#include <QRegularExpression>
#include <QtMath>

#include <cmath>

// --- Out ------------------------------------------------------------------------

namespace
{
QString num(double value)
{
    return Model::number(value, 3);
}

QString paint(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QString alpha(const char *attribute, const QColor &color)
{
    return color.alpha() < 255 ? QStringLiteral(" %1=\"%2\"").arg(QLatin1String(attribute), num(color.alphaF())) : QString();
}

QString strokeAttributes(const Style &style, bool forceVisible = false)
{
    double width = style.strokeWidth;
    if (width <= 0 && forceVisible) {
        width = 1;
    }
    if (width <= 0 || style.stroke.alpha() == 0) {
        return QStringLiteral(" stroke=\"none\"");
    }
    QString out = QStringLiteral(" stroke=\"%1\"%2 stroke-width=\"%3\" stroke-linejoin=\"round\"").arg(paint(style.stroke), alpha("stroke-opacity", style.stroke), num(width));
    if (style.dash == QLatin1String("dash")) {
        out += QStringLiteral(" stroke-dasharray=\"%1 %2\"").arg(num(4 * width), num(3 * width));
    } else if (style.dash == QLatin1String("dot")) {
        out += QStringLiteral(" stroke-linecap=\"round\" stroke-dasharray=\"%1 %2\"").arg(num(0.1 * width), num(2.4 * width));
    } else if (style.dash == QLatin1String("dashdot")) {
        out += QStringLiteral(" stroke-dasharray=\"%1 %2 %3 %2\"").arg(num(4 * width), num(2.5 * width), num(width));
    } else {
        out += QStringLiteral(" stroke-linecap=\"round\"");
    }
    return out;
}

QString mimeOf(const QString &name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (suffix == QLatin1String("svg")) {
        return QStringLiteral("image/svg+xml");
    }
    if (suffix == QLatin1String("webp")) {
        return QStringLiteral("image/webp");
    }
    if (suffix == QLatin1String("gif")) {
        return QStringLiteral("image/gif");
    }
    return QStringLiteral("image/png");
}

QString textElements(const Shape &s)
{
    QString out;
    const QFont font = Render::font(s.style);
    QString attributes = QStringLiteral(" font-family=\"%1\" font-size=\"%2\" fill=\"%3\"%4")
        .arg((s.style.fontFamily.isEmpty() ? QStringLiteral("sans-serif") : s.style.fontFamily).toHtmlEscaped(), num(font.pixelSize()), paint(s.style.textColor),
             alpha("fill-opacity", s.style.textColor));
    if (s.style.bold) {
        attributes += QStringLiteral(" font-weight=\"bold\"");
    }
    if (s.style.italic) {
        attributes += QStringLiteral(" font-style=\"italic\"");
    }
    if (s.style.underline) {
        attributes += QStringLiteral(" text-decoration=\"underline\"");
    }
    for (const Render::TextLine &line : Render::layout(s)) {
        if (line.text.isEmpty()) {
            continue;
        }
        out += QStringLiteral("<text x=\"%1\" y=\"%2\"%3 xml:space=\"preserve\">%4</text>\n")
            .arg(num(line.origin.x()), num(line.origin.y()), attributes, line.text.toHtmlEscaped());
    }
    return out;
}
}

QByteArray Svg::write(const Document &doc, const QRectF &area)
{
    QString defs;
    QString body;
    int gradients = 0;
    bool shadow = false;
    for (const Shape &s : doc.shapes) {
        if (s.hidden || !s.bounds().adjusted(-40, -40, 40, 40).intersects(area)) {
            continue;
        }
        const Style &st = s.style;
        QString group = QStringLiteral("<g");
        if (st.opacity < 1) {
            group += QStringLiteral(" opacity=\"%1\"").arg(num(st.opacity));
        }
        if (s.isLine()) {
            body += group + QStringLiteral(">\n");
            body += QStringLiteral("<path d=\"%1\" fill=\"none\"%2/>\n").arg(Model::pathToD(Geometry::linePath(s), 3), strokeAttributes(st, true));
            for (bool atEnd : {false, true}) {
                bool filled = false;
                const QPainterPath head = Geometry::arrowHead(s, atEnd, &filled);
                if (head.isEmpty()) {
                    continue;
                }
                if (filled) {
                    body += QStringLiteral("<path d=\"%1\" fill=\"%2\"%3/>\n").arg(Model::pathToD(head, 3), paint(st.stroke), alpha("fill-opacity", st.stroke));
                } else {
                    body += QStringLiteral("<path d=\"%1\" fill=\"none\" stroke=\"%2\" stroke-width=\"%3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n")
                        .arg(Model::pathToD(head, 3), paint(st.stroke), num(qMax(1.0, st.strokeWidth)));
                }
            }
            if (!s.text.isEmpty()) {
                body += QStringLiteral("<g transform=\"translate(%1 %2)\">\n%3</g>\n").arg(num(s.x), num(s.y), textElements(s));
            }
            body += QStringLiteral("</g>\n");
            continue;
        }
        const QTransform t = s.transform();
        if (t.isTranslating() && !t.isRotating() && !t.isScaling()) {
            group += QStringLiteral(" transform=\"translate(%1 %2)\"").arg(num(t.dx()), num(t.dy()));
        } else {
            group += QStringLiteral(" transform=\"matrix(%1 %2 %3 %4 %5 %6)\"").arg(Model::number(t.m11(), 6), Model::number(t.m12(), 6), Model::number(t.m21(), 6),
                                                                                   Model::number(t.m22(), 6), num(t.dx()), num(t.dy()));
        }
        body += group + QStringLiteral(">\n");

        QString fill = QStringLiteral(" fill=\"none\"");
        if (st.fillKind == QLatin1String("solid")) {
            fill = QStringLiteral(" fill=\"%1\"%2").arg(paint(st.fill), alpha("fill-opacity", st.fill));
        } else if (st.fillKind == QLatin1String("linear") || st.fillKind == QLatin1String("radial")) {
            const QString id = QStringLiteral("grad%1").arg(++gradients);
            const QString stops = QStringLiteral("<stop offset=\"0\" stop-color=\"%1\"%2/><stop offset=\"1\" stop-color=\"%3\"%4/>")
                .arg(paint(st.fill), alpha("stop-opacity", st.fill), paint(st.fill2), alpha("stop-opacity", st.fill2));
            if (st.fillKind == QLatin1String("linear")) {
                const double a = qDegreesToRadians(st.gradientAngle);
                const QPointF dir(std::cos(a), std::sin(a));
                const double reach = (qAbs(dir.x()) * s.w + qAbs(dir.y()) * s.h) / 2;
                const QPointF c(s.w / 2, s.h / 2);
                const QPointF from = c - dir * reach;
                const QPointF to = c + dir * reach;
                defs += QStringLiteral("<linearGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\">%6</linearGradient>\n")
                    .arg(id, num(from.x()), num(from.y()), num(to.x()), num(to.y()), stops);
            } else {
                defs += QStringLiteral("<radialGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" cx=\"%2\" cy=\"%3\" r=\"%4\">%5</radialGradient>\n")
                    .arg(id, num(s.w / 2), num(s.h / 2), num(qMax(s.w, s.h) / 2), stops);
            }
            fill = QStringLiteral(" fill=\"url(#%1)\"").arg(id);
        }
        QString effect;
        if (st.shadow && st.fillKind != QLatin1String("none") && !s.isFrame() && s.kind != QLatin1String("image") && s.kind != QLatin1String("text")) {
            shadow = true;
            effect = QStringLiteral(" filter=\"url(#shadow)\"");
        }

        if (s.isFrame()) {
            body += QStringLiteral("<rect width=\"%1\" height=\"%2\"%3/>\n").arg(num(s.w), num(s.h), fill);
        } else if (s.kind == QLatin1String("image")) {
            const QByteArray bytes = doc.resources.value(s.image);
            if (!bytes.isEmpty()) {
                body += QStringLiteral("<image width=\"%1\" height=\"%2\" preserveAspectRatio=\"none\" xlink:href=\"data:%3;base64,%4\"/>\n")
                    .arg(num(s.w), num(s.h), mimeOf(s.image), QString::fromLatin1(bytes.toBase64()));
            }
            if (st.strokeWidth > 0) {
                body += QStringLiteral("<rect width=\"%1\" height=\"%2\" fill=\"none\"%3/>\n").arg(num(s.w), num(s.h), strokeAttributes(st));
            }
        } else if (s.kind == QLatin1String("text") && st.fillKind == QLatin1String("none") && st.strokeWidth <= 0) {
            // Only words.
        } else if (s.kind == QLatin1String("shape") && s.form == QLatin1String("rect")) {
            const double r = qMin(st.radius, qMin(s.w, s.h) / 2);
            body += QStringLiteral("<rect width=\"%1\" height=\"%2\"%3%4%5%6/>\n")
                .arg(num(s.w), num(s.h), r > 0 ? QStringLiteral(" rx=\"%1\"").arg(num(r)) : QString(), fill, strokeAttributes(st), effect);
        } else if (s.kind == QLatin1String("shape") && s.form == QLatin1String("ellipse")) {
            body += QStringLiteral("<ellipse cx=\"%1\" cy=\"%2\" rx=\"%1\" ry=\"%2\"%3%4%5/>\n").arg(num(s.w / 2), num(s.h / 2), fill, strokeAttributes(st), effect);
        } else {
            body += QStringLiteral("<path d=\"%1\"%2%3%4/>\n").arg(Model::pathToD(Geometry::outline(s), 3), fill, strokeAttributes(st), effect);
        }
        if (!s.text.isEmpty() && !s.isFrame() && s.kind != QLatin1String("image")) {
            body += textElements(s);
        }
        body += QStringLiteral("</g>\n");
    }
    if (shadow) {
        defs += QStringLiteral("<filter id=\"shadow\" x=\"-30%\" y=\"-30%\" width=\"160%\" height=\"180%\"><feDropShadow dx=\"0\" dy=\"4\" stdDeviation=\"6\" "
                               "flood-color=\"#000000\" flood-opacity=\"0.25\"/></filter>\n");
    }
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                                 "width=\"%1\" height=\"%2\" viewBox=\"%3 %4 %1 %2\">\n")
        .arg(num(area.width()), num(area.height()), num(area.x()), num(area.y()));
    if (!doc.title.isEmpty()) {
        out += QStringLiteral("<title>%1</title>\n").arg(doc.title.toHtmlEscaped());
    }
    if (!defs.isEmpty()) {
        out += QStringLiteral("<defs>\n") + defs + QStringLiteral("</defs>\n");
    }
    out += body + QStringLiteral("</svg>\n");
    return out.toUtf8();
}

// --- In -------------------------------------------------------------------------

namespace
{
struct Context {
    QTransform transform;
    QString fill = QStringLiteral("black");
    QString stroke = QStringLiteral("none");
    QString color = QStringLiteral("#232629");
    double strokeWidth = 1;
    double opacity = 1;
    double fillOpacity = 1;
    double strokeOpacity = 1;
    QString dash;
    QString fillRule;
    double fontSize = 16;
    QString fontFamily;
    bool bold = false;
    bool italic = false;
    QString anchor;
    bool visible = true;
};

struct Reader {
    QList<Shape> shapes;
    QMap<QString, QByteArray> resources;
    QHash<QString, QDomElement> byId;
    /// Style sheet rules, by selector: `rect`, `.name` or `#id`.
    QHash<QString, QHash<QString, QString>> classes;
    QString baseDir;
    QSizeF viewport = QSizeF(100, 100);
    int serial = 0;
    int depth = 0;

    static double length(const QString &raw, double reference, double fallback = 0)
    {
        QString text = raw.trimmed();
        if (text.isEmpty()) {
            return fallback;
        }
        double factor = 1;
        static const struct { const char *unit; double factor; } units[] = {
            {"px", 1}, {"mm", 96 / 25.4}, {"cm", 96 / 2.54}, {"in", 96}, {"pt", 96.0 / 72}, {"pc", 16}, {"em", 16}, {"rem", 16}, {"ex", 8}};
        if (text.endsWith(QLatin1Char('%'))) {
            text.chop(1);
            factor = reference / 100;
        } else {
            for (const auto &unit : units) {
                if (text.endsWith(QLatin1String(unit.unit))) {
                    text.chop(int(qstrlen(unit.unit)));
                    factor = unit.factor;
                    break;
                }
            }
        }
        bool ok = false;
        const double value = text.trimmed().toDouble(&ok);
        return ok && std::isfinite(value) ? value * factor : fallback;
    }

    static QList<double> numbers(const QString &text)
    {
        static const QRegularExpression pattern(QStringLiteral("[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?"));
        QList<double> list;
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            list << it.next().captured().toDouble();
        }
        return list;
    }

    static QTransform transformOf(const QString &text)
    {
        static const QRegularExpression pattern(QStringLiteral("(matrix|translate|scale|rotate|skewX|skewY)\\s*\\(([^)]*)\\)"));
        QTransform total;
        auto it = pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto match = it.next();
            const QString name = match.captured(1);
            const QList<double> v = numbers(match.captured(2));
            QTransform one;
            if (name == QLatin1String("matrix") && v.size() >= 6) {
                one = QTransform(v.at(0), v.at(1), v.at(2), v.at(3), v.at(4), v.at(5));
            } else if (name == QLatin1String("translate") && !v.isEmpty()) {
                one = QTransform::fromTranslate(v.at(0), v.value(1, 0));
            } else if (name == QLatin1String("scale") && !v.isEmpty()) {
                one = QTransform::fromScale(v.at(0), v.value(1, v.at(0)));
            } else if (name == QLatin1String("rotate") && !v.isEmpty()) {
                if (v.size() >= 3) {
                    one.translate(v.at(1), v.at(2));
                    one.rotate(v.at(0));
                    one.translate(-v.at(1), -v.at(2));
                } else {
                    one.rotate(v.at(0));
                }
            } else if (name == QLatin1String("skewX") && !v.isEmpty()) {
                one.shear(std::tan(qDegreesToRadians(v.at(0))), 0);
            } else if (name == QLatin1String("skewY") && !v.isEmpty()) {
                one.shear(0, std::tan(qDegreesToRadians(v.at(0))));
            }
            // The first written is the outermost.
            total = one * total;
        }
        return total;
    }

    static QColor colorOf(const QString &raw, const Context &context)
    {
        const QString text = raw.trimmed();
        if (text.compare(QLatin1String("currentColor"), Qt::CaseInsensitive) == 0) {
            return colorOf(context.color, Context());
        }
        if (text.startsWith(QLatin1String("rgb"), Qt::CaseInsensitive)) {
            const bool percent = text.contains(QLatin1Char('%'));
            const QList<double> v = numbers(text);
            if (v.size() >= 3) {
                const double k = percent ? 2.55 : 1;
                QColor color(qBound(0, qRound(v.at(0) * k), 255), qBound(0, qRound(v.at(1) * k), 255), qBound(0, qRound(v.at(2) * k), 255));
                if (v.size() >= 4) {
                    color.setAlphaF(qBound(0.0, v.at(3), 1.0));
                }
                return color;
            }
            return {};
        }
        if (text.startsWith(QLatin1Char('#')) && text.size() == 9) {
            // #rrggbbaa in CSS; Qt reads eight digits as #aarrggbb.
            return QColor::fromString(QStringLiteral("#") + text.mid(7, 2) + text.mid(1, 6));
        }
        if (text.startsWith(QLatin1Char('#')) && text.size() == 5) {
            const QString wide = QStringLiteral("#%1%1%2%2%3%3%4%4").arg(text.at(4)).arg(text.at(1)).arg(text.at(2)).arg(text.at(3));
            return QColor::fromString(wide);
        }
        return QColor::fromString(text.toLower());
    }

    /// What an element says of itself: attributes, then class rules, then `style`.
    QHash<QString, QString> declared(const QDomElement &element) const
    {
        QHash<QString, QString> map;
        const QDomNamedNodeMap attributes = element.attributes();
        for (int i = 0; i < attributes.count(); ++i) {
            const QDomAttr attribute = attributes.item(i).toAttr();
            map.insert(attribute.name(), attribute.value());
        }
        // The rules of a style sheet, from the loosest to the tightest: by
        // tag, by class, by id.
        QStringList names{element.tagName()};
        for (const QString &name : element.attribute(QStringLiteral("class")).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            names << QLatin1Char('.') + name;
        }
        if (element.hasAttribute(QStringLiteral("id"))) {
            names << QLatin1Char('#') + element.attribute(QStringLiteral("id"));
        }
        for (const QString &name : std::as_const(names)) {
            const auto rule = classes.constFind(name);
            if (rule != classes.constEnd()) {
                for (auto it = rule->constBegin(); it != rule->constEnd(); ++it) {
                    map.insert(it.key(), it.value());
                }
            }
        }
        const QStringList declarations = element.attribute(QStringLiteral("style")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &declaration : declarations) {
            const int colon = declaration.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                map.insert(declaration.left(colon).trimmed(), declaration.mid(colon + 1).trimmed());
            }
        }
        return map;
    }

    Context inherit(const Context &parent, const QHash<QString, QString> &own) const
    {
        Context c = parent;
        // Opacity is not inherited, it multiplies down.
        const auto has = [&own](const char *key) { return own.contains(QLatin1String(key)); };
        const auto get = [&own](const char *key) { return own.value(QLatin1String(key)); };
        if (has("transform")) {
            c.transform = transformOf(get("transform")) * parent.transform;
        }
        if (has("color")) {
            c.color = get("color");
        }
        if (has("fill")) {
            c.fill = get("fill");
        }
        if (has("stroke")) {
            c.stroke = get("stroke");
        }
        if (has("stroke-width")) {
            c.strokeWidth = length(get("stroke-width"), viewport.width(), c.strokeWidth);
        }
        if (has("opacity")) {
            c.opacity = parent.opacity * qBound(0.0, length(get("opacity"), 1, 1), 1.0);
            // Drawn with no ink at all: as good as not there.
            if (c.opacity <= 0.001) {
                c.visible = false;
            }
        }
        if (has("fill-opacity")) {
            c.fillOpacity = qBound(0.0, length(get("fill-opacity"), 1, 1), 1.0);
        }
        if (has("stroke-opacity")) {
            c.strokeOpacity = qBound(0.0, length(get("stroke-opacity"), 1, 1), 1.0);
        }
        if (has("stroke-dasharray")) {
            c.dash = get("stroke-dasharray");
        }
        if (has("fill-rule")) {
            c.fillRule = get("fill-rule");
        }
        if (has("font-size")) {
            c.fontSize = length(get("font-size"), parent.fontSize, c.fontSize);
        }
        if (has("font-family")) {
            c.fontFamily = get("font-family").split(QLatin1Char(',')).value(0).trimmed().remove(QLatin1Char('\'')).remove(QLatin1Char('"'));
        }
        if (has("font-weight")) {
            const QString weight = get("font-weight");
            c.bold = weight == QLatin1String("bold") || weight == QLatin1String("bolder") || weight.toInt() >= 600;
        }
        if (has("font-style")) {
            c.italic = get("font-style") != QLatin1String("normal");
        }
        if (has("text-anchor")) {
            c.anchor = get("text-anchor");
        }
        if (get("display") == QLatin1String("none") || get("visibility") == QLatin1String("hidden")) {
            c.visible = false;
        }
        return c;
    }

    /// The stops of a gradient, following the gradients it borrows from.
    bool gradient(const QString &id, QString *kind, QColor *first, QColor *last, const Context &context) const
    {
        QDomElement element = byId.value(id);
        const QString tag = element.tagName();
        if (tag != QLatin1String("linearGradient") && tag != QLatin1String("radialGradient")) {
            return false;
        }
        *kind = tag == QLatin1String("linearGradient") ? QStringLiteral("linear") : QStringLiteral("radial");
        for (int hops = 0; hops < 8 && !element.isNull(); ++hops) {
            QList<QColor> stops;
            for (QDomElement stop = element.firstChildElement(QStringLiteral("stop")); !stop.isNull(); stop = stop.nextSiblingElement(QStringLiteral("stop"))) {
                const QHash<QString, QString> own = declared(stop);
                QColor color = colorOf(own.value(QStringLiteral("stop-color"), QStringLiteral("black")), context);
                if (!color.isValid()) {
                    color = Qt::black;
                }
                color.setAlphaF(color.alphaF() * qBound(0.0, length(own.value(QStringLiteral("stop-opacity")), 1, 1), 1.0));
                stops << color;
            }
            if (!stops.isEmpty()) {
                *first = stops.first();
                *last = stops.last();
                return true;
            }
            QString link = element.attribute(QStringLiteral("xlink:href"), element.attribute(QStringLiteral("href")));
            element = link.startsWith(QLatin1Char('#')) ? byId.value(link.mid(1)) : QDomElement();
        }
        return false;
    }

    Style styleOf(const Context &c) const
    {
        Style style;
        const double scale = std::sqrt(qAbs(c.transform.determinant()));
        style.opacity = qBound(0.05, c.opacity, 1.0);
        static const QRegularExpression url(QStringLiteral("url\\(\\s*['\"]?#([^)'\"\\s]+)"));
        const auto linked = url.match(c.fill);
        if (c.fill.trimmed() == QLatin1String("none") || c.fill.trimmed() == QLatin1String("transparent")) {
            style.fillKind = QStringLiteral("none");
        } else if (linked.hasMatch()) {
            QString kind;
            QColor first;
            QColor last;
            if (gradient(linked.captured(1), &kind, &first, &last, c)) {
                style.fillKind = kind;
                style.fill = first;
                style.fill2 = last;
                style.gradientAngle = 0;
            } else {
                style.fillKind = QStringLiteral("solid");
                style.fill = QColor(QStringLiteral("#9a9aa0"));
            }
        } else {
            QColor color = colorOf(c.fill, c);
            if (!color.isValid()) {
                color = Qt::black;
            }
            color.setAlphaF(color.alphaF() * c.fillOpacity);
            style.fillKind = QStringLiteral("solid");
            style.fill = color;
        }
        QColor stroke = c.stroke.trimmed() == QLatin1String("none") || c.stroke.contains(QLatin1String("url(")) ? QColor() : colorOf(c.stroke, c);
        if (stroke.isValid() && c.strokeWidth > 0) {
            stroke.setAlphaF(stroke.alphaF() * c.strokeOpacity);
            style.stroke = stroke;
            style.strokeWidth = c.strokeWidth * scale;
        } else {
            style.strokeWidth = 0;
        }
        const QList<double> dashes = numbers(c.dash);
        if (dashes.size() >= 4) {
            style.dash = QStringLiteral("dashdot");
        } else if (!dashes.isEmpty() && dashes.first() > 0) {
            style.dash = dashes.first() <= c.strokeWidth * 1.5 ? QStringLiteral("dot") : QStringLiteral("dash");
        }
        return style;
    }

    Shape blank(const Context &c)
    {
        Shape s;
        s.id = QStringLiteral("svg%1").arg(++serial);
        s.style = styleOf(c);
        return s;
    }

    void addPath(QPainterPath local, const Context &c)
    {
        if (local.isEmpty()) {
            return;
        }
        if (c.fillRule == QLatin1String("evenodd")) {
            local.setFillRule(Qt::OddEvenFill);
        } else {
            local.setFillRule(Qt::WindingFill);
        }
        Shape s = blank(c);
        if (s.style.fillKind == QLatin1String("none") && s.style.strokeWidth <= 0) {
            return;
        }
        s.kind = QStringLiteral("path");
        s.path = c.transform.map(local);
        s.ref = s.path.boundingRect();
        if (s.ref.width() < 0.01) {
            s.ref.adjust(-0.5, 0, 0.5, 0);
        }
        if (s.ref.height() < 0.01) {
            s.ref.adjust(0, -0.5, 0, 0.5);
        }
        s.x = s.ref.x();
        s.y = s.ref.y();
        s.w = s.ref.width();
        s.h = s.ref.height();
        shapes << s;
    }

    /// Only moved and stretched: a box stays a box.
    static bool upright(const QTransform &t)
    {
        return qFuzzyIsNull(t.m12()) && qFuzzyIsNull(t.m21()) && t.m11() > 0 && t.m22() > 0;
    }

    /// Turned and scaled the same both ways, not sheared nor flipped: a box
    /// stays a box, turned.
    static bool turned(const QTransform &t)
    {
        const double scale = std::hypot(t.m11(), t.m12());
        return scale > 0 && qAbs(t.m11() - t.m22()) < 1e-6 * scale && qAbs(t.m12() + t.m21()) < 1e-6 * scale;
    }

    void addBox(const QRectF &local, const QString &form, double radius, const Context &c)
    {
        if (local.width() <= 0 || local.height() <= 0) {
            return;
        }
        const QTransform &t = c.transform;
        if (!upright(t) && turned(t)) {
            Shape s = blank(c);
            if (s.style.fillKind == QLatin1String("none") && s.style.strokeWidth <= 0) {
                return;
            }
            const double scale = std::hypot(t.m11(), t.m12());
            const QPointF centre = t.map(local.center());
            s.kind = QStringLiteral("shape");
            s.form = form;
            s.w = local.width() * scale;
            s.h = local.height() * scale;
            s.x = centre.x() - s.w / 2;
            s.y = centre.y() - s.h / 2;
            s.rotation = qRadiansToDegrees(std::atan2(t.m12(), t.m11()));
            s.style.radius = radius * scale;
            shapes << s;
            return;
        }
        if (!upright(t)) {
            QPainterPath path;
            if (form == QLatin1String("ellipse")) {
                path.addEllipse(local);
            } else if (radius > 0) {
                path.addRoundedRect(local, radius, radius);
            } else {
                path.addRect(local);
            }
            addPath(path, c);
            return;
        }
        Shape s = blank(c);
        if (s.style.fillKind == QLatin1String("none") && s.style.strokeWidth <= 0) {
            return;
        }
        const QRectF box = c.transform.mapRect(local);
        s.kind = QStringLiteral("shape");
        s.form = form;
        s.x = box.x();
        s.y = box.y();
        s.w = box.width();
        s.h = box.height();
        s.style.radius = radius * c.transform.m11();
        shapes << s;
    }

    static QString wordsOf(const QDomElement &element)
    {
        QString words;
        for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
            if (node.isText()) {
                words += node.nodeValue();
            } else if (node.isElement() && node.toElement().tagName() == QLatin1String("tspan")) {
                const QDomElement span = node.toElement();
                // A span that starts its own line.
                if (!words.trimmed().isEmpty() && (span.hasAttribute(QStringLiteral("dy")) || span.hasAttribute(QStringLiteral("y")))
                    && span.hasAttribute(QStringLiteral("x"))) {
                    words += QLatin1Char('\n');
                }
                words += wordsOf(span);
            }
        }
        return words;
    }

    void addText(const QDomElement &element, const QString &left, const QString &baseline, const Context &c)
    {
        QStringList lines;
        for (const QString &line : wordsOf(element).split(QLatin1Char('\n'))) {
            lines << line.simplified();
        }
        while (!lines.isEmpty() && lines.first().isEmpty()) {
            lines.removeFirst();
        }
        while (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
        if (lines.isEmpty()) {
            return;
        }
        const double x = numbers(left).value(0, 0);
        const double y = numbers(baseline).value(0, 0);
        const QTransform &t = c.transform;
        const double scale = std::sqrt(qAbs(t.determinant()));
        Shape s = blank(c);
        const Style painted = s.style;
        s.kind = QStringLiteral("text");
        s.autoSize = true;
        s.text = lines.join(QLatin1Char('\n'));
        s.style = Style();
        s.style.opacity = painted.opacity;
        s.style.fillKind = QStringLiteral("none");
        s.style.strokeWidth = 0;
        s.style.textColor = painted.fillKind == QLatin1String("none") ? (painted.strokeWidth > 0 ? painted.stroke : QColor(Qt::black)) : painted.fill;
        s.style.fontSize = qMax(1.0, c.fontSize * scale);
        s.style.fontFamily = c.fontFamily == QLatin1String("sans-serif") ? QString() : c.fontFamily;
        s.style.bold = c.bold;
        s.style.italic = c.italic;
        s.style.hAlign = c.anchor == QLatin1String("middle") ? QStringLiteral("center") : c.anchor == QLatin1String("end") ? QStringLiteral("right") : QStringLiteral("left");
        s.style.vAlign = QStringLiteral("top");
        const QSizeF size = Render::textSize(s, 0);
        const double pad = 4;
        s.w = size.width() + 2 * pad + 2;
        s.h = size.height() + 2 * pad;
        const double ascent = QFontMetricsF(Render::font(s.style)).ascent();
        // Where the anchor of the first baseline sits inside the box.
        double ax = pad;
        if (s.style.hAlign == QLatin1String("center")) {
            ax = s.w / 2;
        } else if (s.style.hAlign == QLatin1String("right")) {
            ax = s.w - pad;
        }
        const QPointF anchorLocal(ax, pad + ascent);
        const QPointF anchor = t.map(QPointF(x, y));
        const double turn = qRadiansToDegrees(std::atan2(t.m12(), t.m11()));
        const QPointF toCentre = QPointF(s.w / 2, s.h / 2) - anchorLocal;
        const double a = qDegreesToRadians(turn);
        const QPointF centre = anchor + QPointF(toCentre.x() * std::cos(a) - toCentre.y() * std::sin(a), toCentre.x() * std::sin(a) + toCentre.y() * std::cos(a));
        s.x = centre.x() - s.w / 2;
        s.y = centre.y() - s.h / 2;
        s.rotation = qAbs(turn) < 0.01 ? 0 : turn;
        shapes << s;
    }

    void addImage(const QHash<QString, QString> &own, const Context &c)
    {
        const QString link = own.value(QStringLiteral("xlink:href"), own.value(QStringLiteral("href")));
        QByteArray bytes;
        QString suffix = QStringLiteral("png");
        if (link.startsWith(QLatin1String("data:"))) {
            const int comma = link.indexOf(QLatin1Char(','));
            if (comma < 0 || !link.left(comma).contains(QLatin1String("base64"))) {
                return;
            }
            const QString type = link.mid(5, link.indexOf(QLatin1Char(';')) - 5);
            suffix = type.contains(QLatin1String("jpeg")) ? QStringLiteral("jpg") : type.contains(QLatin1String("svg")) ? QStringLiteral("svg") : type.section(QLatin1Char('/'), 1);
            bytes = QByteArray::fromBase64(link.mid(comma + 1).toLatin1());
        } else if (!baseDir.isEmpty() && !link.contains(QLatin1String("://"))) {
            // Only a file beside the SVG, never one somewhere else on the disk.
            const QFileInfo info(baseDir + QLatin1Char('/') + link);
            if (!info.canonicalFilePath().startsWith(QFileInfo(baseDir).canonicalFilePath() + QLatin1Char('/'))) {
                return;
            }
            QFile file(info.filePath());
            if (!file.open(QIODevice::ReadOnly) || file.size() > (64 << 20)) {
                return;
            }
            bytes = file.readAll();
            suffix = info.suffix().toLower();
        }
        if (bytes.isEmpty() || QImage::fromData(bytes).isNull()) {
            return;
        }
        const QRectF local(length(own.value(QStringLiteral("x")), viewport.width()), length(own.value(QStringLiteral("y")), viewport.height()),
                           length(own.value(QStringLiteral("width")), viewport.width()), length(own.value(QStringLiteral("height")), viewport.height()));
        if (local.isEmpty()) {
            return;
        }
        Shape s = blank(c);
        s.kind = QStringLiteral("image");
        s.style = Style();
        s.style.strokeWidth = 0;
        s.style.opacity = qBound(0.05, c.opacity, 1.0);
        const QString name = QStringLiteral("svg-image%1.%2").arg(resources.size() + 1).arg(suffix);
        resources.insert(name, bytes);
        s.image = name;
        const QTransform &t = c.transform;
        const QPointF centre = t.map(local.center());
        s.w = local.width() * std::hypot(t.m11(), t.m12());
        s.h = local.height() * std::hypot(t.m21(), t.m22());
        s.x = centre.x() - s.w / 2;
        s.y = centre.y() - s.h / 2;
        const double turn = qRadiansToDegrees(std::atan2(t.m12(), t.m11()));
        s.rotation = qAbs(turn) < 0.01 ? 0 : turn;
        shapes << s;
    }

    void walk(const QDomElement &element, const Context &parent)
    {
        if (shapes.size() > 20000 || depth > 64) {
            return;
        }
        const QString tag = element.tagName().section(QLatin1Char(':'), -1);
        static const QStringList skipped{QStringLiteral("defs"), QStringLiteral("symbol"), QStringLiteral("clipPath"), QStringLiteral("mask"), QStringLiteral("marker"),
                                         QStringLiteral("pattern"), QStringLiteral("metadata"), QStringLiteral("title"), QStringLiteral("desc"), QStringLiteral("filter"),
                                         QStringLiteral("style"), QStringLiteral("linearGradient"), QStringLiteral("radialGradient"), QStringLiteral("script"),
                                         QStringLiteral("namedview"), QStringLiteral("foreignObject")};
        if (skipped.contains(tag)) {
            return;
        }
        const QHash<QString, QString> own = declared(element);
        const Context c = inherit(parent, own);
        if (!c.visible) {
            return;
        }
        const auto len = [&](const char *key, bool vertical = false) {
            return length(own.value(QLatin1String(key)), vertical ? viewport.height() : viewport.width());
        };
        if (tag == QLatin1String("g") || tag == QLatin1String("a") || tag == QLatin1String("switch") || tag == QLatin1String("svg")) {
            Context inner = c;
            if (tag == QLatin1String("svg") && depth > 0) {
                inner.transform = QTransform::fromTranslate(len("x"), len("y", true)) * c.transform;
            }
            ++depth;
            for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
                walk(child, inner);
            }
            --depth;
        } else if (tag == QLatin1String("use")) {
            const QString link = own.value(QStringLiteral("xlink:href"), own.value(QStringLiteral("href")));
            const QDomElement target = link.startsWith(QLatin1Char('#')) ? byId.value(link.mid(1)) : QDomElement();
            if (!target.isNull()) {
                Context inner = c;
                inner.transform = QTransform::fromTranslate(len("x"), len("y", true)) * c.transform;
                ++depth;
                if (target.tagName() == QLatin1String("symbol")) {
                    for (QDomElement child = target.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
                        walk(child, inner);
                    }
                } else {
                    walk(target, inner);
                }
                --depth;
            }
        } else if (tag == QLatin1String("rect")) {
            double rx = len("rx");
            double ry = len("ry", true);
            if (!own.contains(QStringLiteral("rx"))) {
                rx = ry;
            }
            addBox(QRectF(len("x"), len("y", true), len("width"), len("height", true)), QStringLiteral("rect"), rx, c);
        } else if (tag == QLatin1String("circle")) {
            const double r = len("r");
            addBox(QRectF(len("cx") - r, len("cy", true) - r, 2 * r, 2 * r), QStringLiteral("ellipse"), 0, c);
        } else if (tag == QLatin1String("ellipse")) {
            const double rx = len("rx");
            const double ry = len("ry", true);
            addBox(QRectF(len("cx") - rx, len("cy", true) - ry, 2 * rx, 2 * ry), QStringLiteral("ellipse"), 0, c);
        } else if (tag == QLatin1String("line")) {
            Shape s = blank(c);
            if (s.style.strokeWidth > 0) {
                s.kind = QStringLiteral("line");
                s.p1 = c.transform.map(QPointF(len("x1"), len("y1", true)));
                s.p2 = c.transform.map(QPointF(len("x2"), len("y2", true)));
                const QRectF r = QRectF(s.p1, s.p2).normalized();
                s.x = r.x();
                s.y = r.y();
                s.w = r.width();
                s.h = r.height();
                shapes << s;
            }
        } else if (tag == QLatin1String("polyline") || tag == QLatin1String("polygon")) {
            const QList<double> v = numbers(own.value(QStringLiteral("points")));
            QPainterPath path;
            for (int i = 0; i + 1 < v.size(); i += 2) {
                if (i == 0) {
                    path.moveTo(v.at(0), v.at(1));
                } else {
                    path.lineTo(v.at(i), v.at(i + 1));
                }
            }
            if (tag == QLatin1String("polygon")) {
                path.closeSubpath();
            }
            addPath(path, c);
        } else if (tag == QLatin1String("path")) {
            addPath(Model::pathFromD(own.value(QStringLiteral("d"))), c);
        } else if (tag == QLatin1String("text")) {
            // Some writers wrap the words in spans that carry the place and the look.
            Context look = c;
            QString x = own.value(QStringLiteral("x"));
            QString y = own.value(QStringLiteral("y"));
            for (QDomElement span = element.firstChildElement(QStringLiteral("tspan")); !span.isNull();
                 span = span.firstChildElement(QStringLiteral("tspan"))) {
                const QHash<QString, QString> inner = declared(span);
                if (span.hasAttribute(QStringLiteral("transform"))) {
                    break;
                }
                look = inherit(look, inner);
                if (x.isEmpty() && inner.contains(QStringLiteral("x"))) {
                    x = inner.value(QStringLiteral("x"));
                    y = inner.value(QStringLiteral("y"), y);
                }
            }
            addText(element, x, y, look);
        } else if (tag == QLatin1String("image")) {
            addImage(own, c);
        }
    }

    void index(const QDomElement &element)
    {
        if (element.hasAttribute(QStringLiteral("id"))) {
            byId.insert(element.attribute(QStringLiteral("id")), element);
        }
        if (element.tagName() == QLatin1String("style")) {
            static const QRegularExpression comment(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption);
            static const QRegularExpression rule(QStringLiteral("([^{}]+)\\{([^}]*)\\}"));
            static const QRegularExpression simple(QStringLiteral("^[.#]?[A-Za-z_][\\w-]*$"));
            auto it = rule.globalMatch(element.text().remove(comment));
            while (it.hasNext()) {
                const auto match = it.next();
                // One name alone, or several with commas; anything cleverer is left out.
                for (const QString &part : match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    const QString selector = part.trimmed();
                    if (!simple.match(selector).hasMatch()) {
                        continue;
                    }
                    for (const QString &declaration : match.captured(2).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                        const int colon = declaration.indexOf(QLatin1Char(':'));
                        if (colon > 0) {
                            classes[selector].insert(declaration.left(colon).trimmed(), declaration.mid(colon + 1).trimmed());
                        }
                    }
                }
            }
        }
        for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            index(child);
        }
    }
};
}

bool Svg::read(const QByteArray &bytes, QList<Shape> *shapes, QMap<QString, QByteArray> *resources, QString *error, const QString &baseDir)
{
    QDomDocument dom;
    const QDomDocument::ParseResult parsed = dom.setContent(bytes);
    const QDomElement root = dom.documentElement();
    if (!parsed || root.tagName().section(QLatin1Char(':'), -1) != QLatin1String("svg")) {
        if (error) {
            *error = parsed ? QStringLiteral("no es un SVG") : parsed.errorMessage;
        }
        return false;
    }
    Reader reader;
    reader.baseDir = baseDir;
    reader.index(root);

    const QList<double> box = Reader::numbers(root.attribute(QStringLiteral("viewBox")));
    Context context;
    double width = Reader::length(root.attribute(QStringLiteral("width")), box.value(2, 100), box.value(2, 0));
    double height = Reader::length(root.attribute(QStringLiteral("height")), box.value(3, 100), box.value(3, 0));
    if (box.size() == 4 && box.at(2) > 0 && box.at(3) > 0) {
        if (width <= 0) {
            width = box.at(2);
        }
        if (height <= 0) {
            height = box.at(3);
        }
        // Fitted whole, as preserveAspectRatio does by default.
        const double scale = qMin(width / box.at(2), height / box.at(3));
        context.transform = QTransform::fromTranslate(-box.at(0), -box.at(1)) * QTransform::fromScale(scale, scale);
        reader.viewport = QSizeF(box.at(2), box.at(3));
    } else if (width > 0 && height > 0) {
        reader.viewport = QSizeF(width, height);
    }
    // The root's own x and y mean nothing: walk its children as a group.
    const QHash<QString, QString> own = reader.declared(root);
    Context inner = reader.inherit(context, own);
    inner.transform = context.transform;
    reader.depth = 1;
    for (QDomElement child = root.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        reader.walk(child, inner);
    }
    if (reader.shapes.size() > 1) {
        for (Shape &s : reader.shapes) {
            s.group = QStringLiteral("svg");
        }
    }
    *shapes = reader.shapes;
    if (resources) {
        *resources = reader.resources;
    }
    return true;
}
