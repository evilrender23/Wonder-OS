#include "fileops.h"

#include "format.h"

#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/EmptyTrashJob>
#include <KIO/FileUndoManager>
#include <KIO/MkdirJob>
#include <KIO/Paste>
#include <KIO/PasteJob>
#include <KIO/RestoreJob>

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStorageInfo>

#include <memory>

static QUrl parentOf(const QUrl &url)
{
    return url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
}

static QUrl childOf(const QUrl &directory, const QString &name)
{
    QUrl url = directory;
    const QString path = directory.path();
    url.setPath(path + (path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    return url;
}

FileOps::FileOps(QObject *parent)
    : QObject(parent)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FileOps::clipboardChanged);
    auto *undo = KIO::FileUndoManager::self();
    connect(undo, &KIO::FileUndoManager::undoAvailable, this, &FileOps::undoChanged);
    connect(undo, &KIO::FileUndoManager::undoTextChanged, this, &FileOps::undoChanged);
}

QList<QUrl> FileOps::clipboardUrls() const
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    return data && data->hasUrls() ? data->urls() : QList<QUrl>();
}

bool FileOps::canPaste() const
{
    return !clipboardUrls().isEmpty();
}

bool FileOps::clipboardIsCut() const
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    return data && data->hasUrls() && KIO::isClipboardDataCut(data);
}

bool FileOps::canUndo() const
{
    return KIO::FileUndoManager::self()->isUndoAvailable();
}

QString FileOps::undoText() const
{
    return KIO::FileUndoManager::self()->undoText().remove(QLatin1Char('&'));
}

double FileOps::progress() const
{
    if (m_jobs.isEmpty()) {
        return m_processes > 0 ? -1 : 0;
    }
    double total = 0;
    for (const KJob *job : m_jobs) {
        total += job->percent();
    }
    return total / (100.0 * m_jobs.size());
}

static QMimeData *urlData(const QList<QUrl> &urls, bool cut)
{
    auto *data = new QMimeData;
    data->setUrls(urls);
    QStringList lines;
    for (const QUrl &url : urls) {
        lines << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    }
    data->setText(lines.join(QLatin1Char('\n')));
    KIO::setClipboardDataCut(data, cut);
    return data;
}

void FileOps::copyToClipboard(const QList<QUrl> &urls)
{
    if (!urls.isEmpty()) {
        QGuiApplication::clipboard()->setMimeData(urlData(urls, false));
    }
}

void FileOps::cutToClipboard(const QList<QUrl> &urls)
{
    if (!urls.isEmpty()) {
        QGuiApplication::clipboard()->setMimeData(urlData(urls, true));
    }
}

void FileOps::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

void FileOps::track(KJob *job, const QString &status)
{
    // No UI delegate: conflicts and errors come back through failed(), drawn
    // by the window itself instead of a KDE dialog.
    m_jobs << job;
    m_status = status;
    Q_EMIT busyChanged();
    Q_EMIT progressChanged();
    connect(job, &KJob::percentChanged, this, &FileOps::progressChanged);
    connect(job, &KJob::result, this, [this](KJob *finished) {
        m_jobs.removeAll(finished);
        if (finished->error() && finished->error() != KJob::KilledJobError) {
            Q_EMIT failed(finished->errorString());
        }
        Q_EMIT busyChanged();
        Q_EMIT progressChanged();
    });
}

void FileOps::trackCopy(KIO::CopyJob *job, const QString &status)
{
    KIO::FileUndoManager::self()->recordCopyJob(job);
    const QUrl destination = job->destUrl().adjusted(QUrl::StripTrailingSlash);
    auto made = std::make_shared<QList<QUrl>>();
    connect(job, &KIO::CopyJob::copyingDone, this, [made, destination](KIO::Job *, const QUrl &, const QUrl &to) {
        // Only the top-level items: a copied folder reports every file inside it too.
        if (parentOf(to) == destination) {
            made->append(to);
        }
    });
    connect(job, &KIO::CopyJob::copyingLinkDone, this, [made, destination](KIO::Job *, const QUrl &, const QString &, const QUrl &to) {
        if (parentOf(to) == destination) {
            made->append(to);
        }
    });
    connect(job, &KJob::result, this, [this, made] {
        if (!made->isEmpty()) {
            Q_EMIT created(*made);
        }
    });
    track(job, status);
}

void FileOps::paste(const QUrl &destination)
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasUrls()) {
        return;
    }
    const bool cut = KIO::isClipboardDataCut(data);
    KIO::PasteJob *job = KIO::paste(data, destination, KIO::HideProgressInfo);
    auto made = std::make_shared<QList<QUrl>>();
    connect(job, &KIO::PasteJob::copyJobStarted, this, [](KIO::CopyJob *copy) {
        KIO::FileUndoManager::self()->recordCopyJob(copy);
    });
    connect(job, &KIO::PasteJob::itemCreated, this, [made](const QUrl &url) {
        made->append(url);
    });
    connect(job, &KJob::result, this, [this, made, cut](KJob *finished) {
        if (!made->isEmpty()) {
            Q_EMIT created(*made);
        }
        if (cut && !finished->error()) {
            QGuiApplication::clipboard()->clear();
        }
    });
    track(job, cut ? tr("Moving…") : tr("Copying…"));
}

