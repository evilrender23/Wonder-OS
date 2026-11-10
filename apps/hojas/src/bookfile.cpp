#include "book.h"

#include "chartpaint.h"
#include "csv.h"
#include "numfmt.h"
#include "sheetpaint.h"
#include "store.h"
#include "xlsx.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonObject>
#include <QLocale>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QUuid>

// What a document is, what it reads and writes, its versions and its recovery copies.

namespace
{
QString suffixOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

QString stamp(const QString &baseName)
{
    const QDateTime when = QDateTime::fromString(baseName.left(15), QStringLiteral("yyyyMMdd-HHmmss"));
    return when.isValid() ? QLocale(QLocale::Spanish, QLocale::Spain).toString(when, QStringLiteral("d 'de' MMMM, HH:mm:ss")) : baseName;
}

struct Writer {
    Workbook &book;
    int sheet = 0;

    void put(int row, int col, const QString &typed) { book.setInput(sheet, row, col, typed); }
    void row(int row, int col, const QStringList &cells)
    {
        for (int i = 0; i < cells.size(); ++i) {
            if (!cells.at(i).isEmpty()) {
                put(row, col + i, cells.at(i));
            }
        }
    }
    void style(const Area &area, const std::function<void(Style &)> &change) { book.changeStyle(sheet, area, change); }
    void format(const Area &area, const QString &family, int decimals = 2)
    {
        const QString code = NumFmt::familyCode(family, decimals);
        style(area, [&code](Style &s) { s.format = code; });
    }
    void heading(const Area &area, const QString &fill, const QString &color = QStringLiteral("#ffffff"))
    {
        style(area, [&](Style &s) {
            s.bold = true;
            s.fill = fill;
            s.color = color;
        });
    }
    void title(int row, int col, const QString &text, int size = 18)
    {
        put(row, col, text);
        style(Area{row, col, row, col}, [size](Style &s) {
            s.bold = true;
            s.size = size;
        });
        book.setRowHeight(sheet, row, row, size * 2.0);
    }
};
}

