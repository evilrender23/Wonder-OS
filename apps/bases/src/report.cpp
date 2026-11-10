#include "report.h"

#include "database.h"
#include "sqlutil.h"

#include <QDate>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace Report
{

static constexpr double margin = 42;
static constexpr double lineHeight = 18;
static constexpr double titleHeight = 62;
static constexpr double headerHeight = 24;
static constexpr double footerHeight = 28;
static constexpr int maxRows = 50000;

QSizeF pageSize(bool landscape)
{
    return landscape ? QSizeF(842, 595) : QSizeF(595, 842);
}

int linesPerPage(bool landscape, bool first)
{
    const double room = pageSize(landscape).height() - 2 * margin - headerHeight - footerHeight - (first ? titleHeight : 0);
    return qMax(1, int(room / lineHeight));
}

QString number(double value, bool whole)
{
    static const QLocale spanish(QLocale::Spanish, QLocale::Spain);
    return whole ? spanish.toString(qlonglong(value)) : spanish.toString(value, 'f', 2);
}

namespace
{
struct Tally {
    double sum = 0;
    double min = 0;
    double max = 0;
    int numbers = 0;
    int values = 0;
    bool whole = true;

    void add(const QVariant &value)
    {
        if (value.isNull()) {
            return;
        }
        ++values;
        bool ok = false;
        const double number = value.toDouble(&ok);
        if (!ok) {
            return;
        }
        whole = whole && value.typeId() != QMetaType::Double;
        min = numbers == 0 ? number : qMin(min, number);
        max = numbers == 0 ? number : qMax(max, number);
        sum += number;
        ++numbers;
    }

    QString text(const QString &total) const
    {
        if (total == QLatin1String("count")) {
            return number(values, true);
        }
        if (numbers == 0) {
            return {};
        }
        if (total == QLatin1String("sum")) {
            return number(sum, whole);
        }
        if (total == QLatin1String("avg")) {
            return number(sum / numbers, false);
        }
        if (total == QLatin1String("min")) {
            return number(min, whole);
        }
        if (total == QLatin1String("max")) {
            return number(max, whole);
        }
        return {};
    }
};

QString cellText(const QVariant &value)
{
    if (value.isNull()) {
        return {};
    }
    if (value.typeId() == QMetaType::Double) {
        return number(value.toDouble(), false);
    }
    if (value.typeId() == QMetaType::QByteArray) {
        return QStringLiteral("(binario)");
    }
    QString text = value.toString();
    // One line per row: a note with line breaks is cut at the first.
    const int breakAt = int(text.indexOf(QLatin1Char('\n')));
    return breakAt >= 0 ? text.left(breakAt) + QStringLiteral("…") : text;
}
}

Document build(Database &db, const QVariantMap &definition, const QVariantMap &parameters)
{
    Document document;
    document.title = definition.value(QStringLiteral("title")).toString();
    document.subtitle = definition.value(QStringLiteral("subtitle")).toString();
    document.landscape = definition.value(QStringLiteral("landscape")).toBool();
    document.footer = QLocale(QLocale::Spanish, QLocale::Spain).toString(QDate::currentDate(), QStringLiteral("d 'de' MMMM 'de' yyyy"));

    const QString table = definition.value(QStringLiteral("source")).toString();
    QString select = definition.value(QStringLiteral("sql")).toString();
    if (!table.isEmpty()) {
        if (!db.exists(table)) {
            document.error = QStringLiteral("No existe la tabla «%1».").arg(table);
            return document;
        }
        select = QStringLiteral("SELECT * FROM ") + Sql::ident(table);
    } else if (!Sql::selects(select) || Sql::split(select).size() != 1) {
        document.error = QStringLiteral("Un informe se hace con una tabla o una consulta SELECT.");
        return document;
    }
    const QString groupBy = definition.value(QStringLiteral("groupBy")).toString();
    QString statement = QStringLiteral("SELECT * FROM (%1)").arg(Sql::trimmed(select));
    if (!groupBy.isEmpty()) {
        statement += QStringLiteral(" ORDER BY ") + Sql::ident(groupBy);
    }
    const Result result = db.queryNamed(statement, parameters, maxRows);
    if (!result.ok) {
        document.error = result.error;
        return document;
    }
    document.truncated = result.truncated;
    document.rows = int(result.rows.size());

    // Which columns, in the order of the definition; none listed means all.
    QList<int> at;
    QStringList totals;
    const QVariantList listed = definition.value(QStringLiteral("columns")).toList();
    for (const QVariant &item : listed) {
        const QVariantMap column = item.toMap();
        const int index = int(result.columns.indexOf(column.value(QStringLiteral("name")).toString()));
        if (index < 0 || !column.value(QStringLiteral("show"), true).toBool()) {
            continue;
        }
        at.append(index);
        const QString label = column.value(QStringLiteral("label")).toString();
        document.headers.append(label.isEmpty() ? Sql::pretty(result.columns.at(index)) : label);
        totals.append(column.value(QStringLiteral("total")).toString().toLower());
    }
    if (at.isEmpty() && listed.isEmpty()) {
        for (int i = 0; i < result.columns.size(); ++i) {
            at.append(i);
            document.headers.append(Sql::pretty(result.columns.at(i)));
            totals.append(QString());
        }
    }
    if (at.isEmpty()) {
        document.error = QStringLiteral("El informe no tiene columnas.");
        return document;
    }
    const int group = groupBy.isEmpty() ? -1 : int(result.columns.indexOf(groupBy));
    bool anyTotal = false;
    for (const QString &total : std::as_const(totals)) {
        anyTotal = anyTotal || !total.isEmpty();
    }

    QList<Line> lines;
    QList<Tally> whole(at.size());
    QList<Tally> part(at.size());
    QList<double> longest(at.size(), 0);
    QList<int> numeric(at.size(), 0);
    QList<int> filled(at.size(), 0);
    const auto totalLine = [&](Line::Type type, const QList<Tally> &tallies, const QString &label) {
        Line line;
        line.type = type;
        bool labelled = false;
        for (int i = 0; i < at.size(); ++i) {
            QString text = totals.at(i).isEmpty() ? QString() : tallies.at(i).text(totals.at(i));
            // The label sits in the first column that has no total of its own.
            if (!labelled && totals.at(i).isEmpty()) {
                text = label;
                labelled = true;
            }
            line.cells.append(text);
        }
        lines.append(line);
    };
    QVariant current;
    bool open = false;
    int inGroup = 0;
    for (const QVariantList &row : result.rows) {
        if (group >= 0 && (!open || row.at(group) != current || row.at(group).isNull() != current.isNull())) {
            if (open && anyTotal) {
                totalLine(Line::Subtotal, part, QStringLiteral("Subtotal (%1)").arg(inGroup));
            }
            current = row.at(group);
            open = true;
            inGroup = 0;
            part = QList<Tally>(at.size());
            Line heading;
            heading.type = Line::Group;
            const QString value = cellText(current);
            heading.text = value.isEmpty() ? QStringLiteral("(sin valor)") : value;
            lines.append(heading);
        }
        Line line;
        for (int i = 0; i < at.size(); ++i) {
            const QVariant &value = row.at(at.at(i));
            const QString text = cellText(value);
            line.cells.append(text);
            whole[i].add(value);
            part[i].add(value);
            longest[i] = qMax(longest.at(i), double(qMin<qsizetype>(text.size(), 40)));
            if (!value.isNull()) {
                ++filled[i];
                if (value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Int) {
                    ++numeric[i];
                }
            }
        }
        lines.append(line);
        ++inGroup;
    }
    if (open && anyTotal) {
        totalLine(Line::Subtotal, part, QStringLiteral("Subtotal (%1)").arg(inGroup));
    }
    if (anyTotal && !result.rows.isEmpty()) {
        totalLine(Line::Total, whole, QStringLiteral("Total (%1)").arg(result.rows.size()));
    }

    // Widths follow what the columns hold, within reason.
    double sum = 0;
    for (int i = 0; i < at.size(); ++i) {
        longest[i] = qMax(longest.at(i), double(qMin<qsizetype>(document.headers.at(i).size(), 24)));
        longest[i] = qBound(5.0, longest.at(i) + 2, 42.0);
        sum += longest.at(i);
        document.right.append(filled.at(i) > 0 && numeric.at(i) == filled.at(i));
    }
    for (int i = 0; i < at.size(); ++i) {
        document.widths.append(longest.at(i) / sum);
    }

    // Pages: a group's heading is never left alone at the foot of one.
    QList<Line> page;
    const auto capacity = [&] {
        return linesPerPage(document.landscape, document.pages.isEmpty());
    };
    for (int i = 0; i < lines.size(); ++i) {
        const bool orphan = lines.at(i).type == Line::Group && page.size() == capacity() - 1;
        if (page.size() >= capacity() || orphan) {
            document.pages.append(page);
            page.clear();
        }
        page.append(lines.at(i));
    }
    if (!page.isEmpty() || document.pages.isEmpty()) {
        document.pages.append(page);
    }
    return document;
}

void paint(QPainter *painter, const Document &document, int page)
{
    if (page < 0 || page >= document.pages.size()) {
        return;
    }
    const QSizeF size = pageSize(document.landscape);
    const double left = margin;
    const double width = size.width() - 2 * margin;
    const QColor ink(0x1d, 0x1d, 0x1f);
    const QColor dim(0x6e, 0x6e, 0x73);
    const QColor rule(0xc8, 0xc8, 0xcc);
    const QColor band(0xf2, 0xf2, 0xf4);
    const QColor tint(0xc2, 0x33, 0x4d);

    QFont base = painter->font();
    base.setPixelSize(10);
    QFont bold = base;
    bold.setWeight(QFont::DemiBold);
    double y = margin;
    painter->setRenderHint(QPainter::TextAntialiasing);

    if (page == 0) {
        QFont title = base;
        title.setPixelSize(22);
        title.setWeight(QFont::Bold);
        painter->setFont(title);
        painter->setPen(ink);
        painter->drawText(QRectF(left, y, width, 30), Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(title).elidedText(document.title, Qt::ElideRight, width));
        painter->setFont(base);
        painter->setPen(dim);
        QString sub = document.subtitle;
        if (sub.isEmpty()) {
            sub = document.rows == 1 ? QStringLiteral("1 registro") : QStringLiteral("%1 registros").arg(number(document.rows, true));
        }
        painter->drawText(QRectF(left, y + 32, width, 14), Qt::AlignLeft | Qt::AlignVCenter, sub);
        painter->fillRect(QRectF(left, y + 52, 36, 2.5), tint);
        y += titleHeight;
    }

    QList<double> x;
    QList<double> w;
    double at = left;
    for (const double share : document.widths) {
        x.append(at);
        w.append(share * width);
        at += share * width;
    }
    const auto cells = [&](const QStringList &texts, double top, double height, const QFont &font, const QColor &color) {
        painter->setFont(font);
        painter->setPen(color);
        const QFontMetricsF metrics(font);
        for (int i = 0; i < texts.size() && i < x.size(); ++i) {
            const QRectF box(x.at(i) + 4, top, w.at(i) - 8, height);
            painter->drawText(box, (document.right.value(i) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, metrics.elidedText(texts.at(i), Qt::ElideRight, box.width()));
        }
    };

    painter->fillRect(QRectF(left, y, width, headerHeight - 4), band);
    cells(document.headers, y, headerHeight - 4, bold, ink);
    y += headerHeight;

    for (const Line &line : document.pages.at(page)) {
        switch (line.type) {
        case Line::Group:
            painter->setFont(bold);
            painter->setPen(tint);
            painter->drawText(QRectF(left + 4, y, width - 8, lineHeight), Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(bold).elidedText(line.text, Qt::ElideRight, width - 8));
            painter->setPen(QPen(rule, 0.6));
            painter->drawLine(QPointF(left, y + lineHeight - 0.5), QPointF(left + width, y + lineHeight - 0.5));
            break;
        case Line::Row:
            cells(line.cells, y, lineHeight, base, ink);
            painter->setPen(QPen(QColor(0xe6, 0xe6, 0xe9), 0.5));
            painter->drawLine(QPointF(left, y + lineHeight - 0.25), QPointF(left + width, y + lineHeight - 0.25));
            break;
        case Line::Subtotal:
            cells(line.cells, y, lineHeight, bold, dim);
            break;
        case Line::Total:
            painter->setPen(QPen(ink, 1));
            painter->drawLine(QPointF(left, y + 0.5), QPointF(left + width, y + 0.5));
            cells(line.cells, y + 1, lineHeight, bold, ink);
            break;
        }
        y += lineHeight;
    }

    painter->setFont(base);
    painter->setPen(dim);
    const QRectF foot(left, size.height() - margin - 12, width, 12);
    painter->drawText(foot, Qt::AlignLeft | Qt::AlignVCenter, document.footer);
    painter->drawText(foot, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Página %1 de %2").arg(page + 1).arg(document.pages.size()));
}

bool render(QPagedPaintDevice *device, const Document &document)
{
    device->setPageLayout(QPageLayout(QPageSize(QPageSize::A4), document.landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
    QPainter painter;
    if (!painter.begin(device)) {
        return false;
    }
    // Points on the sheet, whatever the resolution of the device.
    const double scale = device->logicalDpiX() / 72.0;
    for (int page = 0; page < document.pages.size(); ++page) {
        if (page > 0 && !device->newPage()) {
            return false;
        }
        painter.save();
        painter.scale(scale, scale);
        paint(&painter, document, page);
        painter.restore();
    }
    return painter.end();
}

bool exportPdf(const Document &document, const QString &path, QString *error)
{
    if (!document.error.isEmpty()) {
        if (error) {
            *error = document.error;
        }
        return false;
    }
    QPdfWriter writer(path);
    writer.setTitle(document.title);
    writer.setCreator(QStringLiteral("Bases"));
    writer.setResolution(300);
    if (!render(&writer, document) || !QFileInfo(path).isFile()) {
        if (error) {
            *error = QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    return true;
}

}
