#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

/// Tells the shell what the file operations are doing, so the top bar can
/// draw their progress: a small JSON file in the runtime directory that the
/// bar watches. Rewritten at most every `interval` while the numbers move, and
/// once a second regardless, which is how the bar tells a live operation from
/// one whose Rabbit died.
class OpsReport : public QObject
{
    Q_OBJECT

public:
    explicit OpsReport(const QString &path, QObject *parent = nullptr);
    ~OpsReport() override;

    /// $XDG_RUNTIME_DIR/wonderland/rabbit-ops.json
    static QString defaultPath();

    /// `kind` is copy, move, trash, delete or other; `progress` is 0–1, or
    /// negative when the amount of work is unknown.
    void set(bool busy, const QString &kind, const QString &status, double progress, const QString &detail);
    void setInterval(int ms) { m_throttle.setInterval(ms); }

private:
    void write();

    QString m_path;
    bool m_busy = false;
    QString m_kind;
    QString m_status;
    double m_progress = 0;
    QString m_detail;
    QTimer m_throttle;
    QTimer m_beat;
};
