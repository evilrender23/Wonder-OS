#pragma once

#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVariant>

class Database;
class QPagedPaintDevice;
class QPainter;

/// A report: the rows of a table or a statement set on pages, grouped and
/// totalled. The same painting serves the preview, the PDF and the printer.
namespace Report
{
struct Line {
    enum Type { Group, Row, Subtotal, Total } type = Row;
    /// A group's heading.
    QString text;
    QStringList cells;
};

struct Document {
    QString error;
    QString title;
    QString subtitle;
    QStringList headers;
    /// Shares of the width of the page, adding up to 1.
    QList<double> widths;
    /// Numbers go to the right.
    QList<bool> right;
    QList<QList<Line>> pages;
    bool landscape = false;
    int rows = 0;
    /// There were more rows than a report takes.
    bool truncated = false;
    QString footer;
};

/// Page sizes in points (1/72 in): A4.
QSizeF pageSize(bool landscape);
/// How many lines fit on the first page and on the others.
int linesPerPage(bool landscape, bool first);

/// Lays a definition out: {title, subtitle, source (a table) or sql, columns:
/// [{name, label, show, total (sum|avg|count|min|max)}], groupBy, landscape}.
Document build(Database &db, const QVariantMap &definition, const QVariantMap &parameters = {});
/// Paints one page in points, on a sheet with its top left corner at the origin.
void paint(QPainter *painter, const Document &document, int page);
/// Every page onto a PDF writer or a printer.
bool render(QPagedPaintDevice *device, const Document &document);
bool exportPdf(const Document &document, const QString &path, QString *error = nullptr);
/// A number as a report writes it.
QString number(double value, bool whole);
}