void Book::build(Workbook &book, const QString &kind)
{
    book.clear();
    Writer w{book, 0};
    const QString green = QStringLiteral("#1f9d55");
    const QString wash = QStringLiteral("#e8f6ee");
    if (kind == QLatin1String("budget")) {
        book.renameSheet(0, QStringLiteral("Presupuesto"));
        w.title(0, 0, QStringLiteral("Presupuesto personal"));
        w.row(2, 0, {QStringLiteral("Concepto"), QStringLiteral("Categoría"), QStringLiteral("Previsto"), QStringLiteral("Real"), QStringLiteral("Diferencia")});
        w.heading(Area{2, 0, 2, 4}, green);
        const QList<QStringList> lines = {
            {QStringLiteral("Nómina"), QStringLiteral("Ingresos"), QStringLiteral("2100"), QStringLiteral("2100")},
            {QStringLiteral("Alquiler"), QStringLiteral("Vivienda"), QStringLiteral("-750"), QStringLiteral("-750")},
            {QStringLiteral("Luz y agua"), QStringLiteral("Vivienda"), QStringLiteral("-95"), QStringLiteral("-108,4")},
            {QStringLiteral("Internet y móvil"), QStringLiteral("Vivienda"), QStringLiteral("-45"), QStringLiteral("-45")},
            {QStringLiteral("Compra"), QStringLiteral("Comida"), QStringLiteral("-320"), QStringLiteral("-298,75")},
            {QStringLiteral("Transporte"), QStringLiteral("Movilidad"), QStringLiteral("-60"), QStringLiteral("-52")},
            {QStringLiteral("Ocio"), QStringLiteral("Personal"), QStringLiteral("-120"), QStringLiteral("-146,2")},
            {QStringLiteral("Ahorro"), QStringLiteral("Ahorro"), QStringLiteral("-300"), QStringLiteral("-300")},
        };
        for (int i = 0; i < lines.size(); ++i) {
            w.row(3 + i, 0, lines.at(i));
            w.put(3 + i, 4, QStringLiteral("=D%1-C%1").arg(4 + i));
        }
        const int total = 3 + lines.size();
        w.row(total, 0, {QStringLiteral("Saldo"), QString(), QStringLiteral("=SUMA(C4:C%1)").arg(total), QStringLiteral("=SUMA(D4:D%1)").arg(total),
                         QStringLiteral("=SUMA(E4:E%1)").arg(total)});
        w.style(Area{total, 0, total, 4}, [&wash](Style &s) {
            s.bold = true;
            s.fill = wash;
        });
        w.format(Area{3, 2, total, 4}, QStringLiteral("currency"));
        book.setColWidth(0, 0, 0, 150);
        book.setColWidth(0, 1, 1, 110);
        book.setColWidth(0, 2, 4, 104);
        CondFormat rule;
        rule.area = Area{3, 4, total - 1, 4};
        rule.op = QStringLiteral("<");
        rule.a = QStringLiteral("0");
        rule.color = QStringLiteral("#d70015");
        book.addConditional(0, rule);
        book.setFreeze(0, 3, 0);
    } else if (kind == QLatin1String("invoice")) {
        book.renameSheet(0, QStringLiteral("Factura"));
        w.title(0, 0, QStringLiteral("Factura"), 20);
        w.row(1, 0, {QStringLiteral("Número"), QStringLiteral("2026-001")});
        w.row(2, 0, {QStringLiteral("Fecha"), QStringLiteral("=HOY()")});
        w.format(Area{2, 1, 2, 1}, QStringLiteral("date"));
        w.style(Area{1, 1, 2, 1}, [](Style &s) { s.align = 1; });
        w.row(4, 0, {QStringLiteral("Emisor"), QString(), QString(), QStringLiteral("Cliente")});
        w.style(Area{4, 0, 4, 4}, [](Style &s) { s.bold = true; });
        w.row(5, 0, {QStringLiteral("Tu nombre o empresa"), QString(), QString(), QStringLiteral("Nombre del cliente")});
        w.row(6, 0, {QStringLiteral("NIF 00000000X"), QString(), QString(), QStringLiteral("NIF del cliente")});
        w.row(8, 0, {QStringLiteral("Descripción"), QStringLiteral("Cantidad"), QStringLiteral("Precio"), QStringLiteral("IVA"), QStringLiteral("Importe")});
        w.heading(Area{8, 0, 8, 4}, green);
        const QList<QStringList> lines = {
            {QStringLiteral("Diseño de la web"), QStringLiteral("1"), QStringLiteral("850"), QStringLiteral("21%")},
            {QStringLiteral("Horas de desarrollo"), QStringLiteral("12"), QStringLiteral("45"), QStringLiteral("21%")},
            {QStringLiteral("Alojamiento anual"), QStringLiteral("1"), QStringLiteral("96"), QStringLiteral("21%")},
        };
        for (int i = 0; i < 6; ++i) {
            if (i < lines.size()) {
                w.row(9 + i, 0, lines.at(i));
            }
            w.put(9 + i, 4, QStringLiteral("=SI(B%1=\"\";\"\";B%1*C%1)").arg(10 + i));
        }
        w.format(Area{9, 2, 14, 2}, QStringLiteral("currency"));
        w.format(Area{9, 3, 14, 3}, QStringLiteral("percent"), 0);
        w.row(16, 3, {QStringLiteral("Base imponible"), QStringLiteral("=SUMA(E10:E15)")});
        w.row(17, 3, {QStringLiteral("IVA"), QStringLiteral("=SUMAPRODUCTO(B10:B15;C10:C15;D10:D15)")});
        w.row(18, 3, {QStringLiteral("Total"), QStringLiteral("=E17+E18")});
        w.format(Area{9, 4, 18, 4}, QStringLiteral("currency"));
        w.style(Area{18, 3, 18, 4}, [&wash](Style &s) {
            s.bold = true;
            s.fill = wash;
        });
        book.setBorders(0, Area{8, 0, 14, 4}, QStringLiteral("all"), QStringLiteral("#d0d0d5"));
        book.setColWidth(0, 0, 0, 220);
        book.setColWidth(0, 3, 3, 120);
        book.setColWidth(0, 4, 4, 110);
    } else if (kind == QLatin1String("tasks")) {
        book.renameSheet(0, QStringLiteral("Tareas"));
        w.title(0, 0, QStringLiteral("Lista de tareas"));
        w.row(2, 0, {QStringLiteral("Hecho"), QStringLiteral("Tarea"), QStringLiteral("Responsable"), QStringLiteral("Vence"), QStringLiteral("Prioridad")});
        const QDate today = QDate::currentDate();
        const QList<QStringList> lines = {
            {QStringLiteral("VERDADERO"), QStringLiteral("Definir el alcance"), QStringLiteral("Ana"), QString::number(2), QStringLiteral("Alta")},
            {QStringLiteral("VERDADERO"), QStringLiteral("Pedir presupuestos"), QStringLiteral("Luis"), QString::number(5), QStringLiteral("Media")},
            {QStringLiteral("FALSO"), QStringLiteral("Revisar el contrato"), QStringLiteral("Ana"), QString::number(9), QStringLiteral("Alta")},
            {QStringLiteral("FALSO"), QStringLiteral("Preparar la presentación"), QStringLiteral("Marta"), QString::number(14), QStringLiteral("Media")},
            {QStringLiteral("FALSO"), QStringLiteral("Enviar el resumen"), QStringLiteral("Luis"), QString::number(16), QStringLiteral("Baja")},
        };
        for (int i = 0; i < lines.size(); ++i) {
            QStringList cells = lines.at(i);
            cells[3] = today.addDays(cells.at(3).toInt()).toString(QStringLiteral("dd/MM/yyyy"));
            w.row(3 + i, 0, cells);
        }
        w.style(Area{2, 0, 7, 0}, [](Style &s) { s.align = 2; });
        w.format(Area{3, 3, 7, 3}, QStringLiteral("date"));
        book.addTable(0, Area{2, 0, 7, 4});
        w.row(9, 1, {QStringLiteral("Completadas"), QStringLiteral("=CONTAR.SI(A4:A8;VERDADERO)&\" de \"&CONTARA(B4:B8)")});
        w.style(Area{9, 1, 9, 1}, [](Style &s) { s.bold = true; });
        CondFormat rule;
        rule.area = Area{3, 4, 7, 4};
        rule.op = QStringLiteral("contains");
        rule.a = QStringLiteral("Alta");
        rule.fill = QStringLiteral("#ffe3e0");
        rule.color = QStringLiteral("#b3261e");
        book.addConditional(0, rule);
        book.setColWidth(0, 0, 0, 64);
        book.setColWidth(0, 1, 1, 220);
        book.setColWidth(0, 2, 4, 110);
    } else if (kind == QLatin1String("schedule")) {
        book.renameSheet(0, QStringLiteral("Horario"));
        w.title(0, 0, QStringLiteral("Horario semanal"));
        w.row(2, 0, {QStringLiteral("Hora"), QStringLiteral("Lunes"), QStringLiteral("Martes"), QStringLiteral("Miércoles"), QStringLiteral("Jueves"), QStringLiteral("Viernes")});
        w.heading(Area{2, 0, 2, 5}, green);
        w.style(Area{2, 0, 2, 5}, [](Style &s) { s.align = 2; });
        for (int i = 0; i < 8; ++i) {
            w.put(3 + i, 0, QStringLiteral("%1:00").arg(8 + i, 2, 10, QLatin1Char('0')));
        }
        // Hours and minutes: a timetable has no use for the seconds.
        w.style(Area{3, 0, 10, 0}, [](Style &s) { s.format = QStringLiteral("hh:mm"); });
        w.style(Area{3, 0, 10, 0}, [&wash](Style &s) {
            s.align = 2;
            s.fill = wash;
            s.bold = true;
        });
        w.put(3, 1, QStringLiteral("Matemáticas"));
        w.put(3, 3, QStringLiteral("Matemáticas"));
        w.put(4, 2, QStringLiteral("Lengua"));
        w.put(4, 4, QStringLiteral("Lengua"));
        w.put(5, 1, QStringLiteral("Inglés"));
        w.put(5, 5, QStringLiteral("Inglés"));
        w.put(6, 0 + 1, QStringLiteral("Recreo"));
        book.merge(0, Area{6, 1, 6, 5});
        w.style(Area{6, 1, 6, 5}, [](Style &s) {
            s.align = 2;
            s.italic = true;
            s.fill = QStringLiteral("#f2f2f7");
        });
        w.put(7, 2, QStringLiteral("Física"));
        w.put(7, 4, QStringLiteral("Física"));
        w.put(8, 3, QStringLiteral("Historia"));
        w.put(9, 5, QStringLiteral("Deporte"));
        w.style(Area{3, 1, 10, 5}, [](Style &s) {
            if (s.align == 0) {
                s.align = 2;
            }
        });
        book.setBorders(0, Area{2, 0, 10, 5}, QStringLiteral("all"), QStringLiteral("#d0d0d5"));
        book.setColWidth(0, 0, 0, 70);
        book.setColWidth(0, 1, 5, 116);
        book.setRowHeight(0, 3, 10, 34);
    }
}

