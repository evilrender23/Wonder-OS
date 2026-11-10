#include "workbook.h"

#include "csv.h"
#include "eval_p.h"
#include "numfmt.h"

#include <QCollator>
#include <QDateTime>

#include <algorithm>
#include <cmath>

size_t qHash(const Style &s, size_t seed)
{
    return qHashMulti(seed, s.format, s.bold, s.italic, s.underline, s.strike, s.color, s.fill, s.align, s.valign, s.wrap, s.borders, s.borderColor, s.size);
}

namespace
{
const Cell blankCell;

/// Moves an area for rows or columns inserted or deleted; false when all of it went.
bool adjustSpan(int &first, int &last, bool inserting, int at, int count, int limit)
{
    if (inserting) {
        if (first >= at) {
            first += count;
        }
        if (last >= at) {
            last += count;
        }
        last = qMin(last, limit - 1);
        return first < limit;
    }
    const int end = at + count;
    if (first >= at && last < end) {
        return false;
    }
    if (first >= end) {
        first -= count;
    } else if (first >= at) {
        first = at;
    }
    if (last >= end) {
        last -= count;
    } else if (last >= at) {
        last = at - 1;
    }
    return true;
}

bool adjustArea(Area &area, const SheetEdit &edit)
{
    const bool rows = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::DeleteRows;
    const bool inserting = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::InsertCols;
    return rows ? adjustSpan(area.r1, area.r2, inserting, edit.at, edit.count, Limits::Rows)
                : adjustSpan(area.c1, area.c2, inserting, edit.at, edit.count, Limits::Cols);
}

template<typename T>
QMap<int, T> shiftedKeys(const QMap<int, T> &map, bool inserting, int at, int count, int limit)
{
    QMap<int, T> out;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        int k = it.key();
        if (inserting) {
            if (k >= at) {
                k += count;
            }
            if (k >= limit) {
                continue;
            }
        } else {
            if (k >= at && k < at + count) {
                continue;
            }
            if (k >= at + count) {
                k -= count;
            }
        }
        out.insert(k, it.value());
    }
    return out;
}

QSet<int> shiftedSet(const QSet<int> &set, bool inserting, int at, int count, int limit)
{
    QSet<int> out;
    for (int k : set) {
        if (inserting) {
            if (k >= at) {
                k += count;
            }
            if (k >= limit) {
                continue;
            }
        } else {
            if (k >= at && k < at + count) {
                continue;
            }
            if (k >= at + count) {
                k -= count;
            }
        }
        out.insert(k);
    }
    return out;
}

/// Sorting order of values: numbers, then text, then true/false, errors, and blanks last.
int sortRank(const Value &v)
{
    switch (v.type) {
    case Value::Number:
        return 0;
    case Value::Text:
        return 1;
    case Value::Bool:
        return 2;
    case Value::Error:
        return 3;
    case Value::Empty:
        break;
    }
    return 4;
}

bool textMatches(const QString &subject, const QString &text, bool matchCase, bool whole)
{
    const Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    return whole ? subject.compare(text, cs) == 0 : subject.contains(text, cs);
}
}

Workbook::Workbook()
{
    clear();
}

void Workbook::clear()
{
    m_sheets.clear();
    m_styles = {Style()};
    m_styleIds.clear();
    m_styleIds.insert(Style(), 0);
    m_undo.clear();
    m_redo.clear();
    m_group = 0;
    m_groupSaved = false;
    m_nextChart = 1;
    Sheet first;
    first.name = QStringLiteral("Hoja 1");
    m_sheets.append(first);
    rebuildGraph(false);
    m_modified = false;
    m_used.clear();
    ++m_revision;
}

// --- History --------------------------------------------------------------------

void Workbook::checkpoint()
{
    if (m_group > 0) {
        if (m_groupSaved) {
            return;
        }
        m_groupSaved = true;
    }
    m_undo.append(m_sheets);
    if (m_undo.size() > 100) {
        m_undo.removeFirst();
    }
    m_redo.clear();
}

void Workbook::changed()
{
    m_modified = true;
    ++m_revision;
    m_used.clear();
    if (m_group == 0) {
        recalc(true);
    }
}

void Workbook::beginGroup()
{
    if (m_group++ == 0) {
        m_groupSaved = false;
    }
}

void Workbook::endGroup()
{
    if (m_group > 0 && --m_group == 0) {
        if (m_groupSaved) {
            recalc(true);
        }
        m_groupSaved = false;
    }
}

void Workbook::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    m_redo.append(m_sheets);
    m_sheets = m_undo.takeLast();
    rebuildGraph(false);
    m_modified = true;
    ++m_revision;
    m_used.clear();
    recalc(true);
}

void Workbook::redo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    m_undo.append(m_sheets);
    m_sheets = m_redo.takeLast();
    rebuildGraph(false);
    m_modified = true;
    ++m_revision;
    m_used.clear();
    recalc(true);
}

// --- Sheets ---------------------------------------------------------------------

