#include "render.h"

#include "geometry.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QTextLayout>
#include <QtMath>

#include <cmath>

QFont Render::font(const Style &style)
{
    QFont font;
    if (!style.fontFamily.isEmpty()) {
        font.setFamily(style.fontFamily);
    }
    font.setPixelSize(qMax(1, qRound(style.fontSize)));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    font.setUnderline(style.underline);
    // Hinting snaps glyphs to the pixels of one zoom: without it the words
    // take the same room at every zoom, on paper and on screen.
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QRectF Render::textRect(const Shape &s)
{
    double pad = 10;
    if (s.kind == QLatin1String("text")) {
        pad = 4;
    } else if (s.kind == QLatin1String("note")) {
        pad = 14;
    }
    pad = qMin(pad, qMin(s.w, s.h) / 4);
    return QRectF(pad, pad, qMax(1.0, s.w - 2 * pad), qMax(1.0, s.h - 2 * pad));
}

namespace
{
struct Laid {
    QStringList lines;
    QList<double> widths;
    double lineHeight = 0;
    double ascent = 0;
};

Laid breakLines(const QString &text, const QFont &font, double width)
{
    Laid laid;
    const QFontMetricsF metrics(font);
    laid.lineHeight = metrics.lineSpacing();
    laid.ascent = metrics.ascent();
    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (const QString &paragraph : paragraphs) {
        if (paragraph.isEmpty()) {
            laid.lines << QString();
            laid.widths << 0;
            continue;
        }
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(width > 0 ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(width > 0 ? width : 1e7);
            QString piece = paragraph.mid(line.textStart(), line.textLength());
            while (piece.endsWith(QLatin1Char(' '))) {
                piece.chop(1);
            }
            laid.lines << piece;
            laid.widths << metrics.horizontalAdvance(piece);
        }
        layout.endLayout();
    }
    return laid;
}
}

QSizeF Render::textSize(const Shape &shape, double width)
{
    const Laid laid = breakLines(shape.text, font(shape.style), width);
    double widest = 0;
    for (double w : laid.widths) {
        widest = qMax(widest, w);
    }
    return QSizeF(std::ceil(widest), std::ceil(laid.lines.size() * laid.lineHeight));
}

QList<Render::TextLine> Render::layout(const Shape &s)
{
    QList<TextLine> out;
    if (s.text.isEmpty()) {
        return out;
    }
    const QFont f = font(s.style);
    if (s.isLine()) {
        // The words of a line sit over its middle.
        const Laid laid = breakLines(s.text, f, 0);
        const QPointF mid = Geometry::linePath(s).pointAtPercent(0.5) - QPointF(s.x, s.y);
        // Above a stretch that runs across, beside one that runs up or down.
        const double angle = qDegreesToRadians(Geometry::linePath(s).angleAtPercent(0.5));
        const bool upright = qAbs(std::sin(angle)) > qAbs(std::cos(angle));
        const double top = upright ? mid.y() - laid.lines.size() * laid.lineHeight / 2 : mid.y() - laid.lines.size() * laid.lineHeight - 4;
        for (int i = 0; i < laid.lines.size(); ++i) {
            const double left = upright ? mid.x() + 8 : mid.x() - laid.widths.at(i) / 2;
            out << TextLine{laid.lines.at(i), QPointF(left, top + i * laid.lineHeight + laid.ascent), laid.widths.at(i)};
        }
        return out;
    }
    const QRectF rect = textRect(s);
    const Laid laid = breakLines(s.text, f, s.autoSize ? 0 : rect.width());
    const double total = laid.lines.size() * laid.lineHeight;
    double top = rect.top();
    if (s.style.vAlign == QLatin1String("middle")) {
        top = rect.top() + (rect.height() - total) / 2;
    } else if (s.style.vAlign == QLatin1String("bottom")) {
        top = rect.bottom() - total;
    }
    for (int i = 0; i < laid.lines.size(); ++i) {
        double x = rect.left();
        if (s.style.hAlign == QLatin1String("center")) {
            x = rect.left() + (rect.width() - laid.widths.at(i)) / 2;
        } else if (s.style.hAlign == QLatin1String("right")) {
            x = rect.right() - laid.widths.at(i);
        }
        out << TextLine{laid.lines.at(i), QPointF(x, top + i * laid.lineHeight + laid.ascent), laid.widths.at(i)};
    }
    return out;
}

QBrush Render::fillBrush(const Shape &s, const QRectF &box)
{
    const Style &style = s.style;
    if (style.fillKind == QLatin1String("none")) {
        return Qt::NoBrush;
    }
    if (style.fillKind == QLatin1String("linear")) {
        const double a = qDegreesToRadians(style.gradientAngle);
        const QPointF dir(std::cos(a), std::sin(a));
        // From one side of the box to the other, along the angle.
        const double reach = (qAbs(dir.x()) * box.width() + qAbs(dir.y()) * box.height()) / 2;
        QLinearGradient gradient(box.center() - dir * reach, box.center() + dir * reach);
        gradient.setColorAt(0, style.fill);
        gradient.setColorAt(1, style.fill2);
        return gradient;
    }
    if (style.fillKind == QLatin1String("radial")) {
        QRadialGradient gradient(box.center(), qMax(box.width(), box.height()) / 2);
        gradient.setColorAt(0, style.fill);
        gradient.setColorAt(1, style.fill2);
        return gradient;
    }
    return style.fill;
}

QPen Render::strokePen(const Style &style)
{
    if (style.strokeWidth <= 0 || style.stroke.alpha() == 0) {
        return Qt::NoPen;
    }
    QPen pen(style.stroke, style.strokeWidth);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    if (style.dash == QLatin1String("dash")) {
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({4, 3});
    } else if (style.dash == QLatin1String("dot")) {
        pen.setDashPattern({0.1, 2.4});
    } else if (style.dash == QLatin1String("dashdot")) {
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({4, 2.5, 1, 2.5});
    }
    return pen;
}

QImage Render::image(const Document &doc, const QString &resource)
{
    static QHash<QString, QImage> cache;
    const auto found = doc.resources.constFind(resource);
    if (found == doc.resources.constEnd()) {
        return {};
    }
    const QString key = resource + QLatin1Char(':') + QString::number(found.value().size()) + QLatin1Char(':')
        + QString::number(qHash(found.value()));
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd()) {
        return hit.value();
    }
    if (cache.size() > 48) {
        cache.clear();
    }
    const QImage decoded = QImage::fromData(found.value());
    cache.insert(key, decoded);
    return decoded;
}