void Book::opened(const QString &path, const QString &title)
{
    m_path = path;
    m_title = title;
    m_open = true;
    m_sheet = 0;
    m_rows = 120;
    m_cols = 30;
    m_clip = Clip();
    m_session = QUuid::createUuid().toString(QUuid::Id128).left(12);
    m_book.setModified(false);
    Q_EMIT documentChanged();
    touch();
}

void Book::create(const QString &kind)
{
    build(m_book, kind);
    // A template starts with nothing to undo.
    m_book.forgetHistory();
    static const QHash<QString, QString> names = {{QStringLiteral("budget"), QStringLiteral("Presupuesto")}, {QStringLiteral("invoice"), QStringLiteral("Factura")},
                                                  {QStringLiteral("tasks"), QStringLiteral("Tareas")}, {QStringLiteral("schedule"), QStringLiteral("Horario")}};
    opened(QString(), names.value(kind, QStringLiteral("Sin título")));
}

QVariantList Book::templatePreview(const QString &kind)
{
    Workbook book;
    build(book, kind);
    QVariantList rows;
    for (int r = 0; r < 11; ++r) {
        QVariantList cells;
        for (int c = 0; c < 5; ++c) {
            const Style style = book.effectiveStyle(0, r, c);
            const Value value = book.value(0, r, c);
            QString fill = style.fill;
            const int table = book.tableAt(0, r, c);
            if (table >= 0 && fill.isEmpty()) {
                fill = r == book.sheet(0).tables.at(table).area.r1 ? QStringLiteral("#1f9d55") : QString();
            }
            cells.append(QVariantMap{{QStringLiteral("text"), value.isBool() ? (value.truth() ? QStringLiteral("☑") : QStringLiteral("☐")) : book.display(0, r, c)},
                                     {QStringLiteral("fill"), fill}, {QStringLiteral("bold"), style.bold || (table >= 0 && !fill.isEmpty())},
                                     {QStringLiteral("color"), table >= 0 && !fill.isEmpty() && style.color.isEmpty() ? QStringLiteral("#ffffff") : style.color},
                                     {QStringLiteral("big"), style.size >= 16}, {QStringLiteral("number"), value.isNumber()}});
        }
        rows.append(QVariant(cells));
    }
    return rows;
}

