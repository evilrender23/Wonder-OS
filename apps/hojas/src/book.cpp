#include "book.h"

#include "numfmt.h"

#include <QFont>
#include <QFontMetricsF>
#include <QRegularExpression>
#include <cmath>

// --- The document -----------------------------------------------------------------

Book::Book(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

Area Book::area(int r1, int c1, int r2, int c2) const
{
    return Area{qMax(0, qMin(r1, r2)), qMax(0, qMin(c1, c2)), qMin(Limits::Rows - 1, qMax(r1, r2)), qMin(Limits::Cols - 1, qMax(c1, c2))};
}

int Book::freezeRows() const
{
    return m_open ? m_book.sheet(m_sheet).freezeRows : 0;
}

int Book::freezeCols() const
{
    return m_open ? m_book.sheet(m_sheet).freezeCols : 0;
}

QStringList Book::sheets() const
{
    QStringList names;
    for (int i = 0; m_open && i < m_book.sheetCount(); ++i) {
        names.append(m_book.sheetName(i));
    }
    return names;
}

void Book::fitExtent()
{
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    for (const Chart &chart : m_book.sheet(m_sheet).charts) {
        usedRows = qMax(usedRows, chart.row + int(chart.height / Workbook::DefaultRowHeight) + 2);
        usedCols = qMax(usedCols, chart.col + int(chart.width / Workbook::DefaultColWidth) + 2);
    }
    m_rows = qMin(Limits::Rows, qMax(m_rows, qMax(120, usedRows + 60)));
    m_cols = qMin(Limits::Cols, qMax(m_cols, qMax(30, usedCols + 10)));
}

void Book::touch()
{
    if (m_open) {
        m_sheet = qBound(0, m_sheet, m_book.sheetCount() - 1);
        fitExtent();
    }
    m_types.clear();
    ++m_revision;
    Q_EMIT changed();
    Q_EMIT layoutChanged();
}

void Book::setSheet(int index)
{
    if (!m_open || index < 0 || index >= m_book.sheetCount() || index == m_sheet) {
        return;
    }
    m_sheet = index;
    m_rows = 120;
    m_cols = 30;
    touch();
}

void Book::setZoom(double zoom)
{
    zoom = qBound(0.5, std::round(zoom * 20) / 20, 3.0);
    if (!qFuzzyCompare(zoom, m_zoom)) {
        m_zoom = zoom;
        touch();
    }
}

void Book::reach(int row, int col)
{
    if (!m_open) {
        return;
    }
    bool grew = false;
    if (row + 20 > m_rows && m_rows < Limits::Rows) {
        m_rows = qMin(Limits::Rows, row + 100);
        grew = true;
    }
    if (col + 5 > m_cols && m_cols < Limits::Cols) {
        m_cols = qMin(Limits::Cols, col + 20);
        grew = true;
    }
    if (grew) {
        touch();
    }
}

// --- Sheets -----------------------------------------------------------------------

void Book::addSheet()
{
    m_sheet = m_book.addSheet();
    touch();
}

bool Book::removeSheet(int index)
{
    const bool done = m_book.removeSheet(index);
    if (done && m_sheet >= index && m_sheet > 0) {
        --m_sheet;
    }
    touch();
    return done;
}

bool Book::renameSheet(int index, const QString &name)
{
    const bool done = m_book.renameSheet(index, name.trimmed());
    touch();
    return done;
}

void Book::duplicateSheet(int index)
{
    m_sheet = m_book.duplicateSheet(index);
    touch();
}

void Book::moveSheet(int from, int to)
{
    if (m_book.moveSheet(from, to)) {
        if (m_sheet == from) {
            m_sheet = to;
        }
        touch();
    }
}

// --- Geometry ---------------------------------------------------------------------

double Book::colWidth(int col) const
{
    return m_open && !m_book.colHidden(m_sheet, col) ? std::round(m_book.colWidth(m_sheet, col) * m_zoom) : 0;
}

double Book::rowHeight(int row) const
{
    return m_open && !m_book.rowHidden(m_sheet, row) ? std::round(m_book.rowHeight(m_sheet, row) * m_zoom) : 0;
}

double Book::colPos(int col) const
{
    if (!m_open) {
        return 0;
    }
    const Sheet &sheet = m_book.sheet(m_sheet);
    const double normal = std::round(Workbook::DefaultColWidth * m_zoom);
    double x = col * normal;
    for (auto it = sheet.colWidths.constBegin(); it != sheet.colWidths.constEnd() && it.key() < col; ++it) {
        if (!sheet.hiddenCols.contains(it.key())) {
            x += std::round(it.value() * m_zoom) - normal;
        }
    }
    for (int hidden : sheet.hiddenCols) {
        if (hidden < col) {
            x -= normal;
        }
    }
    return x;
}

double Book::rowPos(int row) const
{
    if (!m_open) {
        return 0;
    }
    const Sheet &sheet = m_book.sheet(m_sheet);
    const double normal = std::round(Workbook::DefaultRowHeight * m_zoom);
    double y = row * normal;
    for (auto it = sheet.rowHeights.constBegin(); it != sheet.rowHeights.constEnd() && it.key() < row; ++it) {
        if (!sheet.hiddenRows.contains(it.key()) && !sheet.filteredRows.contains(it.key())) {
            y += std::round(it.value() * m_zoom) - normal;
        }
    }
    for (int hidden : sheet.hiddenRows) {
        if (hidden < row) {
            y -= normal;
        }
    }
    for (int hidden : sheet.filteredRows) {
        if (hidden < row && !sheet.hiddenRows.contains(hidden)) {
            y -= normal;
        }
    }
    return y;
}

int Book::colAt(double x) const
{
    int low = 0, high = m_cols - 1;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (colPos(middle) <= x) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

int Book::rowAt(double y) const
{
    int low = 0, high = m_rows - 1;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (rowPos(middle) <= y) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

void Book::setColWidth(int c1, int c2, double pixels)
{
    m_book.setColWidth(m_sheet, qMin(c1, c2), qMax(c1, c2), qBound(12.0, pixels / m_zoom, 1200.0));
    touch();
}

void Book::setRowHeight(int r1, int r2, double pixels)
{
    m_book.setRowHeight(m_sheet, qMin(r1, r2), qMax(r1, r2), qBound(8.0, pixels / m_zoom, 800.0));
    touch();
}

void Book::fitCols(int c1, int c2)
{
    m_book.beginGroup();
    for (int c = qMin(c1, c2); c <= qMax(c1, c2); ++c) {
        const double wanted = m_book.fittingWidth(m_sheet, c, [](const QString &text, const Style &style) {
            QFont font;
            font.setPixelSize(style.size > 0 ? int(style.size * 96.0 / 72.0) : 13);
            font.setBold(style.bold);
            return QFontMetricsF(font).horizontalAdvance(text) + 14;
        });
        if (wanted > 0) {
            m_book.setColWidth(m_sheet, c, c, qBound(30.0, std::ceil(wanted), 600.0));
        }
    }
    m_book.endGroup();
    touch();
}

void Book::setColsHidden(int c1, int c2, bool hidden)
{
    m_book.setColsHidden(m_sheet, qMin(c1, c2), qMax(c1, c2), hidden);
    touch();
}

void Book::setRowsHidden(int r1, int r2, bool hidden)
{
    m_book.setRowsHidden(m_sheet, qMin(r1, r2), qMax(r1, r2), hidden);
    touch();
}

void Book::setFreeze(int rows, int cols)
{
    m_book.setFreeze(m_sheet, rows, cols);
    touch();
}

int Book::nextRow(int row, int step) const
{
    int next = row + step;
    while (next >= 0 && next < Limits::Rows && m_book.rowHidden(m_sheet, next)) {
        next += step;
    }
    return next < 0 || next >= Limits::Rows ? row : next;
}

int Book::nextCol(int col, int step) const
{
    int next = col + step;
    while (next >= 0 && next < Limits::Cols && m_book.colHidden(m_sheet, next)) {
        next += step;
    }
    return next < 0 || next >= Limits::Cols ? col : next;
}

QVariantMap Book::jump(int row, int col, int dr, int dc) const
{
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    const auto filled = [this](int r, int c) {
        const Cell *cell = m_book.cell(m_sheet, r, c);
        return cell && (!cell->value.isEmpty() || cell->formula);
    };
    const auto inside = [&](int r, int c) {
        return r >= 0 && c >= 0 && r < qMax(usedRows, 1) && c < qMax(usedCols, 1);
    };
    int r = row, c = col;
    if (filled(r, c) && inside(r + dr, c + dc) && filled(r + dr, c + dc)) {
        // Along a run of filled cells, to its end.
        while (inside(r + dr, c + dc) && filled(r + dr, c + dc)) {
            r += dr;
            c += dc;
        }
    } else {
        // Over the gap, to the next filled cell or the edge of what is in use.
        r += dr;
        c += dc;
        while (inside(r, c) && !filled(r, c)) {
            r += dr;
            c += dc;
        }
        if (!inside(r, c)) {
            r = dr > 0 ? qMax(row, usedRows - 1) : (dr < 0 ? 0 : row);
            c = dc > 0 ? qMax(col, usedCols - 1) : (dc < 0 ? 0 : col);
        }
    }
    return {{QStringLiteral("row"), r}, {QStringLiteral("col"), c}};
}

// --- Cells ------------------------------------------------------------------------

QString Book::widgetAt(int row, int col)
{
    if (!m_open) {
        return {};
    }
    const Sheet &sheet = m_book.sheet(m_sheet);
    if (sheet.hasFilter && row == sheet.filterArea.r1 && col >= sheet.filterArea.c1 && col <= sheet.filterArea.c2) {
        return QStringLiteral("filter");
    }
    const Cell *cell = m_book.cell(m_sheet, row, col);
    if (cell && !cell->formula && cell->value.isBool()) {
        return QStringLiteral("check");
    }
    return {};
}

QString Book::input(int row, int col) const
{
    return m_open ? m_book.input(m_sheet, row, col) : QString();
}

QString Book::display(int row, int col)
{
    return m_open ? m_book.display(m_sheet, row, col) : QString();
}

QString Book::setInput(int row, int col, const QString &typed)
{
    if (!m_open) {
        return {};
    }
    if (m_book.input(m_sheet, row, col) == typed) {
        return {};
    }
    const QString error = m_book.setInput(m_sheet, row, col, typed);
    if (error.isEmpty()) {
        touch();
    }
    return error;
}

QString Book::setInputs(int r1, int c1, int r2, int c2, const QString &typed)
{
    const Area a = m_book.clipped(m_sheet, area(r1, c1, r2, c2));
    QString error;
    m_book.beginGroup();
    for (int r = a.r1; r <= a.r2 && error.isEmpty(); ++r) {
        for (int c = a.c1; c <= a.c2 && error.isEmpty(); ++c) {
            QString text = typed;
            if (typed.startsWith(QLatin1Char('=')) && (r != a.r1 || c != a.c1)) {
                const NodePtr node = Formula::parse(typed.mid(1));
                if (node) {
                    text = QLatin1Char('=') + Formula::print(Formula::shifted(node, r - a.r1, c - a.c1), true);
                }
            }
            error = m_book.setInput(m_sheet, r, c, text);
        }
    }
    m_book.endGroup();
    touch();
    return error;
}

QVariantMap Book::extent(int row, int col) const
{
    Area merged{row, col, row, col};
    if (m_open) {
        m_book.mergeAt(m_sheet, row, col, &merged);
    }
    return {{QStringLiteral("r1"), merged.r1}, {QStringLiteral("c1"), merged.c1}, {QStringLiteral("r2"), merged.r2}, {QStringLiteral("c2"), merged.c2}};
}

QVariantMap Book::info(int row, int col)
{
    QVariantMap out;
    if (!m_open) {
        return out;
    }
    const Style &style = m_book.cellStyle(m_sheet, row, col);
    out.insert(QStringLiteral("bold"), style.bold);
    out.insert(QStringLiteral("italic"), style.italic);
    out.insert(QStringLiteral("underline"), style.underline);
    out.insert(QStringLiteral("strike"), style.strike);
    out.insert(QStringLiteral("wrap"), style.wrap);
    out.insert(QStringLiteral("color"), style.color);
    out.insert(QStringLiteral("fill"), style.fill);
    out.insert(QStringLiteral("align"), style.align);
    out.insert(QStringLiteral("valign"), style.valign);
    out.insert(QStringLiteral("size"), style.size);
    out.insert(QStringLiteral("borders"), style.borders);
    out.insert(QStringLiteral("code"), style.format);
    out.insert(QStringLiteral("family"), NumFmt::family(style.format));
    out.insert(QStringLiteral("decimals"), NumFmt::decimals(style.format));
    out.insert(QStringLiteral("merged"), m_book.mergeAt(m_sheet, row, col, nullptr));
    out.insert(QStringLiteral("formula"), !m_book.formula(m_sheet, row, col).isEmpty());
    const Value value = m_book.value(m_sheet, row, col);
    out.insert(QStringLiteral("kind"), value.isNumber() ? (NumFmt::isDate(style.format) ? QStringLiteral("date") : QStringLiteral("number"))
            : value.isBool() ? QStringLiteral("check") : value.isText() ? QStringLiteral("text") : value.isError() ? QStringLiteral("error") : QString());
    out.insert(QStringLiteral("table"), m_book.tableAt(m_sheet, row, col));
    return out;
}

void Book::toggleCheck(int row, int col)
{
    const Value value = m_book.value(m_sheet, row, col);
    if (value.isBool() && m_book.formula(m_sheet, row, col).isEmpty()) {
        m_book.setInput(m_sheet, row, col, value.truth() ? QStringLiteral("FALSO") : QStringLiteral("VERDADERO"));
        touch();
    }
}

QString Book::name(int r1, int c1, int r2, int c2) const
{
    const Area a = area(r1, c1, r2, c2);
    Area merged;
    if (m_open && m_book.mergeAt(m_sheet, a.r1, a.c1, &merged) && merged == a) {
        return Formula::cellName(a.r1, a.c1);
    }
    return a.rows() == 1 && a.cols() == 1 ? Formula::cellName(a.r1, a.c1) : Formula::areaName(a.r1, a.c1, a.r2, a.c2);
}

QVariantMap Book::parseName(const QString &text) const
{
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    QString clean = text.trimmed().toUpper();
    clean.remove(QLatin1Char('$'));
    const bool ok = Formula::parseArea(clean, &r1, &c1, &r2, &c2);
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("r1"), r1}, {QStringLiteral("c1"), c1}, {QStringLiteral("r2"), r2}, {QStringLiteral("c2"), c2}};
}

QString Book::columnName(int col) const
{
    return Formula::columnName(col);
}

static QVariantMap areaMap(const Area &a)
{
    return {{QStringLiteral("r1"), a.r1}, {QStringLiteral("c1"), a.c1}, {QStringLiteral("r2"), a.r2}, {QStringLiteral("c2"), a.c2}};
}

QVariantMap Book::region(int row, int col) const
{
    return areaMap(m_book.region(m_sheet, row, col));
}

QVariantMap Book::usedArea() const
{
    int rows = 0, cols = 0;
    m_book.used(m_sheet, &rows, &cols);
    return areaMap(Area{0, 0, qMax(0, rows - 1), qMax(0, cols - 1)});
}

// --- Format -----------------------------------------------------------------------

void Book::setStyle(int r1, int c1, int r2, int c2, const QString &key, const QVariant &value)
{
    m_book.changeStyle(m_sheet, area(r1, c1, r2, c2), [&](Style &style) {
        if (key == QLatin1String("bold")) {
            style.bold = value.toBool();
        } else if (key == QLatin1String("italic")) {
            style.italic = value.toBool();
        } else if (key == QLatin1String("underline")) {
            style.underline = value.toBool();
        } else if (key == QLatin1String("strike")) {
            style.strike = value.toBool();
        } else if (key == QLatin1String("wrap")) {
            style.wrap = value.toBool();
        } else if (key == QLatin1String("color")) {
            style.color = value.toString();
        } else if (key == QLatin1String("fill")) {
            style.fill = value.toString();
        } else if (key == QLatin1String("align")) {
            style.align = qBound(0, value.toInt(), 3);
        } else if (key == QLatin1String("valign")) {
            style.valign = qBound(0, value.toInt(), 2);
        } else if (key == QLatin1String("size")) {
            style.size = qBound(0, value.toInt(), 96);
        }
    });
    touch();
}

void Book::setFormat(int r1, int c1, int r2, int c2, const QString &family)
{
    setFormatCode(r1, c1, r2, c2, NumFmt::familyCode(family, family == QLatin1String("percent") ? 0 : 2));
}

void Book::setFormatCode(int r1, int c1, int r2, int c2, const QString &code)
{
    m_book.changeStyle(m_sheet, area(r1, c1, r2, c2), [&code](Style &style) {
        style.format = code;
    });
    touch();
}

void Book::changeDecimals(int r1, int c1, int r2, int c2, int delta)
{
    m_book.changeStyle(m_sheet, area(r1, c1, r2, c2), [delta](Style &style) {
        style.format = NumFmt::withDecimals(style.format.isEmpty() ? QStringLiteral("0") : style.format, delta);
    });
    touch();
}

void Book::setBorders(int r1, int c1, int r2, int c2, const QString &which, const QString &color)
{
    m_book.setBorders(m_sheet, area(r1, c1, r2, c2), which, color);
    touch();
}

void Book::clear(int r1, int c1, int r2, int c2, bool contents, bool formats)
{
    m_book.clearArea(m_sheet, area(r1, c1, r2, c2), contents, formats);
    touch();
}

bool Book::merge(int r1, int c1, int r2, int c2)
{
    const bool done = m_book.merge(m_sheet, area(r1, c1, r2, c2));
    touch();
    return done;
}

bool Book::unmerge(int r1, int c1, int r2, int c2)
{
    const bool done = m_book.unmerge(m_sheet, area(r1, c1, r2, c2));
    touch();
    return done;
}

void Book::addConditional(int r1, int c1, int r2, int c2, const QString &op, const QString &a, const QString &b, const QString &fill, const QString &color, bool bold)
{
    CondFormat rule;
    rule.area = area(r1, c1, r2, c2);
    rule.op = op;
    rule.a = a;
    rule.b = b;
    rule.fill = fill;
    rule.color = color;
    rule.bold = bold;
    m_book.addConditional(m_sheet, rule);
    touch();
}

void Book::clearConditionals(int r1, int c1, int r2, int c2)
{
    m_book.clearConditionals(m_sheet, area(r1, c1, r2, c2));
    touch();
}

QVariantList Book::conditionals() const
{
    QVariantList out;
    if (!m_open) {
        return out;
    }
    static const QHash<QString, QString> words = {
        {QStringLiteral(">"), QStringLiteral("mayor que %1")}, {QStringLiteral("<"), QStringLiteral("menor que %1")},
        {QStringLiteral(">="), QStringLiteral("mayor o igual que %1")}, {QStringLiteral("<="), QStringLiteral("menor o igual que %1")},
        {QStringLiteral("="), QStringLiteral("igual a %1")}, {QStringLiteral("<>"), QStringLiteral("distinto de %1")},
        {QStringLiteral("between"), QStringLiteral("entre %1 y %2")}, {QStringLiteral("contains"), QStringLiteral("contiene «%1»")},
        {QStringLiteral("duplicate"), QStringLiteral("valores duplicados")}, {QStringLiteral("top"), QStringLiteral("los %1 mayores")},
        {QStringLiteral("bottom"), QStringLiteral("los %1 menores")},
    };
    for (const CondFormat &rule : m_book.sheet(m_sheet).conditionals) {
        QString text = words.value(rule.op, rule.op);
        if (text.contains(QLatin1String("%1"))) {
            text = text.arg(rule.a);
        }
        if (text.contains(QLatin1String("%2"))) {
            text = text.arg(rule.b);
        }
        out.append(QVariantMap{{QStringLiteral("range"), Formula::areaName(rule.area.r1, rule.area.c1, rule.area.r2, rule.area.c2)},
                               {QStringLiteral("text"), text}, {QStringLiteral("fill"), rule.fill}, {QStringLiteral("color"), rule.color}});
    }
    return out;
}

void Book::removeConditional(int index)
{
    const QList<CondFormat> rules = m_book.sheet(m_sheet).conditionals;
    if (index < 0 || index >= rules.size()) {
        return;
    }
    m_book.beginGroup();
    m_book.clearConditionals(m_sheet, Area{0, 0, Limits::Rows - 1, Limits::Cols - 1});
    for (int i = 0; i < rules.size(); ++i) {
        if (i != index) {
            m_book.addConditional(m_sheet, rules.at(i));
        }
    }
    m_book.endGroup();
    touch();
}

// --- Clipboard and filling ----------------------------------------------------------

QString Book::copy(int r1, int c1, int r2, int c2, bool cut)
{
    m_clip = m_book.copy(m_sheet, area(r1, c1, r2, c2));
    m_clip.cut = cut;
    Q_EMIT changed();
    return m_clip.tsv;
}

QVariantMap Book::paste(int r1, int c1, int r2, int c2, const QString &text, const QString &what)
{
    const Area target = area(r1, c1, r2, c2);
    Area done = target;
    const auto same = [](QString a, QString b) {
        a.remove(QLatin1Char('\r'));
        b.remove(QLatin1Char('\r'));
        while (a.endsWith(QLatin1Char('\n'))) {
            a.chop(1);
        }
        while (b.endsWith(QLatin1Char('\n'))) {
            b.chop(1);
        }
        return a == b;
    };
    if (!m_clip.isEmpty() && (text.isEmpty() || same(text, m_clip.tsv))) {
        done = m_book.paste(m_clip, m_sheet, target, what);
        if (m_clip.cut) {
            m_clip = Clip();
        }
    } else if (!text.isEmpty()) {
        done = m_book.pasteText(m_sheet, target.r1, target.c1, text);
    }
    touch();
    return areaMap(done);
}

QVariantMap Book::copied() const
{
    if (m_clip.isEmpty() || m_clip.sheet != m_sheet) {
        return {};
    }
    return areaMap(Area{m_clip.row, m_clip.col, m_clip.row + m_clip.rows - 1, m_clip.col + m_clip.cols - 1});
}

void Book::forgetCopy()
{
    if (!m_clip.isEmpty()) {
        m_clip = Clip();
        Q_EMIT changed();
    }
}

void Book::fillDown(int r1, int c1, int r2, int c2)
{
    m_book.fillDown(m_sheet, area(r1, c1, r2, c2));
    touch();
}

void Book::fillRight(int r1, int c1, int r2, int c2)
{
    m_book.fillRight(m_sheet, area(r1, c1, r2, c2));
    touch();
}

void Book::fillSeries(int sr1, int sc1, int sr2, int sc2, int r1, int c1, int r2, int c2)
{
    m_book.fillSeries(m_sheet, area(sr1, sc1, sr2, sc2), area(r1, c1, r2, c2));
    touch();
}

// --- Rows and columns ----------------------------------------------------------------

void Book::insertRows(int at, int count)
{
    m_book.insertRows(m_sheet, at, count);
    touch();
}

void Book::deleteRows(int at, int count)
{
    m_book.deleteRows(m_sheet, at, count);
    touch();
}

void Book::insertCols(int at, int count)
{
    m_book.insertCols(m_sheet, at, count);
    touch();
}

void Book::deleteCols(int at, int count)
{
    m_book.deleteCols(m_sheet, at, count);
    touch();
}

QString Book::columnType(int col)
{
    if (!m_open) {
        return {};
    }
    const auto cached = m_types.constFind(col);
    if (cached != m_types.constEnd()) {
        return cached.value();
    }
    int counts[4] = {0, 0, 0, 0}; // text, number, date, check
    int seen = 0;
    bool first = true;
    const auto &rows = m_book.sheet(m_sheet).rows;
    for (auto row = rows.constBegin(); row != rows.constEnd() && seen < 2000; ++row) {
        const auto it = row->constFind(col);
        if (it == row->constEnd() || (it->value.isEmpty() && !it->formula)) {
            continue;
        }
        const Value v = it->formula ? m_book.value(m_sheet, row.key(), col) : it->value;
        if (first) {
            // A text at the top is the column's header, not its contents.
            first = false;
            if (v.isText()) {
                continue;
            }
        }
        ++seen;
        if (v.isBool()) {
            ++counts[3];
        } else if (v.isNumber()) {
            ++counts[NumFmt::isDate(m_book.style(it->style).format) ? 2 : 1];
        } else {
            ++counts[0];
        }
    }
    static const char *const names[] = {"text", "number", "date", "check"};
    QString type;
    for (int i = 0; i < 4; ++i) {
        if (seen > 0 && counts[i] * 10 >= seen * 8) {
            type = QString::fromLatin1(names[i]);
        }
    }
    m_types.insert(col, type);
    return type;
}

void Book::setColumnType(int col, const QString &type)
{
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    int first = 0;
    while (first < usedRows && m_book.value(m_sheet, first, col).isEmpty()) {
        ++first;
    }
    if (first < usedRows && m_book.value(m_sheet, first, col).isText()) {
        ++first;
    }
    int last = usedRows - 1;
    const int table = first > 0 ? m_book.tableAt(m_sheet, first - 1, col) : -1;
    if (table >= 0) {
        const Table &t = m_book.sheet(m_sheet).tables.at(table);
        last = t.totals ? t.area.r2 - 1 : t.area.r2;
    }
    if (first > last) {
        return;
    }
    m_book.beginGroup();
    const Area a{first, col, last, col};
    if (type == QLatin1String("check")) {
        m_book.changeStyle(m_sheet, a, [](Style &style) { style.format.clear(); style.align = 2; });
        for (int r = first; r <= last; ++r) {
            if (!m_book.formula(m_sheet, r, col).isEmpty()) {
                continue;
            }
            const Value v = m_book.value(m_sheet, r, col);
            const QString text = v.isText() ? v.text.trimmed().toLower() : QString();
            const bool on = v.isBool() ? v.truth() : v.isNumber() ? v.num != 0
                : (text == QLatin1String("sí") || text == QLatin1String("si") || text == QLatin1String("x") || text == QLatin1String("hecho") || text == QLatin1String("verdadero") || text == QLatin1String("true"));
            m_book.setInput(m_sheet, r, col, on ? QStringLiteral("VERDADERO") : QStringLiteral("FALSO"));
        }
    } else {
        const QString family = type == QLatin1String("number") ? QStringLiteral("number") : type == QLatin1String("date") ? QStringLiteral("date")
            : type == QLatin1String("currency") ? QStringLiteral("currency") : type == QLatin1String("text") ? QStringLiteral("text") : QString();
        const QString code = NumFmt::familyCode(family, 2);
        m_book.changeStyle(m_sheet, a, [&code](Style &style) { style.format = code; style.align = 0; });
        for (int r = first; r <= last; ++r) {
            if (!m_book.formula(m_sheet, r, col).isEmpty()) {
                continue;
            }
            const Value v = m_book.value(m_sheet, r, col);
            if (type == QLatin1String("text") && !v.isEmpty() && !v.isText()) {
                m_book.setInput(m_sheet, r, col, v.isBool() ? (v.truth() ? QStringLiteral("Sí") : QStringLiteral("No")) : NumFmt::general(v.num));
            } else if (type != QLatin1String("text") && v.isBool()) {
                m_book.setInput(m_sheet, r, col, v.truth() ? QStringLiteral("1") : QStringLiteral("0"));
            } else if (type != QLatin1String("text") && v.isText()) {
                // What reads as a number or a date becomes one.
                const Value parsed = NumFmt::parseInput(v.text);
                if (parsed.isNumber()) {
                    m_book.setInput(m_sheet, r, col, v.text);
                }
            }
        }
    }
    m_book.endGroup();
    touch();
}

// --- Data -------------------------------------------------------------------------

void Book::sort(int r1, int c1, int r2, int c2, int column, bool ascending)
{
    Area a = area(r1, c1, r2, c2);
    if (a.rows() == 1 && a.cols() == 1) {
        a = m_book.region(m_sheet, a.r1, a.c1);
    }
    a = m_book.clipped(m_sheet, a);
    // A first row of texts over a second that is not all text is a header.
    bool header = a.rows() > 1;
    bool otherwise = false;
    for (int c = a.c1; c <= a.c2 && header; ++c) {
        const Value top = m_book.value(m_sheet, a.r1, c);
        header = top.isText() || top.isEmpty();
        otherwise = otherwise || !m_book.value(m_sheet, a.r1 + 1, c).isText();
    }
    const int table = m_book.tableAt(m_sheet, a.r1, a.c1);
    if (table >= 0) {
        const Table &t = m_book.sheet(m_sheet).tables.at(table);
        a = t.area;
        if (t.totals) {
            --a.r2;
        }
        header = true;
        otherwise = true;
    }
    m_book.sort(m_sheet, a, qBound(a.c1, column, a.c2), ascending, header && otherwise);
    touch();
}

bool Book::hasFilter() const
{
    return m_open && m_book.sheet(m_sheet).hasFilter;
}

void Book::toggleFilter(int r1, int c1, int r2, int c2)
{
    if (hasFilter()) {
        m_book.clearFilter(m_sheet);
    } else {
        m_book.setFilter(m_sheet, area(r1, c1, r2, c2));
    }
    touch();
}

QVariantList Book::filterValues(int column)
{
    QVariantList out;
    if (!hasFilter()) {
        return out;
    }
    const Sheet &sheet = m_book.sheet(m_sheet);
    const int offset = column - sheet.filterArea.c1;
    const bool all = !sheet.filterAllowed.contains(offset);
    const QSet<QString> allowed = sheet.filterAllowed.value(offset);
    const QStringList values = m_book.filterValues(m_sheet, offset);
    for (const QString &text : values) {
        out.append(QVariantMap{{QStringLiteral("text"), text}, {QStringLiteral("on"), all || allowed.contains(text)}});
    }
    return out;
}

void Book::setFilterAllowed(int column, const QStringList &allowed, bool all)
{
    if (hasFilter()) {
        m_book.setFilterAllowed(m_sheet, column - m_book.sheet(m_sheet).filterArea.c1, allowed, all);
        touch();
    }
}

QVariantMap Book::find(const QString &text, int row, int col, bool forward, bool matchCase, bool whole)
{
    const bool found = m_open && m_book.find(m_sheet, text, matchCase, whole, forward, &row, &col);
    return {{QStringLiteral("found"), found}, {QStringLiteral("row"), row}, {QStringLiteral("col"), col}};
}

bool Book::replaceAt(int row, int col, const QString &text, const QString &with, bool matchCase, bool whole)
{
    const bool done = m_book.replaceAt(m_sheet, row, col, text, with, matchCase, whole);
    touch();
    return done;
}

int Book::replaceAll(const QString &text, const QString &with, bool matchCase, bool whole)
{
    const int count = m_book.replaceAll(m_sheet, text, with, matchCase, whole);
    touch();
    return count;
}

QVariantMap Book::stats(int r1, int c1, int r2, int c2)
{
    if (!m_open) {
        return {};
    }
    const Area a = area(r1, c1, r2, c2);
    const SelectionStats s = m_book.stats(m_sheet, a);
    // The numbers read the way the first number of the selection is shown.
    QString code;
    bool found = false;
    m_book.forEachCell(RangeRef{m_sheet, a.r1, a.c1, a.r2, a.c2}, [&](int r, int c, const Value &v) {
        if (!found && v.isNumber()) {
            found = true;
            code = m_book.cellStyle(m_sheet, r, c).format;
        }
    });
    const QString family = NumFmt::family(code);
    if (family != QLatin1String("currency") && family != QLatin1String("number") && family != QLatin1String("percent")) {
        code.clear();
    }
    const auto shown = [&code](double number) {
        return code.isEmpty() ? NumFmt::general(std::round(number * 1e6) / 1e6) : NumFmt::format(Value(number), code);
    };
    return {{QStringLiteral("count"), s.count}, {QStringLiteral("numbers"), s.numbers}, {QStringLiteral("sum"), s.sum}, {QStringLiteral("average"), s.average},
            {QStringLiteral("sumText"), shown(s.sum)}, {QStringLiteral("averageText"), shown(s.average)},
            {QStringLiteral("minText"), shown(s.min)}, {QStringLiteral("maxText"), shown(s.max)}};
}

QVariantMap Book::aggregate(int r1, int c1, int r2, int c2, const QString &function)
{
    Area a = area(r1, c1, r2, c2);
    if (a.rows() == 1 && a.cols() == 1) {
        a = m_book.region(m_sheet, a.r1, a.c1);
    }
    a = m_book.clipped(m_sheet, a);
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    a.r2 = qMin(a.r2, qMax(usedRows - 1, a.r1));
    a.c2 = qMin(a.c2, qMax(usedCols - 1, a.c1));
    const QString name = Formula::canonicalFunction(function).isEmpty() ? QStringLiteral("SUM") : Formula::canonicalFunction(function);
    Area written;
    m_book.beginGroup();
    if (a.rows() == 1 && a.cols() > 1) {
        const int col = a.c2 + 1;
        m_book.setInput(m_sheet, a.r1, col, QStringLiteral("=%1(%2)").arg(name, Formula::areaName(a.r1, a.c1, a.r2, a.c2)));
        written = Area{a.r1, col, a.r1, col};
    } else {
        // Under the columns that hold numbers; the first free row below.
        int row = a.r2 + 1;
        written = Area{row, a.c2, row, a.c1};
        for (int c = a.c1; c <= a.c2; ++c) {
            int first = a.r1;
            while (first <= a.r2 && !m_book.value(m_sheet, first, c).isNumber()) {
                ++first;
            }
            if (first > a.r2) {
                continue;
            }
            m_book.setInput(m_sheet, row, c, QStringLiteral("=%1(%2)").arg(name, Formula::areaName(first, c, a.r2, c)));
            if (name != QLatin1String("COUNT") && name != QLatin1String("COUNTA")) {
                const QString format = m_book.cellStyle(m_sheet, a.r2, c).format;
                m_book.changeStyle(m_sheet, Area{row, c, row, c}, [&format](Style &style) { style.format = format; style.bold = true; });
            }
            written.c1 = qMin(written.c1, c);
            written.c2 = qMax(written.c2, c);
        }
        if (written.c1 > written.c2) {
            written = Area{row, a.c1, row, a.c1};
        }
    }
    m_book.endGroup();
    touch();
    return areaMap(written);
}

// --- Tables -----------------------------------------------------------------------

bool Book::makeTable(int r1, int c1, int r2, int c2)
{
    const int index = m_book.addTable(m_sheet, area(r1, c1, r2, c2));
    touch();
    return index >= 0;
}

QVariantMap Book::tableAt(int row, int col) const
{
    const int index = m_open ? m_book.tableAt(m_sheet, row, col) : -1;
    if (index < 0) {
        return {};
    }
    const Table &t = m_book.sheet(m_sheet).tables.at(index);
    QVariantMap out = areaMap(t.area);
    out.insert(QStringLiteral("index"), index);
    out.insert(QStringLiteral("totals"), t.totals);
    out.insert(QStringLiteral("range"), Formula::areaName(t.area.r1, t.area.c1, t.area.r2, t.area.c2));
    return out;
}

void Book::removeTable(int index)
{
    m_book.removeTable(m_sheet, index);
    touch();
}

void Book::setTableTotals(int index, bool on)
{
    m_book.setTableTotals(m_sheet, index, on);
    touch();
}

// --- Charts -----------------------------------------------------------------------

QVariantList Book::charts() const
{
    QVariantList out;
    if (!m_open) {
        return out;
    }
    for (const Chart &chart : m_book.sheet(m_sheet).charts) {
        out.append(QVariantMap{
            {QStringLiteral("id"), chart.id}, {QStringLiteral("type"), chart.type}, {QStringLiteral("title"), chart.title}, {QStringLiteral("legend"), chart.legend},
            {QStringLiteral("headerRow"), chart.headerRow}, {QStringLiteral("headerCol"), chart.headerCol},
            {QStringLiteral("x"), colPos(chart.col)}, {QStringLiteral("y"), rowPos(chart.row)},
            {QStringLiteral("width"), chart.width * m_zoom}, {QStringLiteral("height"), chart.height * m_zoom},
            {QStringLiteral("range"), Formula::areaName(chart.area.r1, chart.area.c1, chart.area.r2, chart.area.c2)},
        });
    }
    return out;
}

bool Book::chart(int id, Chart *chart) const
{
    if (!m_open) {
        return false;
    }
    for (const Chart &candidate : m_book.sheet(m_sheet).charts) {
        if (candidate.id == id) {
            *chart = candidate;
            return true;
        }
    }
    return false;
}

int Book::addChart(int r1, int c1, int r2, int c2, const QString &type)
{
    Area a = area(r1, c1, r2, c2);
    if (a.rows() == 1 && a.cols() == 1) {
        a = m_book.region(m_sheet, a.r1, a.c1);
    }
    a = m_book.clipped(m_sheet, a);
    int usedRows = 0, usedCols = 0;
    m_book.used(m_sheet, &usedRows, &usedCols);
    a.r2 = qMin(a.r2, qMax(usedRows - 1, a.r1));
    a.c2 = qMin(a.c2, qMax(usedCols - 1, a.c1));
    Chart chart;
    chart.type = type.isEmpty() ? QStringLiteral("column") : type;
    chart.area = a;
    // Names come from the first row and column only when they hold text.
    bool texts = false;
    for (int c = a.c1; c <= a.c2; ++c) {
        texts = texts || m_book.value(m_sheet, a.r1, c).isText();
    }
    chart.headerRow = texts && a.rows() > 1;
    texts = false;
    for (int r = a.r1 + (chart.headerRow ? 1 : 0); r <= a.r2; ++r) {
        texts = texts || !m_book.value(m_sheet, r, a.c1).isNumber();
    }
    chart.headerCol = (texts || chart.type == QLatin1String("scatter")) && a.cols() > 1;
    // Beside the data, two columns clear of it.
    chart.row = a.r1;
    chart.col = a.c2 + 2;
    const int id = m_book.addChart(m_sheet, chart);
    touch();
    return id;
}

void Book::setChart(int id, const QString &key, const QVariant &value)
{
    Chart c;
    if (!chart(id, &c)) {
        return;
    }
    if (key == QLatin1String("type")) {
        c.type = value.toString();
    } else if (key == QLatin1String("title")) {
        c.title = value.toString();
    } else if (key == QLatin1String("legend")) {
        c.legend = value.toBool();
    } else if (key == QLatin1String("headerRow")) {
        c.headerRow = value.toBool();
    } else if (key == QLatin1String("headerCol")) {
        c.headerCol = value.toBool();
    } else if (key == QLatin1String("width")) {
        c.width = qBound(160.0, value.toDouble() / m_zoom, 2400.0);
    } else if (key == QLatin1String("height")) {
        c.height = qBound(120.0, value.toDouble() / m_zoom, 1800.0);
    } else if (key == QLatin1String("range")) {
        const QVariantMap parsed = parseName(value.toString());
        if (!parsed.value(QStringLiteral("ok")).toBool()) {
            return;
        }
        c.area = Area{parsed.value(QStringLiteral("r1")).toInt(), parsed.value(QStringLiteral("c1")).toInt(), parsed.value(QStringLiteral("r2")).toInt(),
                      parsed.value(QStringLiteral("c2")).toInt()};
    }
    m_book.updateChart(m_sheet, c);
    touch();
}

void Book::moveChart(int id, double x, double y)
{
    Chart c;
    if (!chart(id, &c)) {
        return;
    }
    c.col = colAt(qMax(0.0, x));
    c.row = rowAt(qMax(0.0, y));
    m_book.updateChart(m_sheet, c);
    touch();
}

void Book::removeChart(int id)
{
    m_book.removeChart(m_sheet, id);
    touch();
}

// --- Formulas ---------------------------------------------------------------------

/// Letters without their accents, upper case: «código» finds «CÓDIGO».
static QString folded(const QString &text)
{
    QString out;
    for (const QChar ch : text.normalized(QString::NormalizationForm_D)) {
        if (ch.category() != QChar::Mark_NonSpacing) {
            out.append(ch.toUpper());
        }
    }
    return out;
}

QVariantList Book::functions() const
{
    QVariantList out;
    for (const FunctionInfo &f : Formula::functions()) {
        out.append(QVariantMap{{QStringLiteral("name"), f.name}, {QStringLiteral("spanish"), f.spanish}, {QStringLiteral("signature"), f.signature},
                               {QStringLiteral("help"), f.help}});
    }
    return out;
}

QVariantList Book::completions(const QString &prefix) const
{
    QVariantList out;
    const QString wanted = folded(prefix);
    if (wanted.isEmpty()) {
        return out;
    }
    QList<QPair<QString, QVariantMap>> found;
    for (const FunctionInfo &f : Formula::functions()) {
        const bool spanish = !f.spanish.isEmpty() && folded(f.spanish).startsWith(wanted);
        const bool english = folded(f.name).startsWith(wanted);
        if (!spanish && !english) {
            continue;
        }
        // The Spanish name is the one shown, unless only the English one was being typed.
        const QString shown = spanish || f.spanish.isEmpty() ? (f.spanish.isEmpty() ? f.name : f.spanish) : f.name;
        const QString other = shown == f.name ? (f.spanish == f.name ? QString() : f.spanish) : f.name;
        found.append({shown, QVariantMap{{QStringLiteral("name"), shown}, {QStringLiteral("other"), other == shown ? QString() : other},
                                         {QStringLiteral("signature"), f.signature}, {QStringLiteral("help"), f.help}}});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
    });
    for (int i = 0; i < found.size() && i < 7; ++i) {
        out.append(found.at(i).second);
    }
    return out;
}

