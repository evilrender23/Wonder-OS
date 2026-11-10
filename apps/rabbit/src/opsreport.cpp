#include "opsreport.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

OpsReport::OpsReport(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
    m_throttle.setSingleShot(true);
    m_throttle.setInterval(100);
    m_beat.setInterval(1000);
    connect(&m_throttle, &QTimer::timeout, this, &OpsReport::write);
    connect(&m_beat, &QTimer::timeout, this, &OpsReport::write);
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    // Whatever a previous run left behind is over.
    write();
}

OpsReport::~OpsReport()
{
    if (m_busy) {
        m_busy = false;
        write();
    }
}

QString OpsReport::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wonderland/rabbit-ops.json");
}

void OpsReport::set(bool busy, const QString &kind, const QString &status, double progress, const QString &detail)
{
    const bool now = busy != m_busy || kind != m_kind || status != m_status;
    const bool changed = now || progress != m_progress || detail != m_detail;
    m_busy = busy;
    m_kind = kind;
    m_status = status;
    m_progress = progress;
    m_detail = detail;
    if (!busy) {
        m_beat.stop();
    } else if (!m_beat.isActive()) {
        m_beat.start();
    }
    if (now) {
        write();
    } else if (changed && !m_throttle.isActive()) {
        m_throttle.start();
    }
}

void OpsReport::write()
{
    m_throttle.stop();
    QJsonObject state;
    state.insert(QStringLiteral("busy"), m_busy);
    if (m_busy) {
        state.insert(QStringLiteral("kind"), m_kind);
        state.insert(QStringLiteral("status"), m_status);
        state.insert(QStringLiteral("progress"), m_progress);
        state.insert(QStringLiteral("detail"), m_detail);
    }
    state.insert(QStringLiteral("updated"), QDateTime::currentMSecsSinceEpoch());
    // Written in place: the bar watches this very file, and a rename would
    // leave it watching the old one.
    QFile file(m_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    }
}
