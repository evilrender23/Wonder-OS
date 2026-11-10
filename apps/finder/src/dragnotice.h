#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

/// Tells the desktop shell when a drag of files starts and ends. Its desktop
/// only takes input on its icons, so that the canvas keeps its own clicks;
/// while it knows of a drag it takes the drop anywhere.
class DragNotice : public QObject
{
    Q_OBJECT

public:
    explicit DragNotice(QObject *parent = nullptr);

    /// The command that gets "true" or "false" appended; empty when there is
    /// no such shell here. `function` is the desktop's call to make.
    static QStringList shellCommand(const QString &function = QStringLiteral("appDrag"));
    void setCommand(const QStringList &command) { m_command = command; }

    /// Said in order, one at a time; a state that changed back meanwhile is not said at all.
    void set(bool dragging);
    bool idle() const { return !m_process; }

private:
    void say();

    QStringList m_command;
    QProcess *m_process = nullptr;
    bool m_wanted = false;
    bool m_said = false;
};
