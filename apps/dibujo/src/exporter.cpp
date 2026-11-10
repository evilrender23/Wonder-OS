#include "exporter.h"

#include "io.h"
#include "render.h"
#include "svg.h"

#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

QRectF Exporter::area(const Document &doc, const QString &frameId)
{
    if (const Shape *frame = doc.find(frameId)) {
        return frame->bounds();
    }
    const QRectF all = doc.bounds();
    return all.isNull() ? QRectF(0, 0, 100, 100) : all.adjusted(-16, -16, 16, 16);
}

bool Exporter::png(const Document &doc, const QRectF &area, double scale, bool transparent, const QString &path, QString *error)
{
    const QImage picture = area.isEmpty() ? QImage() : Render::picture(doc, area, scale, transparent ? QColor(Qt::transparent) : QColor(Qt::white));
    if (picture.isNull() || !picture.save(path, "PNG")) {
        if (error) {
            *error = QStringLiteral("no se pudo escribir la imagen");
        }
        return false;
    }
    return true;
}

bool Exporter::svg(const Document &doc, const QRectF &area, const QString &path, QString *error)
{
    return Io::writeFile(path, Svg::write(doc, area), error);
}

bool Exporter::pdf(const Document &doc, const QList<QRectF> &areas, const QString &path, QString *error)
{
    if (areas.isEmpty()) {
        return false;
    }
    const auto layout = [](const QRectF &area) {
        // Canvas units are CSS pixels: 96 to the inch, 72 points to the inch.
        return QPageLayout(QPageSize(area.size() * 0.75, QPageSize::Point, QString(), QPageSize::ExactMatch), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0),
                           QPageLayout::Point);
    };
    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("Dibujo"));
    writer.setTitle(doc.title);
    writer.setResolution(96);
    writer.setPageLayout(layout(areas.first()));
    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error) {
            *error = QStringLiteral("no se pudo escribir el PDF");
        }
        return false;
    }
    for (int i = 0; i < areas.size(); ++i) {
        if (i > 0) {
            writer.setPageLayout(layout(areas.at(i)));
            writer.newPage();
        }
        painter.save();
        painter.translate(-areas.at(i).topLeft());
        painter.setClipRect(areas.at(i));
        Render::paint(&painter, doc, areas.at(i), {});
        painter.restore();
    }
    return painter.end();
}
