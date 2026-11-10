#include "session.h"

namespace {
const int MaxSteps = 200;
}

Session::Session()
{
    m_doc.equations.append(QString());
}

void Session::setDocument(const FormulaDoc &doc)
{
    m_doc = doc;
    if (m_doc.equations.isEmpty()) {
        m_doc.equations.append(QString());
    }
    m_current = 0;
    m_undo.clear();
    m_redo.clear();
    m_run = false;
    m_dirty = false;
}

void Session::setCurrent(int index)
{
    index = qBound(0, index, count() - 1);
    if (index != m_current) {
        m_current = index;
        m_run = false;
    }
}

void Session::record()
{
    m_undo.append({m_doc, m_current});
    if (m_undo.size() > MaxSteps) {
        m_undo.removeFirst();
    }
    m_redo.clear();
    m_dirty = true;
}

void Session::setLatex(const QString &latex)
{
    if (latex == m_doc.equations.at(m_current)) {
        return;
    }
    if (!m_run) {
        record();
        m_run = true;
    }
    m_redo.clear();
    m_dirty = true;
    m_doc.equations[m_current] = latex;
}

void Session::add(const QString &latex)
{
    record();
    m_run = false;
    m_doc.equations.insert(m_current + 1, latex);
    ++m_current;
}

void Session::duplicate()
{
    add(latex());
}

void Session::remove(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    if (count() == 1 && m_doc.equations.first().isEmpty()) {
        return;
    }
    record();
    m_run = false;
    if (count() == 1) {
        m_doc.equations[0].clear();
        return;
    }
    m_doc.equations.removeAt(index);
    if (m_current > index || m_current >= count()) {
        --m_current;
    }
}

void Session::move(int from, int to)
{
    to = qBound(0, to, count() - 1);
    if (from < 0 || from >= count() || from == to) {
        return;
    }
    record();
    m_run = false;
    m_doc.equations.move(from, to);
    if (m_current == from) {
        m_current = to;
    } else if (from < m_current && to >= m_current) {
        --m_current;
    } else if (from > m_current && to <= m_current) {
        ++m_current;
    }
}

void Session::setTitle(const QString &title)
{
    if (title != m_doc.title) {
        record();
        m_run = false;
        m_doc.title = title;
    }
}

void Session::setColor(const QString &color)
{
    if (color != m_doc.color) {
        record();
        m_run = false;
        m_doc.color = color;
    }
}

void Session::setSize(qreal points)
{
    points = qBound(6.0, points, 144.0);
    if (!qFuzzyCompare(points, m_doc.size)) {
        record();
        m_run = false;
        m_doc.size = points;
    }
}

void Session::setFont(const QString &family)
{
    if (family != m_doc.font) {
        record();
        m_run = false;
        m_doc.font = family;
    }
}

void Session::replace(const FormulaDoc &doc)
{
    record();
    m_run = false;
    const QString uid = m_doc.uid;
    m_doc = doc;
    m_doc.uid = uid;
    if (m_doc.equations.isEmpty()) {
        m_doc.equations.append(QString());
    }
    m_current = qBound(0, m_current, count() - 1);
}

bool Session::undo()
{
    if (m_undo.isEmpty()) {
        return false;
    }
    m_redo.append({m_doc, m_current});
    const State state = m_undo.takeLast();
    m_doc = state.doc;
    m_current = qBound(0, state.current, count() - 1);
    m_run = false;
    m_dirty = true;
    return true;
}

bool Session::redo()
{
    if (m_redo.isEmpty()) {
        return false;
    }
    m_undo.append({m_doc, m_current});
    const State state = m_redo.takeLast();
    m_doc = state.doc;
    m_current = qBound(0, state.current, count() - 1);
    m_run = false;
    m_dirty = true;
    return true;
}