int Workbook::sheetIndex(const QString &name) const
{
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (m_sheets.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QString Workbook::uniqueSheetName(const QString &wanted) const
{
    if (!wanted.isEmpty() && sheetIndex(wanted) < 0) {
        return wanted;
    }
    for (int n = wanted.isEmpty() ? m_sheets.size() + 1 : 2;; ++n) {
        const QString name = wanted.isEmpty() ? QStringLiteral("Hoja %1").arg(n) : QStringLiteral("%1 (%2)").arg(wanted).arg(n);
        if (sheetIndex(name) < 0) {
            return name;
        }
    }
}

int Workbook::addSheet(const QString &name, int at)
{
    checkpoint();
    Sheet sheet;
    sheet.name = uniqueSheetName(name.left(31));
    const int index = at < 0 || at > m_sheets.size() ? m_sheets.size() : at;
    m_sheets.insert(index, sheet);
    rebuildGraph(true);
    changed();
    return index;
}

bool Workbook::removeSheet(int index)
{
    if (m_sheets.size() < 2 || index < 0 || index >= m_sheets.size()) {
        return false;
    }
    checkpoint();
    m_sheets.removeAt(index);
    rebuildGraph(true);
    changed();
    return true;
}

bool Workbook::renameSheet(int index, const QString &wanted)
{
    const QString name = wanted.trimmed();
    static const QString forbidden = QStringLiteral("[]:*?/\\");
    if (index < 0 || index >= m_sheets.size() || name.isEmpty() || name.size() > 31) {
        return false;
    }
    for (const QChar c : name) {
        if (forbidden.contains(c)) {
            return false;
        }
    }
    const int taken = sheetIndex(name);
    if (taken >= 0 && taken != index) {
        return false;
    }
    const QString old = m_sheets.at(index).name;
    if (old == name) {
        return true;
    }
    checkpoint();
    m_sheets[index].name = name;
    for (Sheet &sheet : m_sheets) {
        for (auto row = sheet.rows.begin(); row != sheet.rows.end(); ++row) {
            for (auto it = row->begin(); it != row->end(); ++it) {
                if (it->formula) {
                    it->formula = Formula::sheetRenamed(it->formula, old, name);
                }
            }
        }
    }
    rebuildGraph(true);
    changed();
    return true;
}

bool Workbook::moveSheet(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_sheets.size() || to >= m_sheets.size() || from == to) {
        return false;
    }
    checkpoint();
    m_sheets.move(from, to);
    rebuildGraph(true);
    changed();
    return true;
}

int Workbook::duplicateSheet(int index)
{
    if (index < 0 || index >= m_sheets.size()) {
        return -1;
    }
    checkpoint();
    Sheet copy = m_sheets.at(index);
    copy.name = uniqueSheetName(copy.name.left(27));
    for (Chart &chart : copy.charts) {
        chart.id = m_nextChart++;
    }
    m_sheets.insert(index + 1, copy);
    rebuildGraph(true);
    changed();
    return index + 1;
}

// --- Cells ----------------------------------------------------------------------

const Cell *Workbook::cell(int sheet, int row, int col) const
{
    if (sheet < 0 || sheet >= m_sheets.size()) {
        return nullptr;
    }
    const auto &rows = m_sheets.at(sheet).rows;
    const auto r = rows.constFind(row);
    if (r == rows.constEnd()) {
        return nullptr;
    }
    const auto c = r->constFind(col);
    return c == r->constEnd() ? nullptr : &*c;
}

Cell &Workbook::cellRef(int sheet, int row, int col)
{
    return m_sheets[sheet].rows[row][col];
}

void Workbook::dropIfBlank(int sheet, int row, int col)
{
    auto &rows = m_sheets[sheet].rows;
    const auto r = rows.find(row);
    if (r == rows.end()) {
        return;
    }
    const auto c = r->find(col);
    if (c != r->end() && c->blank()) {
        r->erase(c);
    }
    if (r->isEmpty()) {
        rows.erase(r);
    }
}

Value Workbook::value(int sheet, int row, int col)
{
    return cellValue(sheet, row, col);
}

QString Workbook::display(int sheet, int row, int col, QString *color)
{
    const Cell *c = cell(sheet, row, col);
    if (!c) {
        return {};
    }
    const QString code = m_styles.at(c->style).format;
    const Value v = c->formula ? cellValue(sheet, row, col) : c->value;
    return NumFmt::format(v, code, color);
}

QString Workbook::input(int sheet, int row, int col) const
{
    const Cell *c = cell(sheet, row, col);
    if (!c) {
        return {};
    }
    if (c->formula) {
        return QLatin1Char('=') + Formula::print(c->formula, true);
    }
    if (c->value.isText()) {
        // Text that would be read as something else keeps its apostrophe.
        const QString &text = c->value.text;
        if (!NumFmt::isText(m_styles.at(c->style).format) && (text.startsWith(QLatin1Char('=')) || text.startsWith(QLatin1Char('\'')) || !NumFmt::parseInput(text).isText())) {
            return QLatin1Char('\'') + text;
        }
        return text;
    }
    return NumFmt::editText(c->value, m_styles.at(c->style).format);
}

QString Workbook::formula(int sheet, int row, int col) const
{
    const Cell *c = cell(sheet, row, col);
    return c && c->formula ? Formula::print(c->formula, false) : QString();
}

void Workbook::setFormula(int sheet, int row, int col, const NodePtr &formula)
{
    const quint64 k = key(sheet, row, col);
    detach(k);
    cellRef(sheet, row, col).formula = formula;
    if (formula) {
        attach(k, formula);
    }
    touch(k);
}

void Workbook::setCell(int sheet, int row, int col, const Cell &cell)
{
    const quint64 k = key(sheet, row, col);
    detach(k);
    if (cell.blank()) {
        if (this->cell(sheet, row, col)) {
            cellRef(sheet, row, col) = cell;
            dropIfBlank(sheet, row, col);
        }
    } else {
        cellRef(sheet, row, col) = cell;
        if (cell.formula) {
            attach(k, cell.formula);
        }
    }
    touch(k);
}

QString Workbook::setInput(int sheet, int row, int col, const QString &typed)
{
    if (sheet < 0 || sheet >= m_sheets.size() || row < 0 || col < 0 || row >= Limits::Rows || col >= Limits::Cols) {
        return QStringLiteral("Esa celda no existe.");
    }
    Area merged;
    if (mergeAt(sheet, row, col, &merged)) {
        row = merged.r1;
        col = merged.c1;
    }
    const Cell *old = cell(sheet, row, col);
    Cell next;
    next.style = old ? old->style : 0;
    Style style = m_styles.at(next.style);
    if (typed.isEmpty()) {
        // Nothing: only the format stays.
    } else if (typed.startsWith(QLatin1Char('=')) && typed.size() > 1) {
        QString error;
        next.formula = Formula::parse(typed.mid(1), &error);
        if (!next.formula) {
            return error;
        }
        if (style.format.isEmpty()) {
            // A formula that gives a date shows as one.
            const Node *root = next.formula.get();
            while (root->kind == Node::Paren) {
                root = root->args.at(0).get();
            }
            if (root->kind == Node::Func) {
                static const QStringList dates = {QStringLiteral("TODAY"), QStringLiteral("DATE"), QStringLiteral("EDATE"), QStringLiteral("EOMONTH"),
                                                  QStringLiteral("WORKDAY"), QStringLiteral("DATEVALUE")};
                if (dates.contains(root->text)) {
                    style.format = QStringLiteral("dd/mm/yyyy");
                } else if (root->text == QLatin1String("NOW")) {
                    style.format = QStringLiteral("dd/mm/yyyy hh:mm");
                } else if (root->text == QLatin1String("TIME")) {
                    style.format = QStringLiteral("hh:mm");
                }
            }
        }
    } else if (typed.startsWith(QLatin1Char('\''))) {
        next.value = Value(typed.mid(1));
    } else if (NumFmt::isText(style.format)) {
        next.value = Value(typed);
    } else {
        QString code;
        next.value = NumFmt::parseInput(typed, &code);
        if (!code.isEmpty() && (style.format.isEmpty() || (NumFmt::isDate(code) != NumFmt::isDate(style.format) && NumFmt::family(style.format) != QLatin1String("number")
                                                            && NumFmt::family(style.format) != QLatin1String("currency")))) {
            style.format = code;
        }
    }
    if (!next.formula && !typed.isEmpty()) {
        const QString refused = checkValidation(sheet, row, col, typed, next.value);
        if (!refused.isEmpty()) {
            return refused;
        }
    }
    next.style = styleId(style);
    checkpoint();
    setCell(sheet, row, col, next);
    changed();
    return {};
}

void Workbook::loadCell(int sheet, int row, int col, const Value &value, const QString &formula, int style)
{
    if (sheet < 0 || sheet >= m_sheets.size() || row < 0 || col < 0 || row >= Limits::Rows || col >= Limits::Cols) {
        return;
    }
    Cell c;
    c.value = value;
    c.style = style >= 0 && style < m_styles.size() ? style : 0;
    if (!formula.isEmpty()) {
        c.formula = Formula::parse(formula);
        if (!c.formula && value.isEmpty()) {
            // Not understood and nothing cached: keep what was written, as text.
            c.value = Value(QLatin1Char('=') + formula);
        }
    }
    if (!c.blank()) {
        m_sheets[sheet].rows[row][col] = c;
    }
}

void Workbook::finishLoading()
{
    // What other programs calculated stands where a function is not known here.
    QHash<quint64, Value> cached;
    for (int s = 0; s < m_sheets.size(); ++s) {
        const auto &rows = m_sheets.at(s).rows;
        for (auto row = rows.constBegin(); row != rows.constEnd(); ++row) {
            for (auto it = row->constBegin(); it != row->constEnd(); ++it) {
                if (it->formula && !it->value.isEmpty()) {
                    cached.insert(key(s, row.key(), it.key()), it->value);
                }
            }
        }
    }
    for (Sheet &sheet : m_sheets) {
        for (const Chart &chart : std::as_const(sheet.charts)) {
            m_nextChart = qMax(m_nextChart, chart.id + 1);
        }
    }
    m_used.clear();
    rebuildGraph(true);
    recalc(false);
    for (auto it = cached.constBegin(); it != cached.constEnd(); ++it) {
        const int s = keySheet(it.key()), r = keyRow(it.key()), c = keyCol(it.key());
        const Cell *now = cell(s, r, c);
        if (now && now->value.isError() && now->value.err == Err::Name && !(it.value().isError() && it.value().err == Err::Name)) {
            cellRef(s, r, c).value = it.value();
        }
    }
    for (int s = 0; s < m_sheets.size(); ++s) {
        if (m_sheets.at(s).hasFilter) {
            applyFilter(s);
        }
    }
    m_undo.clear();
    m_redo.clear();
    m_modified = false;
    ++m_revision;
}

// --- The graph --------------------------------------------------------------------

void Workbook::attach(quint64 cellKey, const NodePtr &formula)
{
    EvalContext context{this, keySheet(cellKey), keyRow(cellKey), keyCol(cellKey)};
    QList<RangeRef> ranges;
    Formula::precedents(formula, context, &ranges);
    for (const RangeRef &range : std::as_const(ranges)) {
        if (range.single()) {
            m_singles[key(range.sheet, range.r1, range.c1)].append(cellKey);
        } else if ((range.r2 >> 5) - (range.r1 >> 5) < 128) {
            for (int block = range.r1 >> 5; block <= range.r2 >> 5; ++block) {
                m_blocks[key(range.sheet, block, 0)].append({range, cellKey});
            }
        } else {
            m_big.append({range, cellKey});
        }
    }
    if (!ranges.isEmpty()) {
        m_precedents.insert(cellKey, ranges);
    }
    if (Formula::isVolatile(formula)) {
        m_volatile.insert(cellKey);
    }
}

void Workbook::detach(quint64 cellKey)
{
    m_volatile.remove(cellKey);
    m_dirty.remove(cellKey);
    const auto found = m_precedents.constFind(cellKey);
    if (found == m_precedents.constEnd()) {
        return;
    }
    const auto same = [cellKey](const RangeDep &dep) {
        return dep.cell == cellKey;
    };
    for (const RangeRef &range : found.value()) {
        if (range.single()) {
            const auto it = m_singles.find(key(range.sheet, range.r1, range.c1));
            if (it != m_singles.end()) {
                it->removeOne(cellKey);
                if (it->isEmpty()) {
                    m_singles.erase(it);
                }
            }
        } else if ((range.r2 >> 5) - (range.r1 >> 5) < 128) {
            for (int block = range.r1 >> 5; block <= range.r2 >> 5; ++block) {
                const auto it = m_blocks.find(key(range.sheet, block, 0));
                if (it != m_blocks.end()) {
                    it->removeIf(same);
                    if (it->isEmpty()) {
                        m_blocks.erase(it);
                    }
                }
            }
        } else {
            m_big.removeIf(same);
        }
    }
    m_precedents.remove(cellKey);
}

void Workbook::touch(quint64 start)
{
    QList<quint64> queue{start};
    const Cell *own = cell(keySheet(start), keyRow(start), keyCol(start));
    if (own && own->formula) {
        m_dirty.insert(start);
    }
    while (!queue.isEmpty()) {
        const quint64 k = queue.takeLast();
        const int s = keySheet(k), r = keyRow(k), c = keyCol(k);
        const auto add = [this, &queue](quint64 dependent) {
            if (!m_dirty.contains(dependent)) {
                m_dirty.insert(dependent);
                queue.append(dependent);
            }
        };
        const auto singles = m_singles.constFind(k);
        if (singles != m_singles.constEnd()) {
            for (quint64 dependent : singles.value()) {
                add(dependent);
            }
        }
        const auto block = m_blocks.constFind(key(s, r >> 5, 0));
        if (block != m_blocks.constEnd()) {
            for (const RangeDep &dep : block.value()) {
                if (dep.range.contains(r, c)) {
                    add(dep.cell);
                }
            }
        }
        for (const RangeDep &dep : std::as_const(m_big)) {
            if (dep.range.sheet == s && dep.range.contains(r, c)) {
                add(dep.cell);
            }
        }
    }
}

void Workbook::rebuildGraph(bool dirtyAll)
{
    m_dirty.clear();
    m_visiting.clear();
    m_volatile.clear();
    m_precedents.clear();
    m_singles.clear();
    m_blocks.clear();
    m_big.clear();
    m_ruleCache.clear();
    for (int s = 0; s < m_sheets.size(); ++s) {
        const auto &rows = m_sheets.at(s).rows;
        for (auto row = rows.constBegin(); row != rows.constEnd(); ++row) {
            for (auto it = row->constBegin(); it != row->constEnd(); ++it) {
                if (it->formula) {
                    const quint64 k = key(s, row.key(), it.key());
                    attach(k, it->formula);
                    if (dirtyAll) {
                        m_dirty.insert(k);
                    }
                }
            }
        }
    }
}

Value Workbook::evaluateCell(quint64 cellKey, const NodePtr &formula)
{
    EvalContext context{this, keySheet(cellKey), keyRow(cellKey), keyCol(cellKey)};
    return Formula::evaluate(formula, context);
}

void Workbook::compute(quint64 start)
{
    struct Frame {
        quint64 k;
        bool expanded;
    };
    QList<Frame> stack{{start, false}};
    while (!stack.isEmpty()) {
        const Frame frame = stack.last();
        const int s = keySheet(frame.k), r = keyRow(frame.k), c = keyCol(frame.k);
        if (!m_dirty.contains(frame.k)) {
            if (frame.expanded) {
                m_visiting.remove(frame.k);
            }
            stack.removeLast();
            continue;
        }
        const Cell *own = cell(s, r, c);
        if (!own || !own->formula) {
            m_dirty.remove(frame.k);
            stack.removeLast();
            continue;
        }
        const NodePtr formula = own->formula;
        if (!frame.expanded) {
            stack.last().expanded = true;
            m_visiting.insert(frame.k);
            // The cells it reads that are still waiting go first.
            QList<quint64> pending;
            QSet<quint64> loops;
            const auto consider = [&](quint64 p) {
                if (m_visiting.contains(p)) {
                    loops.insert(p);
                } else {
                    pending.append(p);
                }
            };
            const auto precedents = m_precedents.value(frame.k);
            for (const RangeRef &range : precedents) {
                if (range.sheet < 0 || range.sheet >= m_sheets.size()) {
                    continue;
                }
                if (range.single()) {
                    const quint64 p = key(range.sheet, range.r1, range.c1);
                    if (m_dirty.contains(p)) {
                        consider(p);
                    }
                    continue;
                }
                const auto &rows = m_sheets.at(range.sheet).rows;
                for (auto row = rows.lowerBound(range.r1); row != rows.constEnd() && row.key() <= range.r2; ++row) {
                    for (auto it = row->lowerBound(range.c1); it != row->constEnd() && it.key() <= range.c2; ++it) {
                        if (it->formula) {
                            const quint64 p = key(range.sheet, row.key(), it.key());
                            if (m_dirty.contains(p)) {
                                consider(p);
                            }
                        }
                    }
                }
            }
            if (!loops.isEmpty()) {
                // Everything from the cell it comes back to up to here goes round in a circle.
                int from = stack.size() - 1;
                for (int i = 0; i < stack.size(); ++i) {
                    if (stack.at(i).expanded && loops.contains(stack.at(i).k)) {
                        from = i;
                        break;
                    }
                }
                for (int i = from; i < stack.size(); ++i) {
                    if (!stack.at(i).expanded) {
                        continue;
                    }
                    const quint64 k = stack.at(i).k;
                    cellRef(keySheet(k), keyRow(k), keyCol(k)).value = Value(Err::Cycle);
                    m_dirty.remove(k);
                    m_visiting.remove(k);
                }
                continue;
            }
            if (!pending.isEmpty()) {
                for (quint64 p : std::as_const(pending)) {
                    stack.append({p, false});
                }
                continue;
            }
        }
        const Value result = evaluateCell(frame.k, formula);
        if (m_dirty.contains(frame.k)) {
            cellRef(s, r, c).value = result;
            m_dirty.remove(frame.k);
        }
        m_visiting.remove(frame.k);
        stack.removeLast();
    }
}

void Workbook::recalc(bool volatiles)
{
    if (volatiles) {
        const QList<quint64> again(m_volatile.constBegin(), m_volatile.constEnd());
        for (quint64 k : again) {
            touch(k);
        }
    }
    if (m_dirty.isEmpty()) {
        return;
    }
    const QList<quint64> todo(m_dirty.constBegin(), m_dirty.constEnd());
    for (quint64 k : todo) {
        if (m_dirty.contains(k)) {
            compute(k);
        }
    }
    ++m_revision;
}

void Workbook::recalcAll()
{
    rebuildGraph(true);
    recalc(false);
}

Value Workbook::cellValue(int sheet, int row, int col)
{
    const Cell *c = cell(sheet, row, col);
    if (!c) {
        return {};
    }
    if (c->formula) {
        const quint64 k = key(sheet, row, col);
        if (m_dirty.contains(k)) {
            if (m_visiting.contains(k)) {
                return Value(Err::Cycle);
            }
            // Reached through OFFSET or INDIRECT, which the graph cannot see coming.
            const NodePtr formula = c->formula;
            m_visiting.insert(k);
            const Value result = evaluateCell(k, formula);
            m_visiting.remove(k);
            m_dirty.remove(k);
            cellRef(sheet, row, col).value = result;
            return result;
        }
    }
    return c->value;
}

void Workbook::forEachCell(const RangeRef &range, const std::function<void(int, int, const Value &)> &visit)
{
    if (range.sheet < 0 || range.sheet >= m_sheets.size()) {
        return;
    }
    // Copies share the data and stay whole if a cell gets calculated on the way.
    const QMap<int, QMap<int, Cell>> rows = m_sheets.at(range.sheet).rows;
    for (auto row = rows.lowerBound(range.r1); row != rows.constEnd() && row.key() <= range.r2; ++row) {
        const QMap<int, Cell> cells = row.value();
        for (auto it = cells.lowerBound(range.c1); it != cells.constEnd() && it.key() <= range.c2; ++it) {
            if (it->formula) {
                const Value v = cellValue(range.sheet, row.key(), it.key());
                if (!v.isEmpty()) {
                    visit(row.key(), it.key(), v);
                }
            } else if (!it->value.isEmpty()) {
                visit(row.key(), it.key(), it->value);
            }
        }
    }
}

double Workbook::now() const
{
    if (!std::isnan(m_now)) {
        return m_now;
    }
    return NumFmt::serial(QDateTime::currentDateTime());
}

void Workbook::used(int sheet, int *rows, int *cols) const
{
    *rows = 0;
    *cols = 0;
    if (sheet < 0 || sheet >= m_sheets.size()) {
        return;
    }
    const auto cachedIt = m_used.constFind(sheet);
    if (cachedIt != m_used.constEnd()) {
        *rows = cachedIt->first;
        *cols = cachedIt->second;
        return;
    }
    const auto &map = m_sheets.at(sheet).rows;
    if (!map.isEmpty()) {
        *rows = map.lastKey() + 1;
        for (auto row = map.constBegin(); row != map.constEnd(); ++row) {
            if (!row->isEmpty()) {
                *cols = qMax(*cols, row->lastKey() + 1);
            }
        }
    }
    m_used.insert(sheet, qMakePair(*rows, *cols));
}

Area Workbook::clipped(int sheet, const Area &area) const
{
    Area out = area;
    int rows = 0, cols = 0;
    used(sheet, &rows, &cols);
    if (out.rows() > 5000) {
        out.r2 = qMin(out.r2, qMax(rows - 1, out.r1 + 199));
    }
    if (out.cols() > 500) {
        out.c2 = qMin(out.c2, qMax(cols - 1, out.c1 + 49));
    }
    return out;
}

Area Workbook::region(int sheet, int row, int col) const
{
    Area area{row, col, row, col};
    const auto filled = [this, sheet](int r1, int c1, int r2, int c2) {
        r1 = qMax(r1, 0);
        c1 = qMax(c1, 0);
        r2 = qMin(r2, Limits::Rows - 1);
        c2 = qMin(c2, Limits::Cols - 1);
        if (r1 > r2 || c1 > c2) {
            return false;
        }
        const auto &rows = m_sheets.at(sheet).rows;
        for (auto r = rows.lowerBound(r1); r != rows.constEnd() && r.key() <= r2; ++r) {
            for (auto c = r->lowerBound(c1); c != r->constEnd() && c.key() <= c2; ++c) {
                if (!c->value.isEmpty() || c->formula) {
                    return true;
                }
            }
        }
        return false;
    };
    bool grew = true;
    while (grew) {
        grew = false;
        if (filled(area.r1 - 1, area.c1 - 1, area.r1 - 1, area.c2 + 1)) {
            --area.r1;
            grew = true;
        }
        if (filled(area.r2 + 1, area.c1 - 1, area.r2 + 1, area.c2 + 1)) {
            ++area.r2;
            grew = true;
        }
        if (filled(area.r1 - 1, area.c1 - 1, area.r2 + 1, area.c1 - 1)) {
            --area.c1;
            grew = true;
        }
        if (filled(area.r1 - 1, area.c2 + 1, area.r2 + 1, area.c2 + 1)) {
            ++area.c2;
            grew = true;
        }
    }
    area.r1 = qMax(area.r1, 0);
    area.c1 = qMax(area.c1, 0);
    return area;
}

// --- Styles ---------------------------------------------------------------------

int Workbook::styleId(const Style &style)
{
    const auto found = m_styleIds.constFind(style);
    if (found != m_styleIds.constEnd()) {
        return found.value();
    }
    m_styles.append(style);
    m_styleIds.insert(style, m_styles.size() - 1);
    return m_styles.size() - 1;
}

const Style &Workbook::cellStyle(int sheet, int row, int col) const
{
    const Cell *c = cell(sheet, row, col);
    return m_styles.at(c ? c->style : 0);
}

Style Workbook::effectiveStyle(int sheet, int row, int col)
{
    Style style = cellStyle(sheet, row, col);
    const QList<CondFormat> &rules = m_sheets.at(sheet).conditionals;
    if (rules.isEmpty()) {
        return style;
    }
    Value v;
    bool fetched = false;
    for (int i = 0; i < rules.size(); ++i) {
        const CondFormat &rule = rules.at(i);
        if (!rule.area.contains(row, col)) {
            continue;
        }
        if (!fetched) {
            v = cellValue(sheet, row, col);
            fetched = true;
        }
        if (v.isEmpty()) {
            continue;
        }
        bool on = false;
        if (rule.op == QLatin1String("between")) {
            on = v.isNumber() && Criterion(Value(QStringLiteral(">=") + rule.a)).matches(v) && Criterion(Value(QStringLiteral("<=") + rule.b)).matches(v);
        } else if (rule.op == QLatin1String("contains")) {
            on = Eval::text(v).contains(rule.a, Qt::CaseInsensitive);
        } else if (rule.op == QLatin1String("duplicate")) {
            RuleCache &cache = m_ruleCache[key(sheet, i, 0)];
            if (cache.revision != m_revision) {
                cache.revision = m_revision;
                cache.counts.clear();
                const Area area = clipped(sheet, rule.area);
                forEachCell(RangeRef{sheet, area.r1, area.c1, area.r2, area.c2}, [&cache](int, int, const Value &value) {
                    ++cache.counts[Eval::text(value).toLower()];
                });
            }
            on = cache.counts.value(Eval::text(v).toLower()) > 1;
        } else if (rule.op == QLatin1String("=") || rule.op == QLatin1String("<>") || rule.op == QLatin1String(">") || rule.op == QLatin1String("<")
                   || rule.op == QLatin1String(">=") || rule.op == QLatin1String("<=")) {
            const Criterion criterion(Value(rule.op + rule.a));
            on = criterion.matches(v) && (v.isText() || !std::isnan(NumFmt::parseNumber(rule.a)) || rule.op == QLatin1String("<>"));
        }
        if (on) {
            if (!rule.fill.isEmpty()) {
                style.fill = rule.fill;
            }
            if (!rule.color.isEmpty()) {
                style.color = rule.color;
            }
            style.bold = style.bold || rule.bold;
        }
    }
    return style;
}

void Workbook::changeStyle(int sheet, const Area &wanted, const std::function<void(Style &)> &change)
{
    const Area area = clipped(sheet, wanted);
    checkpoint();
    // The same change to the same style gives the same style: work it out once.
    QHash<int, int> mapped;
    for (int r = area.r1; r <= area.r2; ++r) {
        for (int c = area.c1; c <= area.c2; ++c) {
            const Cell *old = cell(sheet, r, c);
            const int before = old ? old->style : 0;
            auto it = mapped.find(before);
            if (it == mapped.end()) {
                Style style = m_styles.at(before);
                change(style);
                it = mapped.insert(before, styleId(style));
            }
            if (it.value() != before) {
                cellRef(sheet, r, c).style = it.value();
                dropIfBlank(sheet, r, c);
            }
        }
    }
    changed();
}

void Workbook::setBorders(int sheet, const Area &wanted, const QString &which, const QString &color)
{
    const Area area = clipped(sheet, wanted);
    checkpoint();
    for (int r = area.r1; r <= area.r2; ++r) {
        for (int c = area.c1; c <= area.c2; ++c) {
            const Cell *old = cell(sheet, r, c);
            Style style = m_styles.at(old ? old->style : 0);
            int bits = style.borders;
            if (which == QLatin1String("none")) {
                bits = 0;
            } else if (which == QLatin1String("all")) {
                bits = 15;
            } else {
                if ((which == QLatin1String("outline") || which == QLatin1String("left")) && c == area.c1) {
                    bits |= 1;
                }
                if ((which == QLatin1String("outline") || which == QLatin1String("top")) && r == area.r1) {
                    bits |= 2;
                }
                if ((which == QLatin1String("outline") || which == QLatin1String("right")) && c == area.c2) {
                    bits |= 4;
                }
                if ((which == QLatin1String("outline") || which == QLatin1String("bottom")) && r == area.r2) {
                    bits |= 8;
                }
            }
            if (bits == style.borders && (color.isEmpty() || color == style.borderColor || bits == 0)) {
                continue;
            }
            style.borders = bits;
            style.borderColor = bits == 0 ? QString() : (color.isEmpty() ? style.borderColor : color);
            cellRef(sheet, r, c).style = styleId(style);
            dropIfBlank(sheet, r, c);
        }
    }
    changed();
}

// --- Areas ----------------------------------------------------------------------

void Workbook::clearArea(int sheet, const Area &area, bool contents, bool formats)
{
    checkpoint();
    // Only what is there: a whole column is cleared without walking a million rows.
    QList<QPair<int, int>> cells;
    const auto &rows = m_sheets.at(sheet).rows;
    for (auto row = rows.lowerBound(area.r1); row != rows.constEnd() && row.key() <= area.r2; ++row) {
        for (auto it = row->lowerBound(area.c1); it != row->constEnd() && it.key() <= area.c2; ++it) {
            cells.append(qMakePair(row.key(), it.key()));
        }
    }
    for (const auto &at : std::as_const(cells)) {
        Cell next = *cell(sheet, at.first, at.second);
        if (contents) {
            next.value = Value();
            next.formula.reset();
        }
        if (formats) {
            next.style = 0;
        }
        setCell(sheet, at.first, at.second, next);
    }
    changed();
}

Clip Workbook::copy(int sheet, const Area &wanted)
{
    const Area area = clipped(sheet, wanted);
    Clip clip;
    clip.sheet = sheet;
    clip.row = area.r1;
    clip.col = area.c1;
    clip.rows = area.rows();
    clip.cols = area.cols();
    clip.cells.reserve(qsizetype(clip.rows) * clip.cols);
    QStringList lines;
    for (int r = area.r1; r <= area.r2; ++r) {
        QStringList fields;
        for (int c = area.c1; c <= area.c2; ++c) {
            const Cell *source = cell(sheet, r, c);
            Cell copy = source ? *source : Cell();
            if (copy.formula) {
                copy.value = cellValue(sheet, r, c);
            }
            clip.cells.append(copy);
            QString text = display(sheet, r, c);
            if (text.contains(QLatin1Char('\t')) || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('"'))) {
                text.replace(QLatin1Char('"'), QLatin1String("\"\""));
                text = QLatin1Char('"') + text + QLatin1Char('"');
            }
            fields.append(text);
        }
        lines.append(fields.join(QLatin1Char('\t')));
    }
    clip.tsv = lines.join(QLatin1Char('\n'));
    return clip;
}