void FileOps::copyTo(const QList<QUrl> &urls, const QUrl &destination)
{
    if (!urls.isEmpty()) {
        trackCopy(KIO::copy(urls, destination, KIO::HideProgressInfo), tr("Copying…"));
    }
}

void FileOps::moveTo(const QList<QUrl> &urls, const QUrl &destination)
{
    QList<QUrl> moving;
    for (const QUrl &url : urls) {
        // Dropping an item on the folder it is already in does nothing.
        if (parentOf(url) != destination.adjusted(QUrl::StripTrailingSlash)) {
            moving << url;
        }
    }
    if (!moving.isEmpty()) {
        trackCopy(KIO::move(moving, destination, KIO::HideProgressInfo), tr("Moving…"));
    }
}

void FileOps::drop(const QList<QUrl> &urls, const QUrl &destination, bool copy)
{
    if (urls.isEmpty()) {
        return;
    }
    for (const QUrl &url : urls) {
        if (url.adjusted(QUrl::StripTrailingSlash) == destination.adjusted(QUrl::StripTrailingSlash) || url.isParentOf(destination)) {
            return;
        }
    }
    if (destination.scheme() == QLatin1String("trash")) {
        trash(urls);
        return;
    }
    if (!copy) {
        if (urls.first().isLocalFile() && destination.isLocalFile()) {
            const QStorageInfo from(QFileInfo(urls.first().toLocalFile()).absolutePath());
            const QStorageInfo to(destination.toLocalFile());
            copy = from.rootPath() != to.rootPath();
        } else {
            copy = urls.first().scheme() != destination.scheme();
        }
    }
    if (copy) {
        copyTo(urls, destination);
    } else {
        moveTo(urls, destination);
    }
}

bool FileOps::dropWantsCopy(bool ownDrag, int proposedAction) const
{
    if (ownDrag) {
        return proposedAction == Qt::CopyAction;
    }
    return QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ControlModifier);
}

void FileOps::trash(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    KIO::CopyJob *job = KIO::trash(urls, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls, QUrl(QStringLiteral("trash:/")), job);
    connect(job, &KJob::result, this, &FileOps::trashChanged);
    track(job, tr("Moving to Trash…"));
}

void FileOps::remove(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    KJob *job = KIO::del(urls, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, &FileOps::trashChanged);
    track(job, tr("Deleting…"));
}

void FileOps::emptyTrash()
{
    KJob *job = KIO::emptyTrash();
    connect(job, &KJob::result, this, &FileOps::trashChanged);
    track(job, tr("Emptying the Trash…"));
}

void FileOps::restore(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    KJob *job = KIO::restoreFromTrash(urls, KIO::HideProgressInfo);
    connect(job, &KJob::result, this, &FileOps::trashChanged);
    track(job, tr("Putting back…"));
}

QStringList FileOps::namesIn(const QString &directory)
{
    return QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
}

QString FileOps::rename(const QUrl &url, const QString &newName)
{
    const QString name = newName.trimmed();
    const QString old = url.fileName();
    if (name.isEmpty() || name == old) {
        return QString();
    }
    if (name.contains(QLatin1Char('/'))) {
        return tr("The name can’t contain “/”.");
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return tr("The name “%1” can’t be used.").arg(name);
    }
    const QUrl target = childOf(parentOf(url), name);
    if (url.isLocalFile() && QFileInfo::exists(target.toLocalFile())) {
        return tr("The name “%1” is already taken. Please choose a different name.").arg(name);
    }
    KIO::CopyJob *job = KIO::moveAs(url, target, KIO::HideProgressInfo);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    connect(job, &KJob::result, this, [this, target](KJob *finished) {
        if (!finished->error()) {
            Q_EMIT created({target});
        }
    });
    track(job, tr("Renaming…"));
    return QString();
}

void FileOps::newFolder(const QUrl &directory)
{
    QString name = tr("untitled folder");
    if (directory.isLocalFile()) {
        name = Format::uniqueName(name, true, QString(), namesIn(directory.toLocalFile()));
    }
    const QUrl target = childOf(directory, name);
    KIO::MkdirJob *job = KIO::mkdir(target);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkdir, {}, target, job);
    connect(job, &KJob::result, this, [this, target](KJob *finished) {
        if (!finished->error()) {
            Q_EMIT folderCreated(target);
        }
    });
    track(job, tr("Creating folder…"));
}

void FileOps::duplicate(const QList<QUrl> &urls)
{
    // Names are reserved as they are handed out: the jobs run in parallel,
    // so two items duplicated at once must not be given the same new name.
    QHash<QString, QStringList> taken;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        const QString directory = info.absolutePath();
        if (!taken.contains(directory)) {
            taken.insert(directory, namesIn(directory));
        }
        const QString name = Format::uniqueName(info.fileName(), info.isDir() && !info.isSymLink(), tr("copy"), taken.value(directory));
        taken[directory] << name;
        const QUrl target = QUrl::fromLocalFile(directory + QLatin1Char('/') + name);
        KIO::CopyJob *job = KIO::copyAs(url, target, KIO::HideProgressInfo);
        KIO::FileUndoManager::self()->recordCopyJob(job);
        connect(job, &KJob::result, this, [this, target](KJob *finished) {
            if (!finished->error()) {
                Q_EMIT created({target});
            }
        });
        track(job, tr("Duplicating…"));
    }
}