static bool nameChar(QChar ch)
{
    return ch.isLetterOrNumber() || ch == QLatin1Char('.') || ch == QLatin1Char('_');
}

QVariantMap Book::formulaContext(const QString &text, int cursor) const
{
    QVariantMap out;
    if (!text.startsWith(QLatin1Char('='))) {
        return out;
    }
    cursor = qBound(1, cursor, int(text.size()));
    // The calls the cursor is inside, innermost last.
    struct Call {
        QString name;
        int argument = 0;
    };
    QList<Call> calls;
    bool quoted = false;
    for (int i = 1; i < cursor; ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (quoted) {
            continue;
        } else if (ch == QLatin1Char('(')) {
            int start = i;
            while (start > 1 && nameChar(text.at(start - 1))) {
                --start;
            }
            calls.append(Call{text.mid(start, i - start), 0});
        } else if (ch == QLatin1Char(')')) {
            if (!calls.isEmpty()) {
                calls.removeLast();
            }
        } else if (!calls.isEmpty() && (ch == QLatin1Char(';') || (ch == QLatin1Char(',') && !(i > 0 && text.at(i - 1).isDigit() && i + 1 < text.size() && text.at(i + 1).isDigit())))) {
            ++calls.last().argument;
        }
    }
    if (quoted) {
        return out;
    }
    int start = cursor;
    while (start > 1 && nameChar(text.at(start - 1))) {
        --start;
    }
    const QString token = text.mid(start, cursor - start);
    const bool nameAfter = cursor < text.size() && nameChar(text.at(cursor));
    const QChar before = start > 0 ? text.at(start - 1) : QChar();
    if (!token.isEmpty() && token.at(0).isLetter() && !nameAfter && before != QLatin1Char('$') && before != QLatin1Char('!') && before != QLatin1Char('\'')) {
        out.insert(QStringLiteral("token"), token);
        out.insert(QStringLiteral("tokenStart"), start);
    }
    for (int i = calls.size() - 1; i >= 0; --i) {
        const QString canonical = Formula::canonicalFunction(calls.at(i).name);
        if (canonical.isEmpty()) {
            continue;
        }
        for (const FunctionInfo &f : Formula::functions()) {
            if (f.name == canonical) {
                out.insert(QStringLiteral("name"), calls.at(i).name.toUpper());
                out.insert(QStringLiteral("signature"), f.signature);
                out.insert(QStringLiteral("help"), f.help);
                out.insert(QStringLiteral("argument"), calls.at(i).argument);
                // The pieces of the signature, to light up the one being typed.
                QStringList parts;
                for (const QString &part : f.signature.split(QLatin1Char(';'))) {
                    parts.append(part.trimmed());
                }
                out.insert(QStringLiteral("parts"), parts);
                break;
            }
        }
        break;
    }
    return out;
}

