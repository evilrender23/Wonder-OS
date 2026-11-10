#pragma once

#include "formats.h"

#include <QList>

/// The document in hand and what was done to it: every change goes through
/// here, so that all of it can be undone.
class Session
{
public:
    Session();

    const FormulaDoc &document() const { return m_doc; }
    /// A new document: the history starts again and nothing is unsaved.
    void setDocument(const FormulaDoc &doc);

    int count() const { return m_doc.equations.size(); }
    int current() const { return m_current; }
    void setCurrent(int index);
    QString latex() const { return m_doc.equations.value(m_current); }

    /// Typing: the changes to one equation in a row are one step to undo, until `breakRun`.
    void setLatex(const QString &latex);
    void breakRun() { m_run = false; }
    /// A new equation after the current one, which becomes the current.
    void add(const QString &latex = QString());
    void duplicate();
    /// The last one is emptied instead: a document always has an equation.
    void remove(int index);
    void move(int from, int to);
    void setTitle(const QString &title);
    void setColor(const QString &color);
    void setSize(qreal points);
    void setFont(const QString &family);
    /// Everything but the uid, in one step (a version brought back).
    void replace(const FormulaDoc &doc);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    bool undo();
    bool redo();

    bool dirty() const { return m_dirty; }
    void markSaved() { m_dirty = false; m_run = false; }
    void touch() { m_dirty = true; }

private:
    struct State {
        FormulaDoc doc;
        int current = 0;
    };
    void record();

    FormulaDoc m_doc;
    int m_current = 0;
    QList<State> m_undo;
    QList<State> m_redo;
    bool m_run = false;
    bool m_dirty = false;
};