QString Book::read(Workbook &into, const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("No se puede leer «%1».").arg(QFileInfo(path).fileName());
    }
    const QByteArray bytes = file.readAll();
    const QString suffix = suffixOf(path);
    QString error;
    const bool zip = bytes.startsWith("PK");
    if (suffix == QLatin1String("ods") || suffix == QLatin1String("ots")) {
        if (!Ods::load(into, bytes, &error)) {
            return error.isEmpty() ? QStringLiteral("No es una hoja de cálculo OpenDocument válida.") : error;
        }
    } else if (zip) {
        // An Excel workbook, or an OpenDocument one under another name.
        if (!Xlsx::load(into, bytes, &error)) {
            QString other;
            if (!Ods::load(into, bytes, &other)) {
                return error.isEmpty() ? QStringLiteral("No es un libro de Excel válido.") : error;
            }
        }
    } else if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("xlsm")) {
        return QStringLiteral("«%1» no es un libro de Excel válido.").arg(QFileInfo(path).fileName());
    } else {
        into.clear();
        Csv::load(into, 0, bytes, suffix == QLatin1String("tsv") || suffix == QLatin1String("tab") ? QChar(QLatin1Char('\t')) : QChar());
        into.renameSheet(0, QFileInfo(path).completeBaseName().left(31));
        into.finishLoading();
    }
    return {};
}

QString Book::load(const QString &path)
{
    Workbook fresh;
    const QString error = read(fresh, path);
    if (!error.isEmpty()) {
        return error;
    }
    m_book = fresh;
    m_book.recalcAll();
    opened(QFileInfo(path).absoluteFilePath(), QFileInfo(path).completeBaseName());
    return {};
}

QString Book::format() const
{
    const QString suffix = suffixOf(m_path);
    return suffix.isEmpty() ? QStringLiteral("xlsx") : suffix;
}

QString Book::write(const QString &path)
{
    const QString suffix = suffixOf(path);
    QByteArray bytes;
    if (suffix == QLatin1String("xlsx")) {
        bytes = Xlsx::save(m_book);
    } else if (suffix == QLatin1String("csv")) {
        bytes = Csv::save(m_book, m_sheet, QLatin1Char(';'));
    } else if (suffix == QLatin1String("tsv") || suffix == QLatin1String("tab") || suffix == QLatin1String("txt")) {
        bytes = Csv::save(m_book, m_sheet, QLatin1Char('\t'));
    } else if (suffix == QLatin1String("pdf")) {
        return exportPdf(path);
    } else {
        return QStringLiteral("Hojas no escribe archivos «.%1». Guarda como .xlsx, .csv o .tsv.").arg(suffix);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
    }
    return {};
}

