#pragma once

#include "database.h"
#include "report.h"

#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

/// One page of a report on screen, and the way out to PDF and to a printer.
class ReportView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Database *database READ database WRITE setDatabase NOTIFY sourceChanged)
    /// See Report::build().
    Q_PROPERTY(QVariantMap definition READ definition WRITE setDefinition NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters WRITE setParameters NOTIFY sourceChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY laidOut)
    Q_PROPERTY(int rows READ rows NOTIFY laidOut)
    Q_PROPERTY(bool truncated READ truncated NOTIFY laidOut)
    Q_PROPERTY(QString error READ error NOTIFY laidOut)
    /// Height over width of the sheet.
    Q_PROPERTY(double ratio READ ratio NOTIFY laidOut)

public:
    explicit ReportView(QQuickItem *parent = nullptr);

    Database *database() const { return m_db; }
    void setDatabase(Database *db);
    QVariantMap definition() const { return m_definition; }
    void setDefinition(const QVariantMap &definition);
    QVariantMap parameters() const { return m_parameters; }
    void setParameters(const QVariantMap &parameters);
    int page() const { return m_page; }
    void setPage(int page);
    int pageCount() const { return int(m_document.pages.size()); }
    int rows() const { return m_document.rows; }
    bool truncated() const { return m_document.truncated; }
    QString error() const { return m_document.error; }
    double ratio() const;

    void paint(QPainter *painter) override;

    Q_INVOKABLE void refresh();
    /// Both return the error, or «».
    Q_INVOKABLE QString exportPdf(const QString &path);
    /// `printer` empty is the default one; `file` set prints to a PDF file instead.
    Q_INVOKABLE QString print(const QString &printer, const QString &file = {});

Q_SIGNALS:
    void sourceChanged();
    void pageChanged();
    void laidOut();

private:
    QPointer<Database> m_db;
    QVariantMap m_definition;
    QVariantMap m_parameters;
    Report::Document m_document;
    int m_page = 0;
};