Area Workbook::paste(const Clip &clip, int sheet, const Area &target, const QString &what)
{
    if (clip.isEmpty()) {
        return target;
    }
    Area dest = target;
    const int rows = target.rows() % clip.rows == 0 && !clip.cut && target.rows() < 100000 ? target.rows() : clip.rows;
    const int cols = target.cols() % clip.cols == 0 && !clip.cut && target.cols() < 100000 ? target.cols() : clip.cols;
    dest.r2 = qMin(dest.r1 + rows - 1, Limits::Rows - 1);
    dest.c2 = qMin(dest.c1 + cols - 1, Limits::Cols - 1);
    const bool values = what == QLatin1String("values");
    const bool formats = what == QLatin1String("formats");
    checkpoint();
    if (clip.cut && clip.sheet < m_sheets.size()) {
        // What was cut leaves its place.
        for (int r = 0; r < clip.rows; ++r) {
            for (int c = 0; c < clip.cols; ++c) {
                setCell(clip.sheet, clip.row + r, clip.col + c, Cell());
            }
        }
    }
    for (int r = dest.r1; r <= dest.r2; ++r) {
        for (int c = dest.c1; c <= dest.c2; ++c) {
            const int sr = (r - dest.r1) % clip.rows;
            const int sc = (c - dest.c1) % clip.cols;
            const Cell &source = clip.cells.at(qsizetype(sr) * clip.cols + sc);
            const Cell *old = cell(sheet, r, c);
            Cell next = old ? *old : Cell();
            if (formats) {
                next.style = source.style;
            } else if (values) {
                next.value = source.value;
                next.formula.reset();
            } else {
                next = source;
                if (next.formula && !clip.cut) {
                    next.formula = Formula::shifted(next.formula, r - (clip.row + sr), c - (clip.col + sc));
                }
            }
            setCell(sheet, r, c, next);
        }
    }
    changed();
    return dest;
}