QString Book::save()
{
    if (!m_open) {
        return {};
    }
    if (m_path.isEmpty()) {
        return QStringLiteral("El documento aún no tiene archivo.");
    }
    return saveAs(m_path);
}

QString Book::saveAs(const QString &wanted)
{
    if (!m_open) {
        return {};
    }
    QString path = wanted;
    if (suffixOf(path).isEmpty()) {
        path += QStringLiteral(".xlsx");
    }
    const QString suffix = suffixOf(path);
    if (suffix == QLatin1String("pdf") || suffix == QLatin1String("ods")) {
        // Formats the document cannot be read back from whole stay exports.
        if (suffix == QLatin1String("ods")) {
            return QStringLiteral("Hojas lee .ods pero aún no lo escribe. Guarda como .xlsx.");
        }
        return exportTo(path);
    }
    const QString error = write(path);
    if (!error.isEmpty()) {
        return error;
    }
    m_path = QFileInfo(path).absoluteFilePath();
    m_title = QFileInfo(path).completeBaseName();
    m_book.setModified(false);
    keepVersion(m_path);
    QFile::remove(m_dataDir + QStringLiteral("/recovery/") + m_session + QStringLiteral(".xlsx"));
    QFile::remove(m_dataDir + QStringLiteral("/recovery/") + m_session + QStringLiteral(".json"));
    Q_EMIT documentChanged();
    Q_EMIT changed();
    return {};
}

QString Book::exportTo(const QString &path)
{
    return m_open ? write(path) : QString();
}

void Book::close()
{
    if (!m_open) {
        return;
    }
    QFile::remove(m_dataDir + QStringLiteral("/recovery/") + m_session + QStringLiteral(".xlsx"));
    QFile::remove(m_dataDir + QStringLiteral("/recovery/") + m_session + QStringLiteral(".json"));
    m_book.clear();
    m_open = false;
    m_path.clear();
    m_title.clear();
    m_clip = Clip();
    Q_EMIT documentChanged();
    touch();
}

QString Book::rename(const QString &wanted)
{
    QString title = wanted.trimmed();
    title.remove(QLatin1Char('/'));
    if (!m_open || title.isEmpty() || title == m_title) {
        return {};
    }
    if (!m_path.isEmpty()) {
        const QFileInfo info(m_path);
        const QString target = info.absolutePath() + QLatin1Char('/') + title + QLatin1Char('.') + info.suffix();
        if (QFileInfo::exists(target)) {
            return QStringLiteral("Ya hay un archivo «%1» en esa carpeta.").arg(QFileInfo(target).fileName());
        }
        if (QFileInfo::exists(m_path) && !QFile::rename(m_path, target)) {
            return QStringLiteral("No se puede renombrar el archivo.");
        }
        // The versions follow the document to its new name.
        const QString before = versionsDir();
        m_path = target;
        if (QDir(before).exists()) {
            QDir().rename(before, versionsDir());
        }
    }
    m_title = title;
    Q_EMIT documentChanged();
    return {};
}

// --- Versions -----------------------------------------------------------------------

QString Book::versionsDir() const
{
    const QByteArray id = QCryptographicHash::hash(m_path.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return m_dataDir + QStringLiteral("/versions/") + QString::fromLatin1(id);
}

void Book::keepVersion(const QString &path)
{
    const QString suffix = suffixOf(path);
    const QString dir = versionsDir();
    if (!QDir().mkpath(dir)) {
        return;
    }
    // The moment and a count within it: names that sort in the order they were kept.
    const QString moment = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString name;
    for (int n = 1; name.isEmpty() || !QDir(dir).entryList({name + QStringLiteral(".*")}, QDir::Files).isEmpty(); ++n) {
        name = moment + QLatin1Char('-') + QString::number(n).rightJustified(3, QLatin1Char('0'));
    }
    QFile::copy(path, dir + QLatin1Char('/') + name + QLatin1Char('.') + suffix);
    QStringList kept = QDir(dir).entryList(QDir::Files, QDir::Name);
    while (kept.size() > 20) {
        QFile::remove(dir + QLatin1Char('/') + kept.takeFirst());
    }
}

QVariantList Book::versions() const
{
    QVariantList out;
    if (!m_open || m_path.isEmpty()) {
        return out;
    }
    const QDir dir(versionsDir());
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name | QDir::Reversed);
    for (const QFileInfo &info : files) {
        out.append(QVariantMap{{QStringLiteral("file"), info.absoluteFilePath()}, {QStringLiteral("when"), stamp(info.completeBaseName())},
                               {QStringLiteral("size"), QLocale(QLocale::Spanish, QLocale::Spain).formattedDataSize(info.size(), 1, QLocale::DataSizeSIFormat)}});
    }
    return out;
}

