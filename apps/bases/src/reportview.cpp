#include "reportview.h"

#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>

ReportView::ReportView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    m_document.error = QStringLiteral("El informe no tiene de dónde leer.");
}

void ReportView::setDatabase(Database *db)
{
    if (db == m_db) {
        return;
    }
    if (m_db) {
        disconnect(m_db, nullptr, this, nullptr);
    }
    m_db = db;
    if (m_db) {
        connect(m_db, &Database::dataChanged, this, &ReportView::refresh);
        connect(m_db, &Database::schemaChanged, this, &ReportView::refresh);
        connect(m_db, &Database::opened, this, &ReportView::refresh);
    }
    Q_EMIT sourceChanged();
    refresh();
}

void ReportView::setDefinition(const QVariantMap &definition)
{
    if (definition == m_definition) {
        return;
    }
    m_definition = definition;
    Q_EMIT sourceChanged();
    refresh();
}

void ReportView::setParameters(const QVariantMap &parameters)
{
    if (parameters == m_parameters) {
        return;
    }
    m_parameters = parameters;
    Q_EMIT sourceChanged();
    refresh();
}

void ReportView::setPage(int page)
{
    page = qBound(0, page, qMax(0, pageCount() - 1));
    if (page == m_page) {
        return;
    }
    m_page = page;
    Q_EMIT pageChanged();
    update();
}

double ReportView::ratio() const
{
    const QSizeF size = Report::pageSize(m_document.landscape);
    return size.height() / size.width();
}

void ReportView::refresh()
{
    if (!m_db || !m_db->isOpen() || m_definition.isEmpty()) {
        m_document = {};
        m_document.error = QStringLiteral("El informe no tiene de dónde leer.");
        m_document.landscape = m_definition.value(QStringLiteral("landscape")).toBool();
    } else {
        m_document = Report::build(*m_db, m_definition, m_parameters);
    }
    Q_EMIT laidOut();
    if (m_page >= pageCount() && m_page != 0) {
        m_page = qMax(0, pageCount() - 1);
        Q_EMIT pageChanged();
    }
    update();
}

void ReportView::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), Qt::white);
    if (!m_document.error.isEmpty() || m_document.pages.isEmpty()) {
        return;
    }
    const QSizeF size = Report::pageSize(m_document.landscape);
    const double scale = width() / size.width();
    painter->scale(scale, scale);
    Report::paint(painter, m_document, m_page);
}

QString ReportView::exportPdf(const QString &path)
{
    QString error;
    return Report::exportPdf(m_document, path, &error) ? QString() : error;
}

QString ReportView::print(const QString &printer, const QString &file)
{
    if (!m_document.error.isEmpty()) {
        return m_document.error;
    }
    QPrinter device(QPrinter::HighResolution);
    if (!file.isEmpty()) {
        device.setOutputFormat(QPrinter::PdfFormat);
        device.setOutputFileName(file);
    } else {
        const QPrinterInfo info = printer.isEmpty() ? QPrinterInfo::defaultPrinter() : QPrinterInfo::printerInfo(printer);
        if (info.isNull()) {
            return QStringLiteral("No hay ninguna impresora.");
        }
        device.setPrinterName(info.printerName());
    }
    device.setDocName(m_document.title);
    device.setFullPage(true);
    return Report::render(&device, m_document) ? QString() : QStringLiteral("No se ha podido imprimir.");
}