Area Workbook::pasteText(int sheet, int row, int col, const QString &text)
{
    QString body = text;
    if (body.endsWith(QLatin1Char('\n'))) {
        body.chop(1);
    }
    const QList<QStringList> rows = Csv::parse(body, QLatin1Char('\t'));
    Area dest{row, col, row, col};
    beginGroup();
    for (int r = 0; r < rows.size() && row + r < Limits::Rows; ++r) {
        for (int c = 0; c < rows.at(r).size() && col + c < Limits::Cols; ++c) {
            setInput(sheet, row + r, col + c, rows.at(r).at(c));
            dest.r2 = qMax(dest.r2, row + r);
            dest.c2 = qMax(dest.c2, col + c);
        }
    }
    endGroup();
    return dest;
}

void Workbook::fillDown(int sheet, const Area &wanted)
{
    const Area area = clipped(sheet, wanted);
    if (area.rows() < 2) {
        return;
    }
    checkpoint();
    for (int c = area.c1; c <= area.c2; ++c) {
        const Cell *first = cell(sheet, area.r1, c);
        const Cell source = first ? *first : Cell();
        for (int r = area.r1 + 1; r <= area.r2; ++r) {
            Cell next = source;
            if (next.formula) {
                next.formula = Formula::shifted(next.formula, r - area.r1, 0);
            }
            setCell(sheet, r, c, next);
        }
    }
    changed();
}

