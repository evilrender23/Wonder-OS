#pragma once

#include "model.h"

#include <optional>

/// One step that can be undone: the objects it touched, as they were before
/// and as they are after (absent on the side where the object does not
/// exist), and the order of the canvas on both sides.
struct Command {
    struct Change {
        QString id;
        std::optional<Shape> before;
        std::optional<Shape> after;
    };
    QString label;
    QList<Change> changes;
    QStringList orderBefore;
    QStringList orderAfter;

    bool isEmpty() const { return changes.isEmpty() && orderBefore == orderAfter; }
    /// What turned `before` into `after`.
    static Command between(const Document &before, const Document &after, const QString &label);
    void undo(Document &doc) const;
    void redo(Document &doc) const;
};

/// The steps taken on a drawing, to walk back and forth.
class History
{
public:
    /// Takes a step; what had been undone can no longer be redone. A step
    /// with the same `mergeKey` as the one before joins it (typing a number,
    /// dragging a slider), so one undo takes back the whole gesture.
    void push(const Command &command, const QString &mergeKey = {});
    bool canUndo() const { return m_at > 0; }
    bool canRedo() const { return m_at < m_commands.size(); }
    QString undoLabel() const { return canUndo() ? m_commands.at(m_at - 1).label : QString(); }
    QString redoLabel() const { return canRedo() ? m_commands.at(m_at).label : QString(); }
    bool undo(Document &doc);
    bool redo(Document &doc);
    void clear();
    /// Remembers this point as what is on disk.
    void markSaved() { m_saved = m_at; m_mergeKey.clear(); }
    bool atSaved() const { return m_saved == m_at; }
    /// No step joins the one before any more.
    void seal() { m_mergeKey.clear(); }
    int size() const { return m_commands.size(); }

private:
    QList<Command> m_commands;
    int m_at = 0;
    int m_saved = 0;
    QString m_mergeKey;
};