static void paintShadow(QPainter *painter, const QPainterPath &path)
{
    painter->save();
    painter->translate(0, 4);
    painter->setBrush(Qt::NoBrush);
    // A soft edge out of a few wide, faint strokes: cheap, and good enough.
    for (int i = 5; i >= 1; --i) {
        QPen pen(QColor(0, 0, 0, 9), i * 3.2);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->drawPath(path);
    }
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 46));
    painter->drawPath(path);
    painter->restore();
}

void Render::paintShape(QPainter *painter, const Shape &s, const Document &doc, const Options &options)
{
    if (s.hidden) {
        return;
    }
    painter->save();
    painter->setOpacity(painter->opacity() * s.style.opacity);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF box(0, 0, s.w, s.h);

    if (s.isLine()) {
        const QPainterPath path = Geometry::linePath(s);
        QPen pen = strokePen(s.style);
        if (pen.style() == Qt::NoPen) {
            pen = QPen(s.style.stroke, 1);
        }
        if (s.style.shadow && options.shadows) {
            painter->save();
            painter->translate(0, 3);
            painter->setPen(QPen(QColor(0, 0, 0, 40), pen.widthF() + 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->drawPath(path);
            painter->restore();
        }
        painter->setBrush(Qt::NoBrush);
        painter->setPen(pen);
        painter->drawPath(path);
        for (bool atEnd : {false, true}) {
            bool filled = false;
            const QPainterPath head = Geometry::arrowHead(s, atEnd, &filled);
            if (head.isEmpty()) {
                continue;
            }
            QPen headPen(s.style.stroke, qMax(1.0, s.style.strokeWidth), Qt::SolidLine, Qt::RoundCap, filled ? Qt::MiterJoin : Qt::RoundJoin);
            if (filled) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(s.style.stroke);
            } else {
                painter->setPen(headPen);
                painter->setBrush(Qt::NoBrush);
            }
            painter->drawPath(head);
        }
    } else {
        painter->setTransform(s.transform(), true);
        const QPainterPath path = Geometry::outline(s);
        if (s.isFrame()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fillBrush(s, box));
            painter->drawRect(box);
            if (options.frameChrome) {
                QPen edge(options.chromeColor, 1);
                edge.setCosmetic(true);
                painter->setPen(edge);
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(box);
                QFont label;
                label.setPixelSize(12);
                painter->save();
                // The name keeps its size on the screen whatever the zoom.
                painter->scale(1 / options.zoom, 1 / options.zoom);
                painter->setFont(label);
                painter->setPen(options.chromeColor);
                const QString name = QFontMetricsF(label).elidedText(s.name, Qt::ElideRight, qMax(40.0, s.w * options.zoom));
                painter->drawText(QPointF(1, -7), name);
                painter->restore();
            }
        } else if (s.kind == QLatin1String("image")) {
            const QImage picture = image(doc, s.image);
            if (picture.isNull()) {
                painter->setPen(QPen(QColor(140, 140, 145), 1, Qt::DashLine));
                painter->setBrush(QColor(140, 140, 145, 40));
                painter->drawRect(box);
            } else {
                painter->drawImage(box, picture);
            }
            if (s.style.strokeWidth > 0 && s.style.stroke.alpha() > 0) {
                painter->setPen(strokePen(s.style));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(box);
            }
        } else if (s.kind != QLatin1String("text") || s.style.fillKind != QLatin1String("none") || s.style.strokeWidth > 0) {
            const bool closedLook = s.style.fillKind != QLatin1String("none");
            if (s.style.shadow && options.shadows && (closedLook || s.kind == QLatin1String("note"))) {
                paintShadow(painter, path);
            }
            QPen pen = strokePen(s.style);
            if (s.kind == QLatin1String("shape")) {
                pen.setJoinStyle(Qt::MiterJoin);
                pen.setMiterLimit(4);
            }
            painter->setPen(pen);
            painter->setBrush(fillBrush(s, box));
            painter->drawPath(path);
        }
    }

    if (!s.text.isEmpty() && s.id != options.editing) {
        if (s.isLine()) {
            painter->translate(s.x, s.y);
        }
        painter->setFont(font(s.style));
        painter->setPen(s.style.textColor);
        if (!s.isLine() && !s.autoSize) {
            painter->setClipRect(box.adjusted(-1, -1, 1, 1), Qt::IntersectClip);
        }
        for (const TextLine &line : layout(s)) {
            painter->drawText(line.origin, line.text);
        }
    }
    painter->restore();
}