void Workbook::fillRight(int sheet, const Area &wanted)
{
    const Area area = clipped(sheet, wanted);
    if (area.cols() < 2) {
        return;
    }
    checkpoint();
    for (int r = area.r1; r <= area.r2; ++r) {
        const Cell *first = cell(sheet, r, area.c1);
        const Cell source = first ? *first : Cell();
        for (int c = area.c1 + 1; c <= area.c2; ++c) {
            Cell next = source;
            if (next.formula) {
                next.formula = Formula::shifted(next.formula, 0, c - area.c1);
            }
            setCell(sheet, r, c, next);
        }
    }
    changed();
}

void Workbook::fillSeries(int sheet, const Area &source, const Area &target)
{
    const bool down = target.r2 > source.r2;
    if (!down && target.c2 <= source.c2) {
        return;
    }
    checkpoint();
    const int lines = down ? source.cols() : source.rows();
    const int length = down ? source.rows() : source.cols();
    const int until = down ? target.r2 : target.c2;
    for (int line = 0; line < lines; ++line) {
        QList<Cell> seed;
        bool numeric = length >= 2;
        for (int i = 0; i < length; ++i) {
            const Cell *c = down ? cell(sheet, source.r1 + i, source.c1 + line) : cell(sheet, source.r1 + line, source.c1 + i);
            seed.append(c ? *c : Cell());
            numeric = numeric && c && !c->formula && c->value.isNumber();
        }
        const double step = numeric ? seed.at(length - 1).value.num - seed.at(length - 2).value.num : 0;
        const int start = (down ? source.r2 : source.c2) + 1;
        for (int at = start; at <= until; ++at) {
            const int n = at - start;
            Cell next = seed.at(n % length);
            if (numeric) {
                next = seed.at(length - 1);
                next.value = Value(seed.at(length - 1).value.num + step * (n + 1));
            } else if (next.formula) {
                const int shift = at - ((down ? source.r1 : source.c1) + n % length);
                next.formula = Formula::shifted(next.formula, down ? shift : 0, down ? 0 : shift);
            }
            if (down) {
                setCell(sheet, at, source.c1 + line, next);
            } else {
                setCell(sheet, source.r1 + line, at, next);
            }
        }
    }
    changed();
}

void Workbook::structural(int sheetIndex, const SheetEdit &edit)
{
    const bool rowsEdit = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::DeleteRows;
    const bool inserting = edit.kind == SheetEdit::InsertRows || edit.kind == SheetEdit::InsertCols;
    checkpoint();
    Sheet &sheet = m_sheets[sheetIndex];
    if (rowsEdit) {
        sheet.rows = shiftedKeys(sheet.rows, inserting, edit.at, edit.count, Limits::Rows);
        sheet.rowHeights = shiftedKeys(sheet.rowHeights, inserting, edit.at, edit.count, Limits::Rows);
        sheet.hiddenRows = shiftedSet(sheet.hiddenRows, inserting, edit.at, edit.count, Limits::Rows);
        sheet.filteredRows = shiftedSet(sheet.filteredRows, inserting, edit.at, edit.count, Limits::Rows);
        if (edit.at < sheet.freezeRows) {
            sheet.freezeRows = qMax(0, sheet.freezeRows + (inserting ? edit.count : -qMin(edit.count, sheet.freezeRows - edit.at)));
        }
    } else {
        for (auto row = sheet.rows.begin(); row != sheet.rows.end();) {
            *row = shiftedKeys(*row, inserting, edit.at, edit.count, Limits::Cols);
            row = row->isEmpty() ? sheet.rows.erase(row) : std::next(row);
        }
        sheet.colWidths = shiftedKeys(sheet.colWidths, inserting, edit.at, edit.count, Limits::Cols);
        sheet.hiddenCols = shiftedSet(sheet.hiddenCols, inserting, edit.at, edit.count, Limits::Cols);
        if (edit.at < sheet.freezeCols) {
            sheet.freezeCols = qMax(0, sheet.freezeCols + (inserting ? edit.count : -qMin(edit.count, sheet.freezeCols - edit.at)));
        }
    }
    // Areas follow, and the ones that went altogether are dropped.
    QList<Area> merges;
    for (Area area : std::as_const(sheet.merges)) {
        if (adjustArea(area, edit) && (area.rows() > 1 || area.cols() > 1)) {
            merges.append(area);
        }
    }
    sheet.merges = merges;
    QList<CondFormat> conditionals;
    for (CondFormat rule : std::as_const(sheet.conditionals)) {
        if (adjustArea(rule.area, edit)) {
            conditionals.append(rule);
        }
    }
    sheet.conditionals = conditionals;
    QList<Validation> validations;
    for (Validation rule : std::as_const(sheet.validations)) {
        if (adjustArea(rule.area, edit)) {
            validations.append(rule);
        }
    }
    sheet.validations = validations;
    QList<Table> tables;
    for (Table table : std::as_const(sheet.tables)) {
        if (adjustArea(table.area, edit) && table.area.rows() > 1) {
            tables.append(table);
        }
    }
    sheet.tables = tables;
    for (Chart &chart : sheet.charts) {
        adjustArea(chart.area, edit);
        Area anchor{chart.row, chart.col, chart.row, chart.col};
        if (adjustArea(anchor, edit)) {
            chart.row = anchor.r1;
            chart.col = anchor.c1;
        } else {
            chart.row = rowsEdit ? edit.at : chart.row;
            chart.col = rowsEdit ? chart.col : edit.at;
        }
    }
    if (sheet.hasFilter && !adjustArea(sheet.filterArea, edit)) {
        sheet.hasFilter = false;
        sheet.filterAllowed.clear();
        sheet.filteredRows.clear();
    }
    if (sheet.hasFilter && !rowsEdit) {
        // Which column each choice belongs to moved with the columns.
        sheet.filterAllowed.clear();
        sheet.filteredRows.clear();
    }
    // Every formula that points here follows.
    for (int s = 0; s < m_sheets.size(); ++s) {
        const QString home = m_sheets.at(s).name;
        Sheet &other = m_sheets[s];
        for (auto row = other.rows.begin(); row != other.rows.end(); ++row) {
            for (auto it = row->begin(); it != row->end(); ++it) {
                if (it->formula) {
                    const NodePtr next = Formula::adjusted(it->formula, edit, home);
                    if (next != it->formula) {
                        it->formula = next;
                    }
                }
            }
        }
    }
    rebuildGraph(true);
    changed();
}

