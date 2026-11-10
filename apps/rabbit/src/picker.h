#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class App;

/// One "Open" or "Save" another app asked for: what the window that answers it
/// needs to know, and where its answer goes.
class Picker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Made when an app asks for a file.")
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)
    Q_PROPERTY(bool saving READ saving CONSTANT)
    Q_PROPERTY(bool folders READ folders CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(QUrl folder READ folder CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QStringList filterNames READ filterNames CONSTANT)
    Q_PROPERTY(int filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QStringList globs READ globs NOTIFY filterChanged)
    Q_PROPERTY(QStringList mimes READ mimes NOTIFY filterChanged)

public:
    enum class Kind { OpenFile, SaveFile, SaveFiles };
    /// The portal's answers: the choice, a dismissal, or an end the user didn't ask for.
    enum Response : uint { Chosen = 0, Cancelled = 1, Ended = 2 };

    struct Filter {
        QString name;
        QStringList globs;
        QStringList mimes;
    };

    /// `options` are those of org.freedesktop.impl.portal.FileChooser.
    Picker(Kind kind, const QString &title, const QVariantMap &options, QObject *parent);
    /// Test runs (--pick): "open", "open-many", "folder" or "save" in `folder`.
    static Picker *forTest(const QString &kind, const QUrl &folder, QObject *parent);

    QString title() const { return m_title; }
    QString acceptLabel() const { return m_acceptLabel; }
    bool saving() const { return m_kind == Kind::SaveFile; }
    /// The answer is a folder: "Choose Folder", or where to save several files.
    bool folders() const { return m_directory || m_kind == Kind::SaveFiles; }
    bool multiple() const { return m_multiple; }
    QUrl folder() const { return m_folder; }
    QString name() const { return m_name; }
    QStringList filterNames() const;
    int filter() const { return m_filter; }
    void setFilter(int filter);
    QStringList globs() const;
    QStringList mimes() const;
    void setFilters(const QList<Filter> &filters, int current = 0);

    Q_INVOKABLE void accept(const QList<QUrl> &urls);
    Q_INVOKABLE void cancel();
    /// The app that asked withdrew its request.
    void end();

    /// What accepting `urls` answers: for several files to save, each one's place in the folder.
    QStringList answerFor(const QList<QUrl> &urls) const;

Q_SIGNALS:
    void filterChanged();
    void finished(uint response, const QStringList &uris);

private:
    void finish(Response response, const QStringList &uris);

    Kind m_kind;
    QString m_title;
    QString m_acceptLabel;
    bool m_directory = false;
    bool m_multiple = false;
    bool m_done = false;
    QUrl m_folder;
    QString m_name;
    QStringList m_files;
    QList<Filter> m_filters;
    int m_filter = 0;
};

/// org.freedesktop.impl.portal.FileChooser: the desktop's "Open" and "Save" are Rabbit's windows.
class FileChooserAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")

public:
    explicit FileChooserAdaptor(App *app);

public Q_SLOTS:
    uint OpenFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title, const QVariantMap &options,
                  const QDBusMessage &message, QVariantMap &results);
    uint SaveFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title, const QVariantMap &options,
                  const QDBusMessage &message, QVariantMap &results);
    uint SaveFiles(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window, const QString &title, const QVariantMap &options,
                   const QDBusMessage &message, QVariantMap &results);

private:
    void ask(Picker::Kind kind, const QDBusObjectPath &handle, const QString &title, const QVariantMap &options, const QDBusMessage &message);

    App *m_app;
};

/// org.freedesktop.impl.portal.Request, on each request's own path: the asking app can take it back.
class PickRequestAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")

public:
    explicit PickRequestAdaptor(Picker *picker);

public Q_SLOTS:
    void Close();

private:
    Picker *m_picker;
};
