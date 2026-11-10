#include "picker.h"

#include "app.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFileInfo>

/// Paths travel as bytes with their closing NUL.
static QString pathFrom(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    const int end = bytes.indexOf('\0');
    if (end >= 0) {
        bytes.truncate(end);
    }
    return QString::fromLocal8Bit(bytes);
}

/// One filter, "(sa(us))": its name and what it lets through, globs (0) and types (1).
static Picker::Filter filterFrom(const QDBusArgument &argument)
{
    Picker::Filter filter;
    argument.beginStructure();
    argument >> filter.name;
    argument.beginArray();
    while (!argument.atEnd()) {
        uint kind = 0;
        QString pattern;
        argument.beginStructure();
        argument >> kind >> pattern;
        argument.endStructure();
        (kind == 0 ? filter.globs : filter.mimes) << pattern;
    }
    argument.endArray();
    argument.endStructure();
    return filter;
}

static QList<Picker::Filter> filtersFrom(const QVariant &value)
{
    QList<Picker::Filter> filters;
    if (!value.canConvert<QDBusArgument>()) {
        return filters;
    }
    const QDBusArgument argument = value.value<QDBusArgument>();
    argument.beginArray();
    while (!argument.atEnd()) {
        filters << filterFrom(argument);
    }
    argument.endArray();
    return filters;
}

