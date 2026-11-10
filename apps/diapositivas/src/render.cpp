#include "render.h"

#include "richtext.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCache>
#include <QImageReader>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextDocument>

#include <cmath>

namespace Render {

using namespace Model;

static QImage picture(const Presentation &deck, const QString &key)
{
    // The key is a hash of the bytes: one cache serves every deck.
    static QCache<QString, QImage> cache(64);
    if (QImage *found = cache.object(key)) {
        return *found;
    }
    QByteArray bytes = deck.media.value(key);
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (!image.isNull()) {
        cache.insert(key, new QImage(image));
    }
    return image;
}

QPainterPath outline(const Item &item)
{
    QPainterPath path;
    const double w = item.w;
    const double h = item.h;
    const QString &shape = item.shape;
    if (item.type != QLatin1String("shape") || shape == QLatin1String("rect")) {
        path.addRect(0, 0, w, h);
    } else if (shape == QLatin1String("rounded")) {
        const double radius = qMin(w, h) / 6;
        path.addRoundedRect(0, 0, w, h, radius, radius);
    } else if (shape == QLatin1String("ellipse")) {
        path.addEllipse(0, 0, w, h);
    } else if (shape == QLatin1String("triangle")) {
        path.addPolygon(QPolygonF({QPointF(w / 2, 0), QPointF(w, h), QPointF(0, h)}));
        path.closeSubpath();
    } else if (shape == QLatin1String("diamond")) {
        path.addPolygon(QPolygonF({QPointF(w / 2, 0), QPointF(w, h / 2), QPointF(w / 2, h), QPointF(0, h / 2)}));
        path.closeSubpath();
    } else if (shape == QLatin1String("arrow")) {
        const double head = qMin(w, qMin(w, h) / 2);
        path.addPolygon(QPolygonF({QPointF(0, h / 4), QPointF(w - head, h / 4), QPointF(w - head, 0), QPointF(w, h / 2),
                                   QPointF(w - head, h), QPointF(w - head, h * 3 / 4), QPointF(0, h * 3 / 4)}));
        path.closeSubpath();
    } else if (shape == QLatin1String("star")) {
        // Stretched so that its points touch the four sides of the box, as the star of a .pptx is.
        QPolygonF star;
        const double rx = w / 2 * 1.05146;
        const double ry = h / 2 * 1.10557;
        for (int i = 0; i < 10; ++i) {
            const double angle = -M_PI / 2 + i * M_PI / 5;
            const double radius = i % 2 == 0 ? 1.0 : 0.382;
            star << QPointF(w / 2 + std::cos(angle) * radius * rx, ry + std::sin(angle) * radius * ry);
        }
        path.addPolygon(star);
        path.closeSubpath();
    } else if (shape == QLatin1String("line")) {
        path.moveTo(0, 0);
        path.lineTo(w, h);
    } else {
        path.addRect(0, 0, w, h);
    }
    return path;
}

static void layOut(QTextDocument &document, const Item &item, const Look &look, bool prompt)
{
    const TextDefaults d = Model::textDefaults(item, look);
    QList<Paragraph> paragraphs = item.text;
    if (prompt) {
        Run run;
        run.text = Model::prompt(item);
        Paragraph paragraph = paragraphs.isEmpty() ? Paragraph() : paragraphs.first();
        paragraph.runs = {run};
        paragraphs = {paragraph};
    }
    RichText::fill(document, paragraphs, d, look, qMax(1.0, item.w - 2 * insetX));
}

double textHeight(const Item &item, const Look &look)
{
    QTextDocument document;
    layOut(document, item, look, false);
    return document.size().height() + 2 * insetY;
}

static void paintText(QPainter &p, const Item &item, const Look &look, bool prompt)
{
    QTextDocument document;
    layOut(document, item, look, prompt);
    const double height = document.size().height();
    const double room = item.h - 2 * insetY;
    const double top = item.valign == 1 ? (room - height) / 2 : item.valign == 2 ? room - height : 0;
    p.save();
    p.translate(insetX, insetY + top);
    if (prompt) {
        p.setOpacity(p.opacity() * 0.35);
    }
    QAbstractTextDocumentLayout::PaintContext context;
    document.documentLayout()->draw(&p, context);
    p.restore();
}

static void paintTable(QPainter &p, const Item &item, const Look &look)
{
    if (item.rows <= 0 || item.cols <= 0) {
        return;
    }
    const TextDefaults d = Model::textDefaults(item, look);
    QColor head = look.color(item.fill);
    if (!head.isValid()) {
        head = look.accents.value(0, QColor(0x0a, 0x7a, 0xff));
    }
    const double luma = 0.299 * head.redF() + 0.587 * head.greenF() + 0.114 * head.blueF();
    const QColor headInk = luma < 0.6 ? QColor(Qt::white) : QColor(0x1d, 0x1d, 0x1f);
    QColor band = look.text;
    band.setAlphaF(0.06);
    QColor rule = look.text;
    rule.setAlphaF(0.25);
    const double cw = item.w / item.cols;
    const double ch = item.h / item.rows;
    for (int row = 0; row < item.rows; ++row) {
        const QRectF line(0, row * ch, item.w, ch);
        if (row == 0) {
            p.fillRect(line, head);
        } else if (row % 2 == 0) {
            p.fillRect(line, band);
        }
        for (int col = 0; col < item.cols; ++col) {
            const QString text = item.cell(row, col);
            if (text.isEmpty()) {
                continue;
            }
            QFont font(d.font);
            font.setPixelSize(qMax(1, qRound(d.size)));
            font.setBold(row == 0);
            p.setFont(font);
            p.setPen(row == 0 ? headInk : d.color);
            const QRectF cell(col * cw + insetX, row * ch, cw - 2 * insetX, ch);
            p.save();
            p.setClipRect(QRectF(col * cw, row * ch, cw, ch));
            p.drawText(cell, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap, text);
            p.restore();
        }
    }
    p.setPen(QPen(rule, 1));
    p.setBrush(Qt::NoBrush);
    for (int row = 0; row <= item.rows; ++row) {
        p.drawLine(QPointF(0, row * ch), QPointF(item.w, row * ch));
    }
    for (int col = 0; col <= item.cols; ++col) {
        p.drawLine(QPointF(col * cw, 0), QPointF(col * cw, item.h));
    }
}

static void paintItem(QPainter &p, const Presentation &deck, const Item &item, const Look &look, const Options &options)
{
    p.save();
    p.setOpacity(qBound(0.0, item.opacity, 1.0));
    const bool line = item.type == QLatin1String("shape") && item.shape == QLatin1String("line");
    if (line) {
        // A line runs between its two ends, wherever its box says they are.
        const QLineF l = item.line();
        const QColor color = look.color(item.stroke).isValid() ? look.color(item.stroke) : look.text;
        const double width = qMax(0.5, item.strokeWidth);
        p.translate(item.center());
        p.rotate(item.rotation);
        p.translate(-item.center());
        QLineF shaft = l;
        const double head = qMax(10.0, width * 4);
        if (item.arrow && l.length() > head) {
            shaft.setLength(l.length() - head * 0.8);
        }
        if (item.shadow) {
            p.setPen(QPen(QColor(0, 0, 0, 50), width + 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(l.translated(0, 3));
        }
        p.setPen(QPen(color, width, Qt::SolidLine, item.arrow ? Qt::FlatCap : Qt::RoundCap));
        p.drawLine(shaft);
        if (item.arrow && l.length() > 0) {
            const double angle = std::atan2(l.dy(), l.dx());
            const QPointF tip = l.p2();
            const QPointF back(tip.x() - std::cos(angle) * head, tip.y() - std::sin(angle) * head);
            const QPointF side(-std::sin(angle) * head * 0.4, std::cos(angle) * head * 0.4);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawPolygon(QPolygonF({tip, back + side, back - side}));
        }
        p.restore();
        return;
    }

    p.translate(item.center());
    p.rotate(item.rotation);
    p.save();
    p.scale(item.flipH ? -1 : 1, item.flipV ? -1 : 1);
    p.translate(-item.w / 2, -item.h / 2);
    const QPainterPath path = outline(item);
    const QColor fill = look.color(item.fill);
    const QColor stroke = look.color(item.stroke);
    const bool stroked = stroke.isValid() && item.strokeWidth > 0;
    const bool emptyPicture = item.type == QLatin1String("image") && item.image.isEmpty();
    const bool isTable = item.type == QLatin1String("table");
    if (item.shadow && !emptyPicture && (fill.isValid() || stroked || item.type == QLatin1String("image") || isTable)) {
        // A soft shadow out of a few widening, fading outlines.
        p.save();
        p.translate(0, 4);
        p.setBrush(Qt::NoBrush);
        for (int i = 5; i >= 1; --i) {
            p.setPen(QPen(QColor(0, 0, 0, 9), i * 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }
        p.fillPath(path, QColor(0, 0, 0, 45));
        p.restore();
    }
    if (item.type == QLatin1String("image")) {
        if (!emptyPicture) {
            const QImage image = picture(deck, item.image);
            if (!image.isNull()) {
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                p.drawImage(QRectF(0, 0, item.w, item.h), image);
            } else {
                p.fillRect(QRectF(0, 0, item.w, item.h), QColor(128, 128, 128, 60));
            }
        } else if (options.editing) {
            QColor frame = look.text;
            frame.setAlphaF(0.35);
            QColor wash = look.text;
            wash.setAlphaF(0.05);
            p.setBrush(wash);
            p.setPen(QPen(frame, 1.5, Qt::DashLine));
            p.drawRoundedRect(QRectF(0, 0, item.w, item.h), 8, 8);
        }
    } else if (item.type == QLatin1String("shape") || item.type == QLatin1String("text")) {
        if (fill.isValid()) {
            p.fillPath(path, fill);
        }
    }
    if (stroked && !isTable) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(stroke, item.strokeWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        p.drawPath(path);
    }
    p.restore();

    // Text is never mirrored.
    p.translate(-item.w / 2, -item.h / 2);
    if (isTable) {
        paintTable(p, item, look);
    } else if (emptyPicture) {
        if (options.editing) {
            Item label = item;
            label.type = QStringLiteral("text");
            label.role = QStringLiteral("caption");
            label.valign = 1;
            label.text.clear();
            QTextDocument document;
            Run run;
            run.text = QStringLiteral("Imagen");
            Paragraph paragraph;
            paragraph.runs = {run};
            label.text = {paragraph};
            p.setOpacity(p.opacity() * 0.4);
            paintText(p, label, look, false);
        }
    } else if (item.type != QLatin1String("image") && item.id != options.skipText) {
        if (item.hasText()) {
            paintText(p, item, look, false);
        } else if (options.editing && !item.role.isEmpty()) {
            paintText(p, item, look, true);
        }
    }
    p.restore();
}

void paint(QPainter &p, const Presentation &deck, int index, const Options &options)
{
    if (index < 0 || index >= deck.slides.size()) {
        return;
    }
    const Slide &slide = deck.slides.at(index);
    const Look look = deck.look(options.theme == deck.theme ? QString() : options.theme);
    const QRectF whole(0, 0, deck.width, deck.height);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setClipRect(whole, Qt::IntersectClip);
    if (options.only.isEmpty()) {
        const QColor own = look.color(slide.background);
        if (own.isValid()) {
            p.fillRect(whole, own);
        } else if (look.bg1 == look.bg2) {
            p.fillRect(whole, look.bg1);
        } else {
            QLinearGradient gradient(0, 0, 0, deck.height);
            gradient.setColorAt(0, look.bg1);
            gradient.setColorAt(1, look.bg2);
            p.fillRect(whole, gradient);
        }
    }
    const QStringList builds = slide.builds();
    for (const Item &item : slide.items) {
        if (!options.only.isEmpty() && item.id != options.only) {
            continue;
        }
        if (options.only.isEmpty() && options.step >= 0 && builds.indexOf(item.id) >= options.step) {
            continue;
        }
        paintItem(p, deck, item, look, options);
    }
    if (options.only.isEmpty() && (!deck.master.footer.isEmpty() || deck.master.numbers)) {
        QFont font(look.bodyFont);
        font.setPixelSize(12);
        p.setFont(font);
        QColor ink = look.text;
        ink.setAlphaF(0.6);
        p.setPen(ink);
        const QRectF strip(deck.width * 0.06, deck.height - 34, deck.width * 0.88, 20);
        if (!deck.master.footer.isEmpty()) {
            p.drawText(strip, Qt::AlignLeft | Qt::AlignVCenter, deck.master.footer);
        }
        if (deck.master.numbers) {
            p.drawText(strip, Qt::AlignRight | Qt::AlignVCenter, QString::number(index + 1));
        }
    }
    p.restore();
}

QImage image(const Presentation &deck, int index, double scale, const Options &options)
{
    QImage out(qMax(1, qRound(deck.width * scale)), qMax(1, qRound(deck.height * scale)), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.scale(scale, scale);
    paint(p, deck, index, options);
    p.end();
    return out;
}

bool savePng(const Presentation &deck, int index, const QString &path, double scale)
{
    if (index < 0 || index >= deck.slides.size()) {
        return false;
    }
    return image(deck, index, scale).save(path, "PNG");
}

bool savePdf(const Presentation &deck, const QString &path)
{
    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("Wonderland Diapositivas"));
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(deck.width, deck.height), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&writer)) {
        return false;
    }
    bool first = true;
    for (int i = 0; i < deck.slides.size(); ++i) {
        if (deck.slides.at(i).hidden) {
            continue;
        }
        if (!first) {
            writer.newPage();
        }
        first = false;
        paint(p, deck, i);
    }
    return p.end();
}

}