void FileOps::makeAlias(const QList<QUrl> &urls)
{
    QHash<QString, QStringList> taken;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        const QString directory = info.absolutePath();
        if (!taken.contains(directory)) {
            taken.insert(directory, namesIn(directory));
        }
        const QString name = Format::uniqueName(info.fileName(), true, tr("alias"), taken.value(directory));
        taken[directory] << name;
        const QUrl target = QUrl::fromLocalFile(directory + QLatin1Char('/') + name);
        KIO::CopyJob *job = KIO::linkAs(url, target, KIO::HideProgressInfo);
        KIO::FileUndoManager::self()->recordCopyJob(job);
        connect(job, &KJob::result, this, [this, target](KJob *finished) {
            if (!finished->error()) {
                Q_EMIT created({target});
            }
        });
        track(job, tr("Making alias…"));
    }
}

bool FileOps::isArchive(const QString &mime) const
{
    static const QStringList archives = {
        QStringLiteral("application/zip"),
        QStringLiteral("application/x-tar"),
        QStringLiteral("application/x-compressed-tar"),
        QStringLiteral("application/x-bzip2-compressed-tar"),
        QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-zstd-compressed-tar"),
        QStringLiteral("application/x-7z-compressed"),
        QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-cpio"),
    };
    return archives.contains(mime);
}

void FileOps::runArchiver(const QStringList &arguments, const QString &workingDirectory, const QUrl &result, const QString &status)
{
    auto *process = new QProcess(this);
    process->setWorkingDirectory(workingDirectory);
    process->setProcessChannelMode(QProcess::MergedChannels);
    ++m_processes;
    m_status = status;
    Q_EMIT busyChanged();
    Q_EMIT progressChanged();

    auto settled = std::make_shared<bool>(false);
    const auto finish = [this, process, result, settled](bool ok) {
        if (*settled) {
            return;
        }
        *settled = true;
        const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
        process->deleteLater();
        --m_processes;
        if (ok) {
            Q_EMIT created({result});
        } else {
            // A failed run leaves a truncated archive or an empty folder behind.
            const QFileInfo partial(result.toLocalFile());
            if (partial.isFile()) {
                QFile::remove(partial.filePath());
            } else if (partial.isDir() && QDir(partial.filePath()).isEmpty()) {
                QDir().rmdir(partial.filePath());
            }
            Q_EMIT failed(output.isEmpty() ? tr("The archive tool (bsdtar) could not be run.") : output);
        }
        Q_EMIT busyChanged();
        Q_EMIT progressChanged();
    };
    connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus exit) {
        finish(exit == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false);
        }
    });
    process->start(QStringLiteral("bsdtar"), arguments);
}

void FileOps::compress(const QList<QUrl> &urls)
{
    QStringList names;
    QString directory;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QFileInfo info(url.toLocalFile());
        if (directory.isEmpty()) {
            directory = info.absolutePath();
        }
        if (info.absolutePath() == directory) {
            names << info.fileName();
        }
    }
    if (names.isEmpty()) {
        return;
    }
    const QString wanted = (names.size() == 1 ? names.first() : tr("Archive")) + QStringLiteral(".zip");
    const QString archive = Format::uniqueName(wanted, false, QString(), namesIn(directory));
    QStringList arguments = {QStringLiteral("--format"), QStringLiteral("zip"), QStringLiteral("-cf"), archive, QStringLiteral("--")};
    arguments << names;
    runArchiver(arguments, directory, QUrl::fromLocalFile(directory + QLatin1Char('/') + archive), tr("Compressing…"));
}

void FileOps::extract(const QUrl &archive)
{
    if (!archive.isLocalFile()) {
        return;
    }
    const QFileInfo info(archive.toLocalFile());
    QString base = info.completeBaseName();
    if (base.endsWith(QLatin1String(".tar"), Qt::CaseInsensitive)) {
        base.chop(4);
    }
    const QString folder = Format::uniqueName(base, true, QString(), namesIn(info.absolutePath()));
    const QString target = info.absolutePath() + QLatin1Char('/') + folder;
    if (!QDir().mkdir(target)) {
        Q_EMIT failed(tr("The folder “%1” can’t be created.").arg(folder));
        return;
    }
    runArchiver({QStringLiteral("-xf"), info.absoluteFilePath(), QStringLiteral("-C"), target}, info.absolutePath(), QUrl::fromLocalFile(target), tr("Expanding…"));
}

void FileOps::undo()
{
    if (canUndo()) {
        KIO::FileUndoManager::self()->undo();
    }
}

void FileOps::cancel()
{
    const QList<KJob *> jobs = m_jobs;
    for (KJob *job : jobs) {
        job->kill(KJob::EmitResult);
    }
}
