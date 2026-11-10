#pragma once

#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class KJob;
class OpsReport;
namespace KIO
{
class CopyJob;
}

/// Everything that changes files: clipboard, copy/move/trash, rename,
/// duplicate, alias, archives and undo. Jobs run through KIO, so they work on
/// any URL KIO lists and land in the shared undo history.
class FileOps : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    Q_PROPERTY(bool clipboardIsCut READ clipboardIsCut NOTIFY clipboardChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY busyChanged)

public:
    explicit FileOps(QObject *parent = nullptr);

    bool canPaste() const;
    bool clipboardIsCut() const;
    bool canUndo() const;
    QString undoText() const;
    bool busy() const { return !m_jobs.isEmpty() || m_processes > 0; }
    double progress() const;
    QString status() const { return m_status; }
    /// "1.2 GB of 4 GB" or "3 of 12", empty while the amount is unknown.
    QString detail() const;
    /// Publishes what is running to this file, for the top bar (see OpsReport).
    void reportTo(const QString &path);

    Q_INVOKABLE void copyToClipboard(const QList<QUrl> &urls);
    Q_INVOKABLE void cutToClipboard(const QList<QUrl> &urls);
    Q_INVOKABLE QList<QUrl> clipboardUrls() const;
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void paste(const QUrl &destination);

    Q_INVOKABLE void copyTo(const QList<QUrl> &urls, const QUrl &destination);
    Q_INVOKABLE void moveTo(const QList<QUrl> &urls, const QUrl &destination);
    /// A drop: moves within a volume, copies across volumes or when `copy` is held.
    Q_INVOKABLE void drop(const QList<QUrl> &urls, const QUrl &destination, bool copy);
    /// Whether a drop asks for a copy. A drag from another app always proposes
    /// a copy (Qt's default for a foreign drag), so for those only a held Ctrl
    /// counts, as on macOS where a drop moves within a volume unless ⌥ is held.
    Q_INVOKABLE bool dropWantsCopy(bool ownDrag, int proposedAction) const;
    Q_INVOKABLE void trash(const QList<QUrl> &urls);
    Q_INVOKABLE void remove(const QList<QUrl> &urls);
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void restore(const QList<QUrl> &urls);
    /// Returns the reason the name is unusable, or an empty string once the rename has started.
    Q_INVOKABLE QString rename(const QUrl &url, const QString &newName);
    Q_INVOKABLE void newFolder(const QUrl &directory);
    Q_INVOKABLE void duplicate(const QList<QUrl> &urls);
    Q_INVOKABLE void makeAlias(const QList<QUrl> &urls);
    Q_INVOKABLE void compress(const QList<QUrl> &urls);
    Q_INVOKABLE void extract(const QUrl &archive);
    Q_INVOKABLE bool isArchive(const QString &mime) const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void clipboardChanged();
    void undoChanged();
    void busyChanged();
    void progressChanged();
    /// New items the view should select once they are listed.
    void created(const QList<QUrl> &urls);
    /// Like created(), for a new folder: the view starts renaming it.
    void folderCreated(const QUrl &url);
    void failed(const QString &message);
    void trashChanged();

private:
    void track(KJob *job, const QString &status, const QString &kind = QStringLiteral("other"));
    void trackCopy(KIO::CopyJob *job, const QString &status, const QString &kind);
    void publish();
    void runArchiver(const QStringList &arguments, const QString &workingDirectory, const QUrl &result, const QString &status);
    static QStringList namesIn(const QString &directory);

    QList<KJob *> m_jobs;
    QString m_status;
    QString m_kind;
    int m_processes = 0;
    OpsReport *m_report = nullptr;
};