QVariantList Book::versionPreview(const QString &file) const
{
    QVariantList rows;
    Workbook book;
    if (!read(book, file).isEmpty()) {
        return rows;
    }
    book.recalcAll();
    const int sheet = qBound(0, m_sheet, book.sheetCount() - 1);
    int usedRows = 0, usedCols = 0;
    book.used(sheet, &usedRows, &usedCols);
    for (int r = 0; r < qMin(usedRows, 14); ++r) {
        QVariantList cells;
        for (int c = 0; c < qMin(usedCols, 6); ++c) {
            const Value value = book.value(sheet, r, c);
            cells.append(QVariantMap{{QStringLiteral("text"), value.isBool() ? (value.truth() ? QStringLiteral("☑") : QStringLiteral("☐")) : book.display(sheet, r, c)},
                                     {QStringLiteral("bold"), book.effectiveStyle(sheet, r, c).bold}, {QStringLiteral("number"), value.isNumber()}});
        }
        rows.append(QVariant(cells));
    }
    return rows;
}

QString Book::restoreVersion(const QString &file)
{
    Workbook fresh;
    const QString error = read(fresh, file);
    if (!error.isEmpty()) {
        return error;
    }
    m_book = fresh;
    m_book.recalcAll();
    m_book.setModified(true);
    m_sheet = qBound(0, m_sheet, m_book.sheetCount() - 1);
    touch();
    return {};
}

// --- Recovery ---------------------------------------------------------------------

