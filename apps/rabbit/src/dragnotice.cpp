#include "dragnotice.h"

#include <QProcess>
#include <QStandardPaths>

DragNotice::DragNotice(QObject *parent)
    : QObject(parent)
    , m_command(shellCommand())
{
}

QStringList DragNotice::shellCommand(const QString &function)
{
    const QString qs = QStandardPaths::findExecutable(QStringLiteral("qs"));
    const QString shell =
        QStandardPaths::locate(QStandardPaths::GenericConfigLocation, QStringLiteral("driftwm/quickshell"), QStandardPaths::LocateDirectory);
    if (qs.isEmpty() || shell.isEmpty()) {
        return {};
    }
    return {qs, QStringLiteral("-p"), shell, QStringLiteral("ipc"), QStringLiteral("call"), QStringLiteral("desktop"), function};
}

void DragNotice::set(bool dragging)
{
    m_wanted = dragging;
    if (!m_process) {
        say();
    }
}

void DragNotice::say()
{
    if (m_command.isEmpty() || m_said == m_wanted) {
        return;
    }
    m_said = m_wanted;
    auto *process = new QProcess(this);
    m_process = process;
    const auto done = [this, process] {
        if (m_process != process) {
            return;
        }
        m_process = nullptr;
        process->deleteLater();
        say();
    };
    connect(process, &QProcess::finished, this, done);
    connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            done();
        }
    });
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    process->start(m_command.first(), m_command.mid(1) << (m_said ? QStringLiteral("true") : QStringLiteral("false")));
}