void Workbook::insertRows(int sheet, int at, int count)
{
    if (count > 0 && at >= 0 && at < Limits::Rows) {
        structural(sheet, SheetEdit{SheetEdit::InsertRows, m_sheets.at(sheet).name, at, count});
    }
}

void Workbook::deleteRows(int sheet, int at, int count)
{
    if (count > 0 && at >= 0 && at < Limits::Rows) {
        structural(sheet, SheetEdit{SheetEdit::DeleteRows, m_sheets.at(sheet).name, at, count});
    }
}

void Workbook::insertCols(int sheet, int at, int count)
{
    if (count > 0 && at >= 0 && at < Limits::Cols) {
        structural(sheet, SheetEdit{SheetEdit::InsertCols, m_sheets.at(sheet).name, at, count});
    }
}

void Workbook::deleteCols(int sheet, int at, int count)
{
    if (count > 0 && at >= 0 && at < Limits::Cols) {
        structural(sheet, SheetEdit{SheetEdit::DeleteCols, m_sheets.at(sheet).name, at, count});
    }
}

void Workbook::sort(int sheet, const Area &wanted, int column, bool ascending, bool header)
{
    Area area = clipped(sheet, wanted);
    int usedRows = 0, usedCols = 0;
    used(sheet, &usedRows, &usedCols);
    area.r2 = qMin(area.r2, qMax(usedRows - 1, area.r1));
    area.c2 = qMin(area.c2, qMax(usedCols - 1, area.c1));
    const int first = area.r1 + (header ? 1 : 0);
    if (first >= area.r2 || column < area.c1 || column > area.c2) {
        return;
    }
    struct Line {
        int row;
        Value key;
    };
    QList<Line> lines;
    for (int r = first; r <= area.r2; ++r) {
        lines.append({r, cellValue(sheet, r, column)});
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::stable_sort(lines.begin(), lines.end(), [&](const Line &x, const Line &y) {
        const int rx = sortRank(x.key), ry = sortRank(y.key);
        // Blanks stay at the bottom either way.
        if (rx == 4 || ry == 4) {
            return rx < ry;
        }
        int order = 0;
        if (rx != ry) {
            order = rx - ry;
        } else if (x.key.isText()) {
            order = collator.compare(x.key.text, y.key.text);
        } else if (x.key.isError()) {
            order = 0;
        } else {
            order = x.key.num < y.key.num ? -1 : (x.key.num > y.key.num ? 1 : 0);
        }
        return ascending ? order < 0 : order > 0;
    });
    checkpoint();
    QList<QList<Cell>> moved;
    for (const Line &line : std::as_const(lines)) {
        QList<Cell> cells;
        for (int c = area.c1; c <= area.c2; ++c) {
            const Cell *source = cell(sheet, line.row, c);
            cells.append(source ? *source : Cell());
        }
        moved.append(cells);
    }
    for (int i = 0; i < lines.size(); ++i) {
        const int dest = first + i;
        for (int c = area.c1; c <= area.c2; ++c) {
            Cell next = moved.at(i).at(c - area.c1);
            if (next.formula && dest != lines.at(i).row) {
                next.formula = Formula::shifted(next.formula, dest - lines.at(i).row, 0);
            }
            setCell(sheet, dest, c, next);
        }
    }
    changed();
}

bool Workbook::merge(int sheet, const Area &area)
{
    if (area.rows() == 1 && area.cols() == 1) {
        return false;
    }
    if (area.rows() > 5000 || area.cols() > 500) {
        return false;
    }
    checkpoint();
    Sheet &s = m_sheets[sheet];
    s.merges.removeIf([&area](const Area &other) {
        return other.intersects(area);
    });
    s.merges.append(area);
    // Only the corner keeps what it had.
    for (int r = area.r1; r <= area.r2; ++r) {
        for (int c = area.c1; c <= area.c2; ++c) {
            const Cell *old = cell(sheet, r, c);
            if ((r != area.r1 || c != area.c1) && old && (!old->value.isEmpty() || old->formula)) {
                Cell next = *old;
                next.value = Value();
                next.formula.reset();
                setCell(sheet, r, c, next);
            }
        }
    }
    changed();
    return true;
}

bool Workbook::unmerge(int sheet, const Area &area)
{
    bool any = false;
    for (const Area &other : m_sheets.at(sheet).merges) {
        any = any || other.intersects(area);
    }
    if (!any) {
        return false;
    }
    checkpoint();
    m_sheets[sheet].merges.removeIf([&area](const Area &other) {
        return other.intersects(area);
    });
    changed();
    return true;
}

bool Workbook::mergeAt(int sheet, int row, int col, Area *area) const
{
    for (const Area &merge : m_sheets.at(sheet).merges) {
        if (merge.contains(row, col)) {
            if (area) {
                *area = merge;
            }
            return true;
        }
    }
    return false;
}

// --- Layout ---------------------------------------------------------------------

double Workbook::colWidth(int sheet, int col) const
{
    return m_sheets.at(sheet).colWidths.value(col, DefaultColWidth);
}

double Workbook::rowHeight(int sheet, int row) const
{
    return m_sheets.at(sheet).rowHeights.value(row, DefaultRowHeight);
}

void Workbook::setColWidth(int sheet, int c1, int c2, double width)
{
    checkpoint();
    for (int c = c1; c <= c2 && c - c1 < 2000; ++c) {
        if (width <= 0 || qFuzzyCompare(width, DefaultColWidth)) {
            m_sheets[sheet].colWidths.remove(c);
        } else {
            m_sheets[sheet].colWidths.insert(c, qBound(8.0, width, 2000.0));
        }
    }
    changed();
}

void Workbook::setRowHeight(int sheet, int r1, int r2, double height)
{
    checkpoint();
    for (int r = r1; r <= r2 && r - r1 < 5000; ++r) {
        if (height <= 0 || qFuzzyCompare(height, DefaultRowHeight)) {
            m_sheets[sheet].rowHeights.remove(r);
        } else {
            m_sheets[sheet].rowHeights.insert(r, qBound(6.0, height, 1000.0));
        }
    }
    changed();
}

void Workbook::setColsHidden(int sheet, int c1, int c2, bool hidden)
{
    checkpoint();
    for (int c = c1; c <= c2 && c - c1 < 2000; ++c) {
        if (hidden) {
            m_sheets[sheet].hiddenCols.insert(c);
        } else {
            m_sheets[sheet].hiddenCols.remove(c);
        }
    }
    changed();
}

void Workbook::setRowsHidden(int sheet, int r1, int r2, bool hidden)
{
    checkpoint();
    for (int r = r1; r <= r2 && r - r1 < 20000; ++r) {
        if (hidden) {
            m_sheets[sheet].hiddenRows.insert(r);
        } else {
            m_sheets[sheet].hiddenRows.remove(r);
        }
    }
    recalc(true);
    changed();
}

bool Workbook::colHidden(int sheet, int col) const
{
    return m_sheets.at(sheet).hiddenCols.contains(col);
}

bool Workbook::rowHidden(int sheet, int row) const
{
    const Sheet &s = m_sheets.at(sheet);
    return s.hiddenRows.contains(row) || s.filteredRows.contains(row);
}

bool Workbook::rowOut(int sheet, int row, bool byHand) const
{
    if (sheet < 0 || sheet >= m_sheets.size()) {
        return false;
    }
    const Sheet &s = m_sheets.at(sheet);
    return s.filteredRows.contains(row) || (byHand && s.hiddenRows.contains(row));
}

void Workbook::setFreeze(int sheet, int rows, int cols)
{
    checkpoint();
    m_sheets[sheet].freezeRows = qBound(0, rows, 200);
    m_sheets[sheet].freezeCols = qBound(0, cols, 50);
    changed();
}

double Workbook::fittingWidth(int sheet, int col, const std::function<double(const QString &, const Style &)> &measure)
{
    double widest = 0;
    const QMap<int, QMap<int, Cell>> rows = m_sheets.at(sheet).rows;
    for (auto row = rows.constBegin(); row != rows.constEnd(); ++row) {
        const auto it = row->constFind(col);
        if (it == row->constEnd() || mergeAt(sheet, row.key(), col, nullptr)) {
            continue;
        }
        const QString text = display(sheet, row.key(), col);
        if (!text.isEmpty()) {
            widest = qMax(widest, measure(text, m_styles.at(it->style)));
        }
    }
    return widest;
}

// --- Data -----------------------------------------------------------------------

void Workbook::setFilter(int sheet, const Area &wanted)
{
    Area area = wanted.rows() == 1 && wanted.cols() == 1 ? region(sheet, wanted.r1, wanted.c1) : clipped(sheet, wanted);
    int usedRows = 0, usedCols = 0;
    used(sheet, &usedRows, &usedCols);
    area.r2 = qMin(area.r2, qMax(usedRows - 1, area.r1));
    area.c2 = qMin(area.c2, qMax(usedCols - 1, area.c1));
    checkpoint();
    Sheet &s = m_sheets[sheet];
    s.hasFilter = true;
    s.filterArea = area;
    s.filterAllowed.clear();
    s.filteredRows.clear();
    changed();
}

void Workbook::clearFilter(int sheet)
{
    if (!m_sheets.at(sheet).hasFilter) {
        return;
    }
    checkpoint();
    Sheet &s = m_sheets[sheet];
    s.hasFilter = false;
    s.filterAllowed.clear();
    s.filteredRows.clear();
    changed();
}

QStringList Workbook::filterValues(int sheet, int column)
{
    const Sheet &s = m_sheets.at(sheet);
    if (!s.hasFilter) {
        return {};
    }
    QSet<QString> seen;
    const int col = s.filterArea.c1 + column;
    const Area area = s.filterArea;
    for (int r = area.r1 + 1; r <= area.r2; ++r) {
        seen.insert(display(sheet, r, col));
    }
    QStringList values(seen.constBegin(), seen.constEnd());
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(values.begin(), values.end(), [&collator](const QString &x, const QString &y) {
        return collator.compare(x, y) < 0;
    });
    return values;
}

void Workbook::setFilterAllowed(int sheet, int column, const QStringList &allowed, bool all)
{
    if (!m_sheets.at(sheet).hasFilter) {
        return;
    }
    checkpoint();
    if (all) {
        m_sheets[sheet].filterAllowed.remove(column);
    } else {
        m_sheets[sheet].filterAllowed.insert(column, QSet<QString>(allowed.constBegin(), allowed.constEnd()));
    }
    applyFilter(sheet);
    // The totals that pass over what a filter leaves out go again.
    recalc(true);
    changed();
}

void Workbook::applyFilter(int sheet)
{
    const Area area = m_sheets.at(sheet).filterArea;
    const QMap<int, QSet<QString>> allowed = m_sheets.at(sheet).filterAllowed;
    QSet<int> hidden;
    if (!allowed.isEmpty()) {
        for (int r = area.r1 + 1; r <= area.r2; ++r) {
            for (auto it = allowed.constBegin(); it != allowed.constEnd(); ++it) {
                if (!it.value().contains(display(sheet, r, area.c1 + it.key()))) {
                    hidden.insert(r);
                    break;
                }
            }
        }
    }
    m_sheets[sheet].filteredRows = hidden;
}

bool Workbook::find(int sheet, const QString &text, bool matchCase, bool whole, bool forward, int *row, int *col)
{
    if (text.isEmpty()) {
        return false;
    }
    QList<QPair<int, int>> hits;
    const QMap<int, QMap<int, Cell>> rows = m_sheets.at(sheet).rows;
    for (auto r = rows.constBegin(); r != rows.constEnd(); ++r) {
        for (auto c = r->constBegin(); c != r->constEnd(); ++c) {
            if (c->value.isEmpty() && !c->formula) {
                continue;
            }
            if (textMatches(display(sheet, r.key(), c.key()), text, matchCase, whole)
                || (c->formula && textMatches(input(sheet, r.key(), c.key()), text, matchCase, whole))) {
                hits.append(qMakePair(r.key(), c.key()));
            }
        }
    }
    if (hits.isEmpty()) {
        return false;
    }
    const QPair<int, int> here(*row, *col);
    QPair<int, int> next = forward ? hits.first() : hits.last();
    if (forward) {
        for (const auto &hit : std::as_const(hits)) {
            if (hit > here) {
                next = hit;
                break;
            }
        }
    } else {
        for (int i = hits.size() - 1; i >= 0; --i) {
            if (hits.at(i) < here) {
                next = hits.at(i);
                break;
            }
        }
    }
    *row = next.first;
    *col = next.second;
    return true;
}

bool Workbook::replaceAt(int sheet, int row, int col, const QString &text, const QString &with, bool matchCase, bool whole)
{
    const Cell *c = cell(sheet, row, col);
    if (!c || text.isEmpty()) {
        return false;
    }
    const QString before = input(sheet, row, col);
    QString subject = before;
    if (!c->formula && subject.startsWith(QLatin1Char('\''))) {
        subject.remove(0, 1);
    }
    const Qt::CaseSensitivity cs = matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    QString after;
    if (whole) {
        if (subject.compare(text, cs) != 0) {
            return false;
        }
        after = with;
    } else {
        if (!subject.contains(text, cs)) {
            return false;
        }
        after = subject;
        after.replace(text, with, cs);
    }
    return setInput(sheet, row, col, after).isEmpty();
}

int Workbook::replaceAll(int sheet, const QString &text, const QString &with, bool matchCase, bool whole)
{
    if (text.isEmpty()) {
        return 0;
    }
    QList<QPair<int, int>> cells;
    const QMap<int, QMap<int, Cell>> rows = m_sheets.at(sheet).rows;
    for (auto r = rows.constBegin(); r != rows.constEnd(); ++r) {
        for (auto c = r->constBegin(); c != r->constEnd(); ++c) {
            if (!c->value.isEmpty() || c->formula) {
                cells.append(qMakePair(r.key(), c.key()));
            }
        }
    }
    int count = 0;
    beginGroup();
    for (const auto &at : std::as_const(cells)) {
        if (replaceAt(sheet, at.first, at.second, text, with, matchCase, whole)) {
            ++count;
        }
    }
    endGroup();
    return count;
}

void Workbook::addConditional(int sheet, const CondFormat &rule)
{
    checkpoint();
    m_sheets[sheet].conditionals.append(rule);
    m_ruleCache.clear();
    changed();
}

void Workbook::clearConditionals(int sheet, const Area &area)
{
    checkpoint();
    m_sheets[sheet].conditionals.removeIf([&area](const CondFormat &rule) {
        return rule.area.intersects(area);
    });
    m_ruleCache.clear();
    changed();
}

void Workbook::setValidation(int sheet, const Validation &rule)
{
    checkpoint();
    m_sheets[sheet].validations.removeIf([&rule](const Validation &other) {
        return other.area == rule.area;
    });
    m_sheets[sheet].validations.append(rule);
    changed();
}

void Workbook::clearValidation(int sheet, const Area &area)
{
    checkpoint();
    m_sheets[sheet].validations.removeIf([&area](const Validation &rule) {
        return rule.area.intersects(area);
    });
    changed();
}

const Validation *Workbook::validationAt(int sheet, int row, int col) const
{
    const QList<Validation> &rules = m_sheets.at(sheet).validations;
    for (int i = rules.size() - 1; i >= 0; --i) {
        if (rules.at(i).area.contains(row, col)) {
            return &rules.at(i);
        }
    }
    return nullptr;
}

QString Workbook::checkValidation(int sheet, int row, int col, const QString &typed, const Value &value) const
{
    const Validation *rule = validationAt(sheet, row, col);
    if (!rule) {
        return {};
    }
    const QString refusal = rule->message.isEmpty() ? QStringLiteral("Ese valor no cumple la validación de la celda.") : rule->message;
    if (rule->kind == QLatin1String("list")) {
        for (const QString &item : rule->items) {
            if (item.trimmed().compare(typed.trimmed(), Qt::CaseInsensitive) == 0) {
                return {};
            }
        }
        return rule->message.isEmpty() ? QStringLiteral("Elige uno de los valores de la lista: %1.").arg(rule->items.join(QStringLiteral(", "))) : rule->message;
    }
    if (rule->kind == QLatin1String("length")) {
        const double low = rule->min.isEmpty() ? 0 : NumFmt::parseNumber(rule->min);
        const double high = rule->max.isEmpty() ? 1e9 : NumFmt::parseNumber(rule->max);
        return typed.size() >= low && typed.size() <= high ? QString() : refusal;
    }
    if (!value.isNumber()) {
        return refusal;
    }
    if (rule->kind == QLatin1String("integer") && value.num != std::floor(value.num)) {
        return refusal;
    }
    const auto bound = [](const QString &text, double fallback) {
        if (text.trimmed().isEmpty()) {
            return fallback;
        }
        const Value parsed = NumFmt::parseInput(text);
        return parsed.isNumber() ? parsed.num : fallback;
    };
    if (value.num < bound(rule->min, -HUGE_VAL) || value.num > bound(rule->max, HUGE_VAL)) {
        return refusal;
    }
    return {};
}

SelectionStats Workbook::stats(int sheet, const Area &area)
{
    SelectionStats out;
    bool first = true;
    forEachCell(RangeRef{sheet, area.r1, area.c1, area.r2, area.c2}, [&](int, int, const Value &v) {
        ++out.count;
        if (v.isNumber()) {
            ++out.numbers;
            out.sum += v.num;
            out.min = first ? v.num : qMin(out.min, v.num);
            out.max = first ? v.num : qMax(out.max, v.num);
            first = false;
        }
    });
    out.average = out.numbers > 0 ? out.sum / out.numbers : 0;
    return out;
}

// --- Charts ---------------------------------------------------------------------

int Workbook::addChart(int sheet, const Chart &chart)
{
    checkpoint();
    Chart added = chart;
    added.id = m_nextChart++;
    m_sheets[sheet].charts.append(added);
    changed();
    return added.id;
}

void Workbook::updateChart(int sheet, const Chart &chart)
{
    QList<Chart> &charts = m_sheets[sheet].charts;
    for (int i = 0; i < charts.size(); ++i) {
        if (charts.at(i).id == chart.id) {
            if (charts.at(i) == chart) {
                return;
            }
            checkpoint();
            m_sheets[sheet].charts[i] = chart;
            changed();
            return;
        }
    }
}

void Workbook::removeChart(int sheet, int id)
{
    checkpoint();
    m_sheets[sheet].charts.removeIf([id](const Chart &chart) {
        return chart.id == id;
    });
    changed();
}

ChartData Workbook::chartData(int sheet, const Chart &chart)
{
    ChartData data;
    Area area = clipped(sheet, chart.area);
    int usedRows = 0, usedCols = 0;
    used(sheet, &usedRows, &usedCols);
    area.r2 = qMin(area.r2, qMax(usedRows - 1, area.r1));
    area.c2 = qMin(area.c2, qMax(usedCols - 1, area.c1));
    // A single row or column cannot spare one for names.
    const bool headerRow = chart.headerRow && area.rows() > 1;
    const bool headerCol = chart.headerCol && area.cols() > 1;
    const int firstRow = area.r1 + (headerRow ? 1 : 0);
    const int firstCol = area.c1 + (headerCol ? 1 : 0);
    for (int r = firstRow; r <= area.r2 && data.categories.size() < 2000; ++r) {
        if (rowHidden(sheet, r)) {
            continue;
        }
        data.categories.append(headerCol ? display(sheet, r, area.c1) : QString::number(r - firstRow + 1));
        const Value x = headerCol ? cellValue(sheet, r, area.c1) : Value(double(r - firstRow + 1));
        data.xs.append(x.isNumber() ? x.num : qQNaN());
    }
    for (int c = firstCol; c <= area.c2 && data.series.size() < 50; ++c) {
        if (colHidden(sheet, c)) {
            continue;
        }
        QString name = headerRow ? display(sheet, area.r1, c) : QString();
        if (name.isEmpty()) {
            name = QStringLiteral("Serie %1").arg(data.series.size() + 1);
        }
        data.seriesNames.append(name);
        QList<double> values;
        for (int r = firstRow; r <= area.r2 && values.size() < 2000; ++r) {
            if (rowHidden(sheet, r)) {
                continue;
            }
            const Value v = cellValue(sheet, r, c);
            values.append(v.isNumber() ? v.num : qQNaN());
        }
        data.series.append(values);
    }
    return data;
}

// --- Tables ---------------------------------------------------------------------

int Workbook::tableAt(int sheet, int row, int col) const
{
    const QList<Table> &tables = m_sheets.at(sheet).tables;
    for (int i = 0; i < tables.size(); ++i) {
        if (tables.at(i).area.contains(row, col)) {
            return i;
        }
    }
    return -1;
}

int Workbook::addTable(int sheet, const Area &wanted)
{
    Area area = wanted.rows() == 1 && wanted.cols() == 1 ? region(sheet, wanted.r1, wanted.c1) : clipped(sheet, wanted);
    int usedRows = 0, usedCols = 0;
    used(sheet, &usedRows, &usedCols);
    area.r2 = qMin(area.r2, qMax(usedRows - 1, area.r1));
    area.c2 = qMin(area.c2, qMax(usedCols - 1, area.c1));
    if (area.rows() < 2) {
        return -1;
    }
    for (const Table &other : std::as_const(m_sheets.at(sheet).tables)) {
        if (other.area.intersects(area)) {
            return -1;
        }
    }
    beginGroup();
    checkpoint();
    Table table;
    table.area = area;
    m_sheets[sheet].tables.append(table);
    changed();
    setFilter(sheet, area);
    endGroup();
    return m_sheets.at(sheet).tables.size() - 1;
}

void Workbook::removeTable(int sheet, int index)
{
    if (index < 0 || index >= m_sheets.at(sheet).tables.size()) {
        return;
    }
    beginGroup();
    checkpoint();
    const Table table = m_sheets.at(sheet).tables.at(index);
    m_sheets[sheet].tables.removeAt(index);
    changed();
    if (m_sheets.at(sheet).hasFilter && m_sheets.at(sheet).filterArea.r1 == table.area.r1 && m_sheets.at(sheet).filterArea.c1 == table.area.c1) {
        clearFilter(sheet);
    }
    endGroup();
}

void Workbook::setTableTotals(int sheet, int index, bool on)
{
    if (index < 0 || index >= m_sheets.at(sheet).tables.size() || m_sheets.at(sheet).tables.at(index).totals == on) {
        return;
    }
    beginGroup();
    checkpoint();
    Table table = m_sheets.at(sheet).tables.at(index);
    if (on) {
        const int row = table.area.r2 + 1;
        bool free = true;
        for (int c = table.area.c1; c <= table.area.c2; ++c) {
            const Cell *below = cell(sheet, row, c);
            free = free && (!below || (below->value.isEmpty() && !below->formula));
        }
        if (!free) {
            insertRows(sheet, row, 1);
            table = m_sheets.at(sheet).tables.at(index);
        }
        const int first = table.area.r1 + 1;
        const int last = table.area.r2;
        for (int c = table.area.c1; c <= table.area.c2; ++c) {
            bool numeric = false;
            for (int r = first; r <= last && !numeric; ++r) {
                numeric = value(sheet, r, c).isNumber();
            }
            if (c == table.area.c1 && !numeric) {
                setInput(sheet, row, c, QStringLiteral("Total"));
            } else if (numeric) {
                setInput(sheet, row, c, QStringLiteral("=SUBTOTAL(109,%1)").arg(Formula::areaName(first, c, last, c)));
                const Cell *above = cell(sheet, last, c);
                const QString format = above ? m_styles.at(above->style).format : QString();
                changeStyle(sheet, Area{row, c, row, c}, [&format](Style &style) { style.format = format; });
            }
        }
        table.area.r2 = row;
        table.totals = true;
    } else {
        clearArea(sheet, Area{table.area.r2, table.area.c1, table.area.r2, table.area.c2}, true, false);
        table.area.r2 -= 1;
        table.totals = false;
    }
    m_sheets[sheet].tables[index] = table;
    changed();
    endGroup();
}