void Book::saveRecovery()
{
    if (!m_open || !m_book.modified()) {
        return;
    }
    const QString dir = m_dataDir + QStringLiteral("/recovery");
    if (!QDir().mkpath(dir)) {
        return;
    }
    const bool was = m_book.modified();
    const QByteArray bytes = Xlsx::save(m_book);
    m_book.setModified(was);
    QSaveFile file(dir + QLatin1Char('/') + m_session + QStringLiteral(".xlsx"));
    if (file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit()) {
        Store::write(dir + QLatin1Char('/') + m_session + QStringLiteral(".json"),
                     QJsonObject{{QStringLiteral("title"), m_title}, {QStringLiteral("path"), m_path},
                                 {QStringLiteral("when"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    }
}

QVariantList Book::recoveries() const
{
    QVariantList out;
    const QDir dir(m_dataDir + QStringLiteral("/recovery"));
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.xlsx")}, QDir::Files, QDir::Time);
    for (const QFileInfo &info : files) {
        if (info.completeBaseName() == m_session) {
            continue;
        }
        const QJsonObject about = Store::read(dir.filePath(info.completeBaseName() + QStringLiteral(".json")));
        const QDateTime when = QDateTime::fromString(about.value(QStringLiteral("when")).toString(), Qt::ISODate);
        out.append(QVariantMap{{QStringLiteral("file"), info.absoluteFilePath()}, {QStringLiteral("title"), about.value(QStringLiteral("title")).toString(QStringLiteral("Sin título"))},
                               {QStringLiteral("when"), QLocale(QLocale::Spanish, QLocale::Spain).toString(when.isValid() ? when : info.lastModified(), QStringLiteral("d 'de' MMMM, HH:mm"))}});
    }
    return out;
}

QString Book::recover(const QString &file)
{
    Workbook fresh;
    const QString error = read(fresh, file);
    if (!error.isEmpty()) {
        return error;
    }
    const QFileInfo info(file);
    const QString about = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".json");
    const QJsonObject data = Store::read(about);
    m_book = fresh;
    m_book.recalcAll();
    opened(data.value(QStringLiteral("path")).toString(), data.value(QStringLiteral("title")).toString(QStringLiteral("Recuperado")));
    m_book.setModified(true);
    QFile::remove(file);
    QFile::remove(about);
    Q_EMIT changed();
    return {};
}

void Book::discardRecovery(const QString &file)
{
    const QFileInfo info(file);
    if (info.absolutePath() == QFileInfo(m_dataDir + QStringLiteral("/recovery")).absoluteFilePath()) {
        QFile::remove(file);
        QFile::remove(info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".json"));
    }
}

// --- PDF ----------------------------------------------------------------------------

QString Book::exportPdf(const QString &path)
{
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    const Sheet &sheet = m_book.sheet(m_sheet);
    for (const Chart &chart : sheet.charts) {
        usedRows = qMax(usedRows, chart.row + int(chart.height / Workbook::DefaultRowHeight) + 1);
        usedCols = qMax(usedCols, chart.col + int(chart.width / Workbook::DefaultColWidth) + 1);
    }
    usedRows = qMax(usedRows, 1);
    usedCols = qMax(usedCols, 1);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QPdfWriter writer(path);
    writer.setTitle(m_title);
    writer.setCreator(QStringLiteral("Hojas"));
    writer.setResolution(96);
    // Wide sheets go on a landscape page.
    double totalWidth = 0;
    for (int c = 0; c < usedCols; ++c) {
        totalWidth += m_book.colHidden(m_sheet, c) ? 0 : m_book.colWidth(m_sheet, c);
    }
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), totalWidth > 700 ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(12, 12, 12, 14), QPageLayout::Millimeter));
    QPainter p;
    if (!p.begin(&writer)) {
        return QStringLiteral("No se puede escribir en «%1».").arg(QFileInfo(path).fileName());
    }
    const QRectF page(0, 0, writer.width(), writer.height());
    // Shrinks a little to fit the width before breaking columns across pages.
    const double scale = totalWidth > page.width() ? qMax(0.6, page.width() / totalWidth) : 1.0;
    const double pageW = page.width() / scale;
    const double pageH = (page.height() - 18) / scale;

    // Where pages break: whole columns and whole rows.
    QList<QPair<int, int>> colPages, rowPages;
    double run = 0;
    for (int c = 0, start = 0; c <= usedCols; ++c) {
        const double width = c < usedCols && !m_book.colHidden(m_sheet, c) ? m_book.colWidth(m_sheet, c) : 0;
        if (c == usedCols || (run + width > pageW && c > start)) {
            colPages.append({start, c - 1});
            start = c;
            run = 0;
        }
        run += width;
    }
    run = 0;
    for (int r = 0, start = 0; r <= usedRows; ++r) {
        const double height = r < usedRows && !m_book.rowHidden(m_sheet, r) ? m_book.rowHeight(m_sheet, r) : 0;
        if (r == usedRows || (run + height > pageH && r > start)) {
            rowPages.append({start, r - 1});
            start = r;
            run = 0;
        }
        run += height;
    }

    const auto xOf = [this](int col) {
        double x = 0;
        for (int c = 0; c < col; ++c) {
            x += m_book.colHidden(m_sheet, c) ? 0 : m_book.colWidth(m_sheet, c);
        }
        return x;
    };
    const auto yOf = [this](int row) {
        double y = 0;
        for (int r = 0; r < row; ++r) {
            y += m_book.rowHidden(m_sheet, r) ? 0 : m_book.rowHeight(m_sheet, r);
        }
        return y;
    };

    int number = 0;
    const int pages = colPages.size() * rowPages.size();
    for (const auto &rows : std::as_const(rowPages)) {
        for (const auto &cols : std::as_const(colPages)) {
            if (number++ > 0) {
                writer.newPage();
            }
            p.save();
            p.scale(scale, scale);
            const double originX = xOf(cols.first);
            const double originY = yOf(rows.first);
            p.translate(-originX, -originY);
            p.setClipRect(QRectF(originX, originY, xOf(cols.second + 1) - originX, yOf(rows.second + 1) - originY));
            SheetPaint::Look look;
            look.grid = false;
            look.controls = false;
            SheetPaint::paint(&p, m_book, m_sheet, rows.first, cols.first, rows.second, cols.second, xOf, yOf, look);
            for (const Chart &chart : sheet.charts) {
                const QRectF box(xOf(chart.col), yOf(chart.row), chart.width, chart.height);
                p.setPen(QPen(QColor(0xd0, 0xd0, 0xd5), 1));
                p.setBrush(Qt::white);
                p.drawRoundedRect(box, 8, 8);
                QFont font = p.font();
                font.setBold(false);
                font.setItalic(false);
                font.setUnderline(false);
                font.setStrikeOut(false);
                p.setFont(font);
                ChartPaint::paint(&p, box, chart, m_book.chartData(m_sheet, chart), false);
            }
            p.restore();
            QFont small = p.font();
            small.setPixelSize(10);
            small.setBold(false);
            small.setItalic(false);
            small.setUnderline(false);
            small.setStrikeOut(false);
            p.setFont(small);
            p.setPen(QColor(0x8e, 0x8e, 0x93));
            p.drawText(QRectF(0, page.height() - 14, page.width(), 14), Qt::AlignLeft | Qt::AlignVCenter, m_title + QStringLiteral(" · ") + sheet.name);
            p.drawText(QRectF(0, page.height() - 14, page.width(), 14), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("%1 / %2").arg(number).arg(pages));
        }
    }
    p.end();
    return {};
}