void Render::paint(QPainter *painter, const Document &doc, const QRectF &visible, const Options &options)
{
    for (const Shape &s : doc.shapes) {
        if (s.hidden) {
            continue;
        }
        if (!visible.isNull()) {
            const double slack = s.style.strokeWidth * 5 + 40;
            if (!s.bounds().adjusted(-slack, -slack, slack, slack).intersects(visible)) {
                continue;
            }
        }
        paintShape(painter, s, doc, options);
    }
}

QImage Render::picture(const Document &doc, const QRectF &area, double scale, const QColor &background, const Options &options)
{
    const QSize size(qMax(1, qCeil(area.width() * scale)), qMax(1, qCeil(area.height() * scale)));
    // A runaway scale must not ask for gigabytes.
    if (qint64(size.width()) * size.height() > 16000LL * 16000LL) {
        return {};
    }
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(background.isValid() ? background : QColor(Qt::transparent));
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(scale, scale);
    painter.translate(-area.topLeft());
    paint(&painter, doc, area, options);
    painter.end();
    return out;
}

QImage Render::thumbnail(const Document &doc, const QSize &size, const QColor &background, bool pages)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(background);
    QRectF area = doc.bounds();
    QRectF sheet;
    if (pages) {
        for (const Shape *frame : doc.frames()) {
            sheet |= frame->box();
        }
    }
    if (!sheet.isEmpty()) {
        area = sheet;
    }
    if (area.isEmpty()) {
        return out;
    }
    const double margin = sheet.isEmpty() ? qMax(area.width(), area.height()) * 0.06 + 4 : 0;
    area.adjust(-margin, -margin, margin, margin);
    const double scale = qMin(size.width() / area.width(), size.height() / area.height());
    QPainter painter(&out);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.translate((size.width() - area.width() * scale) / 2, (size.height() - area.height() * scale) / 2);
    painter.scale(scale, scale);
    painter.translate(-area.topLeft());
    Options options;
    options.shadows = scale > 0.3;
    if (!sheet.isEmpty()) {
        painter.setClipRect(sheet);
    } else if (pages) {
        // Loose shapes get a sheet of paper too: they are drawn to be seen on one.
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(area, 6 / scale, 6 / scale);
    }
    paint(&painter, doc, QRectF(), options);
    painter.end();
    return out;
}
