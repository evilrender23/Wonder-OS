#include "history.h"

#include <QHash>

static QStringList order(const Document &doc)
{
    QStringList ids;
    ids.reserve(doc.shapes.size());
    for (const Shape &s : doc.shapes) {
        ids << s.id;
    }
    return ids;
}

Command Command::between(const Document &before, const Document &after, const QString &label)
{
    Command command;
    command.label = label;
    command.orderBefore = order(before);
    command.orderAfter = order(after);
    QHash<QString, const Shape *> old;
    for (const Shape &s : before.shapes) {
        old.insert(s.id, &s);
    }
    QHash<QString, bool> seen;
    for (const Shape &s : after.shapes) {
        seen.insert(s.id, true);
        const Shape *was = old.value(s.id);
        if (!was) {
            command.changes << Change{s.id, std::nullopt, s};
        } else if (!(*was == s)) {
            command.changes << Change{s.id, *was, s};
        }
    }
    for (const Shape &s : before.shapes) {
        if (!seen.contains(s.id)) {
            command.changes << Change{s.id, s, std::nullopt};
        }
    }
    return command;
}

static void apply(Document &doc, const QList<Command::Change> &changes, bool forward, const QStringList &order)
{
    for (const Command::Change &change : changes) {
        const std::optional<Shape> &target = forward ? change.after : change.before;
        const int index = doc.indexOf(change.id);
        if (!target) {
            if (index >= 0) {
                doc.shapes.removeAt(index);
            }
        } else if (index >= 0) {
            doc.shapes[index] = *target;
        } else {
            doc.shapes << *target;
        }
    }
    QHash<QString, int> place;
    for (int i = 0; i < order.size(); ++i) {
        place.insert(order.at(i), i);
    }
    std::stable_sort(doc.shapes.begin(), doc.shapes.end(), [&place](const Shape &a, const Shape &b) {
        return place.value(a.id, INT_MAX) < place.value(b.id, INT_MAX);
    });
}

void Command::undo(Document &doc) const
{
    apply(doc, changes, false, orderBefore);
}

void Command::redo(Document &doc) const
{
    apply(doc, changes, true, orderAfter);
}

void History::push(const Command &command, const QString &mergeKey)
{
    if (command.isEmpty()) {
        return;
    }
    const bool merge = !mergeKey.isEmpty() && mergeKey == m_mergeKey && m_at == m_commands.size() && m_at > 0 && m_saved != m_at;
    if (merge) {
        // Keep the oldest «before» of each object and take the newest «after».
        Command &last = m_commands[m_at - 1];
        for (const Command::Change &change : command.changes) {
            bool found = false;
            for (Command::Change &old : last.changes) {
                if (old.id == change.id) {
                    old.after = change.after;
                    found = true;
                    break;
                }
            }
            if (!found) {
                last.changes << change;
            }
        }
        last.orderAfter = command.orderAfter;
        return;
    }
    if (m_saved > m_at) {
        // What is on disk can no longer be reached by undoing or redoing.
        m_saved = -1;
    }
    m_commands.resize(m_at);
    m_commands << command;
    m_at = m_commands.size();
    m_mergeKey = mergeKey;
    // An afternoon of drawing must not grow without end.
    const int limit = 400;
    if (m_commands.size() > limit) {
        const int drop = m_commands.size() - limit;
        m_commands.remove(0, drop);
        m_at -= drop;
        m_saved = m_saved >= drop ? m_saved - drop : -1;
    }
}

bool History::undo(Document &doc)
{
    if (!canUndo()) {
        return false;
    }
    m_commands.at(--m_at).undo(doc);
    m_mergeKey.clear();
    return true;
}

bool History::redo(Document &doc)
{
    if (!canRedo()) {
        return false;
    }
    m_commands.at(m_at++).redo(doc);
    m_mergeKey.clear();
    return true;
}

void History::clear()
{
    m_commands.clear();
    m_at = 0;
    m_saved = 0;
    m_mergeKey.clear();
}