// --- An example -----------------------------------------------------------------------

void Book::sample()
{
    create(QStringLiteral("blank"));
    m_title = QStringLiteral("Ventas 2026");
    m_book.beginGroup();
    Writer w{m_book, 0};
    m_book.renameSheet(0, QStringLiteral("Ventas"));
    w.row(0, 0, {QStringLiteral("Mes"), QStringLiteral("Norte"), QStringLiteral("Sur"), QStringLiteral("Online"), QStringLiteral("Total"), QStringLiteral("Objetivo"), QStringLiteral("Cumplido")});
    const QStringList months = {QStringLiteral("Enero"), QStringLiteral("Febrero"), QStringLiteral("Marzo"), QStringLiteral("Abril"), QStringLiteral("Mayo"), QStringLiteral("Junio"),
                                QStringLiteral("Julio"), QStringLiteral("Agosto")};
    const int north[] = {12400, 11850, 14200, 15100, 16750, 15900, 13200, 9800};
    const int south[] = {8300, 9100, 8700, 10400, 11200, 12650, 12900, 10100};
    const int online[] = {5200, 6100, 7450, 8300, 9900, 11200, 12800, 13500};
    for (int i = 0; i < months.size(); ++i) {
        const int r = 1 + i;
        w.row(r, 0, {months.at(i), QString::number(north[i]), QString::number(south[i]), QString::number(online[i]), QStringLiteral("=SUMA(B%1:D%1)").arg(r + 1),
                     QStringLiteral("30000"), QStringLiteral("=E%1>=F%1").arg(r + 1)});
    }
    w.format(Area{1, 1, 8, 5}, QStringLiteral("currency"), 0);
    w.style(Area{1, 6, 8, 6}, [](Style &s) { s.align = 2; });
    m_book.addTable(0, Area{0, 0, 8, 6});
    m_book.setTableTotals(0, 0, true);
    CondFormat rule;
    rule.area = Area{1, 4, 8, 4};
    rule.op = QStringLiteral(">=");
    rule.a = QStringLiteral("35000");
    rule.fill = QStringLiteral("#d9f2e3");
    rule.color = QStringLiteral("#14713c");
    rule.bold = true;
    m_book.addConditional(0, rule);
    m_book.setColWidth(0, 0, 0, 96);
    m_book.setColWidth(0, 6, 6, 108);
    m_book.setFreeze(0, 1, 0);
    Chart chart;
    chart.type = QStringLiteral("column");
    chart.title = QStringLiteral("Ventas por zona");
    chart.area = Area{0, 0, 8, 3};
    chart.row = 12;
    chart.col = 0;
    chart.width = 520;
    chart.height = 280;
    m_book.addChart(0, chart);

    const int second = m_book.addSheet(QStringLiteral("Equipo"));
    Writer t{m_book, second};
    t.row(0, 0, {QStringLiteral("Nombre"), QStringLiteral("Alta"), QStringLiteral("Zona"), QStringLiteral("Horas"), QStringLiteral("Activo")});
    t.row(1, 0, {QStringLiteral("Ana Ruiz"), QStringLiteral("12/01/2021"), QStringLiteral("Norte"), QStringLiteral("37,5"), QStringLiteral("VERDADERO")});
    t.row(2, 0, {QStringLiteral("Luis Peña"), QStringLiteral("03/09/2022"), QStringLiteral("Sur"), QStringLiteral("40"), QStringLiteral("VERDADERO")});
    t.row(3, 0, {QStringLiteral("Marta Gil"), QStringLiteral("21/05/2019"), QStringLiteral("Online"), QStringLiteral("30"), QStringLiteral("FALSO")});
    t.row(4, 0, {QStringLiteral("Iván Soto"), QStringLiteral("08/11/2023"), QStringLiteral("Norte"), QStringLiteral("40"), QStringLiteral("VERDADERO")});
    t.style(Area{0, 0, 0, 4}, [](Style &s) { s.bold = true; });
    t.style(Area{1, 4, 4, 4}, [](Style &s) { s.align = 2; });
    m_book.setColWidth(second, 0, 0, 120);
    m_book.endGroup();
    m_book.setModified(true);
    m_sheet = 0;
    Q_EMIT documentChanged();
    touch();
}