QVariantList Book::references(const QString &text) const
{
    QVariantList out;
    if (!m_open || !text.startsWith(QLatin1Char('='))) {
        return out;
    }
    static const QRegularExpression pattern(QStringLiteral(
        "(?:(?:'([^']+)'|([\\p{L}_][\\p{L}\\p{N}_.]*))!)?"
        "(\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7}(?::\\$?[A-Za-z]{1,3}\\$?[0-9]{1,7})?|\\$?[A-Za-z]{1,3}:\\$?[A-Za-z]{1,3}|\\$?[0-9]{1,7}:\\$?[0-9]{1,7})"),
        QRegularExpression::UseUnicodePropertiesOption);
    // Where the strings are, to leave what looks like a reference inside them alone.
    QList<QPair<int, int>> strings;
    int open = -1;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('"')) {
            if (open < 0) {
                open = i;
            } else {
                strings.append({open, i});
                open = -1;
            }
        }
    }
    if (open >= 0) {
        strings.append({open, int(text.size())});
    }
    QStringList seen;
    auto match = pattern.globalMatch(text);
    while (match.hasNext()) {
        const QRegularExpressionMatch m = match.next();
        const int start = int(m.capturedStart());
        const int end = int(m.capturedEnd());
        bool skip = false;
        for (const auto &span : std::as_const(strings)) {
            skip = skip || (start >= span.first && start <= span.second);
        }
        if (skip || (start > 0 && nameChar(text.at(start - 1))) || (end < text.size() && (nameChar(text.at(end)) || text.at(end) == QLatin1Char('(')))) {
            continue;
        }
        QString cells = m.captured(3).toUpper();
        cells.remove(QLatin1Char('$'));
        int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
        if (!Formula::parseArea(cells, &r1, &c1, &r2, &c2)) {
            continue;
        }
        const QString sheetName = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        const QString key = sheetName.toUpper() + QLatin1Char('!') + cells;
        int index = seen.indexOf(key);
        if (index < 0) {
            index = seen.size();
            seen.append(key);
        }
        static const QStringList palette = {QStringLiteral("#0a84ff"), QStringLiteral("#ff453a"), QStringLiteral("#bf5af2"),
                                            QStringLiteral("#ff9f0a"), QStringLiteral("#30b0c7"), QStringLiteral("#ff375f")};
        const bool here = sheetName.isEmpty() || m_book.sheetIndex(sheetName) == m_sheet;
        out.append(QVariantMap{{QStringLiteral("start"), start}, {QStringLiteral("length"), end - start}, {QStringLiteral("color"), palette.at(index % palette.size())},
                               {QStringLiteral("here"), here}, {QStringLiteral("r1"), r1}, {QStringLiteral("c1"), c1},
                               {QStringLiteral("r2"), qMin(r2, m_rows - 1)}, {QStringLiteral("c2"), qMin(c2, m_cols - 1)}});
    }
    return out;
}

// --- History ----------------------------------------------------------------------

void Book::undo()
{
    m_book.undo();
    touch();
}

void Book::redo()
{
    m_book.redo();
    touch();
}

void Book::beginGroup()
{
    m_book.beginGroup();
}

void Book::endGroup()
{
    m_book.endGroup();
    touch();
}