Picker::Picker(Kind kind, const QString &title, const QVariantMap &options, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_title(title)
    , m_acceptLabel(options.value(QStringLiteral("accept_label")).toString().remove(QLatin1Char('_')))
    , m_directory(kind == Kind::OpenFile && options.value(QStringLiteral("directory")).toBool())
    , m_multiple(kind == Kind::OpenFile && options.value(QStringLiteral("multiple")).toBool())
    , m_name(options.value(QStringLiteral("current_name")).toString())
{
    QString folder = pathFrom(options.value(QStringLiteral("current_folder")));
    const QString file = pathFrom(options.value(QStringLiteral("current_file")));
    if (!file.isEmpty()) {
        const QFileInfo info(file);
        folder = info.path();
        if (m_name.isEmpty()) {
            m_name = info.fileName();
        }
    }
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {
        m_folder = QUrl::fromLocalFile(folder);
    }
    if (kind == Kind::SaveFiles) {
        const QVariant files = options.value(QStringLiteral("files"));
        if (files.canConvert<QDBusArgument>()) {
            const QDBusArgument argument = files.value<QDBusArgument>();
            argument.beginArray();
            while (!argument.atEnd()) {
                QByteArray name;
                argument >> name;
                m_files << pathFrom(name);
            }
            argument.endArray();
        } else {
            m_files = files.toStringList();
        }
    }
    if (!folders()) {
        m_filters = filtersFrom(options.value(QStringLiteral("filters")));
        const QVariant current = options.value(QStringLiteral("current_filter"));
        if (current.canConvert<QDBusArgument>()) {
            const Filter wanted = filterFrom(current.value<QDBusArgument>());
            for (int i = 0; i < m_filters.size(); ++i) {
                if (m_filters.at(i).name == wanted.name) {
                    m_filter = i;
                }
            }
        }
    }
}

Picker *Picker::forTest(const QString &kind, const QUrl &folder, QObject *parent)
{
    QVariantMap options;
    options.insert(QStringLiteral("current_folder"), folder.toLocalFile().toLocal8Bit());
    options.insert(QStringLiteral("multiple"), kind == QLatin1String("open-many"));
    options.insert(QStringLiteral("directory"), kind == QLatin1String("folder"));
    options.insert(QStringLiteral("current_name"), QStringLiteral("untitled.txt"));
    const bool save = kind == QLatin1String("save");
    auto *picker = new Picker(save ? Kind::SaveFile : Kind::OpenFile, save ? QStringLiteral("Save As") : QStringLiteral("Open"), options, parent);
    if (!picker->folders()) {
        picker->setFilters({{QStringLiteral("Text files"), {QStringLiteral("*.txt")}, {}}, {QStringLiteral("All files"), {QStringLiteral("*")}, {}}});
    }
    return picker;
}

QStringList Picker::filterNames() const
{
    QStringList names;
    for (const Filter &filter : m_filters) {
        names << filter.name;
    }
    return names;
}

void Picker::setFilter(int filter)
{
    if (filter == m_filter || filter < 0 || filter >= m_filters.size()) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
}

void Picker::setFilters(const QList<Filter> &filters, int current)
{
    m_filters = filters;
    m_filter = current >= 0 && current < filters.size() ? current : 0;
    Q_EMIT filterChanged();
}

QStringList Picker::globs() const
{
    return m_filter < m_filters.size() ? m_filters.at(m_filter).globs : QStringList();
}

QStringList Picker::mimes() const
{
    return m_filter < m_filters.size() ? m_filters.at(m_filter).mimes : QStringList();
}

QStringList Picker::answerFor(const QList<QUrl> &urls) const
{
    QStringList uris;
    if (m_kind == Kind::SaveFiles) {
        if (!urls.isEmpty()) {
            const QDir folder(urls.constFirst().toLocalFile());
            for (const QString &file : m_files) {
                uris << QUrl::fromLocalFile(folder.filePath(QFileInfo(file).fileName())).toString(QUrl::FullyEncoded);
            }
        }
        return uris;
    }
    for (const QUrl &url : urls) {
        uris << url.toString(QUrl::FullyEncoded);
    }
    return uris;
}

void Picker::accept(const QList<QUrl> &urls)
{
    if (!urls.isEmpty()) {
        finish(Chosen, answerFor(urls));
    }
}

void Picker::cancel()
{
    finish(Cancelled, {});
}

void Picker::end()
{
    finish(Ended, {});
}

void Picker::finish(Response response, const QStringList &uris)
{
    if (m_done) {
        return;
    }
    m_done = true;
    Q_EMIT finished(response, uris);
}

FileChooserAdaptor::FileChooserAdaptor(App *app)
    : QDBusAbstractAdaptor(app)
    , m_app(app)
{
}

void FileChooserAdaptor::ask(Picker::Kind kind, const QDBusObjectPath &handle, const QString &title, const QVariantMap &options, const QDBusMessage &message)
{
    message.setDelayedReply(true);
    auto *picker = new Picker(kind, title, options, m_app);
    new PickRequestAdaptor(picker);
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString path = handle.path();
    bus.registerObject(path, picker);
    connect(picker, &Picker::finished, m_app, [message, path](uint response, const QStringList &uris) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(path);
        QVariantMap results;
        if (response == Picker::Chosen) {
            results.insert(QStringLiteral("uris"), uris);
        }
        bus.send(message.createReply({QVariant::fromValue(response), results}));
        // The window that answered may be the last one, and the app ends with it:
        // a round trip to the bus first, so the answer is on its way before that.
        bus.interface()->isServiceRegistered(QStringLiteral("org.freedesktop.DBus"));
    });
    m_app->pick(picker);
}

uint FileChooserAdaptor::OpenFile(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title, const QVariantMap &options,
                                  const QDBusMessage &message, QVariantMap &)
{
    ask(Picker::Kind::OpenFile, handle, title, options, message);
    return 0;
}

uint FileChooserAdaptor::SaveFile(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title, const QVariantMap &options,
                                  const QDBusMessage &message, QVariantMap &)
{
    ask(Picker::Kind::SaveFile, handle, title, options, message);
    return 0;
}

uint FileChooserAdaptor::SaveFiles(const QDBusObjectPath &handle, const QString &, const QString &, const QString &title, const QVariantMap &options,
                                   const QDBusMessage &message, QVariantMap &)
{
    ask(Picker::Kind::SaveFiles, handle, title, options, message);
    return 0;
}

PickRequestAdaptor::PickRequestAdaptor(Picker *picker)
    : QDBusAbstractAdaptor(picker)
    , m_picker(picker)
{
}

void PickRequestAdaptor::Close()
{
    m_picker->end();
}
