#include "app.h"

#include "exporter.h"
#include "io.h"
#include "render.h"
#include "store.h"
#include "svg.h"
#include "templates.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPainter>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUuid>

App *App::s_app = nullptr;

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_DIBUJO_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
    , m_editor(new Editor(this))
    , m_watcher(new QFileSystemWatcher(this))
    , m_autosave(new QTimer(this))
    , m_shotPath(shotPath)
{
    s_app = this;

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &App::refreshDark);
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &App::refreshDark);
    // The bar rewrites its state file by rename, which drops a file watch:
    // watching the directory catches the replacement.
    const QString barDir = QFileInfo(barStatePath()).absolutePath();
    if (QFileInfo(barDir).isDir()) {
        m_watcher->addPath(barDir);
    }
    refreshDark();

    connect(m_editor, &Editor::said, this, &App::said);
    // Unsaved work is copied aside a little after it stops changing.
    m_autosave->setSingleShot(true);
    m_autosave->setInterval(4000);
    connect(m_autosave, &QTimer::timeout, this, &App::autosave);
    connect(m_editor, &Editor::changed, this, [this] {
        if (m_open && !testing()) {
            m_autosave->start();
        }
    });
}

App::~App()
{
    s_app = nullptr;
    delete m_store;
}

App *App::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_app, QJSEngine::CppOwnership);
    return s_app;
}

void App::setTest(const QStringList &actions, const QSize &size)
{
    m_testActions = actions;
    m_testSize = size;
}

void App::saveShot(QQuickWindow *window)
{
    const bool saved = window && window->grabWindow().save(m_shotPath);
    QCoreApplication::exit(saved ? 0 : 1);
}

// --- The drawing and its file ---------------------------------------------------

QVariantList App::templates() const
{
    return Templates::list();
}

QVariantList App::formSections() const
{
    QVariantList sections;
    QString current;
    QVariantList forms;
    const auto flush = [&] {
        if (!forms.isEmpty()) {
            sections << QVariantMap{{QStringLiteral("section"), current}, {QStringLiteral("forms"), forms}};
        }
        forms.clear();
    };
    for (const QVariant &v : Geometry::forms()) {
        const QVariantMap form = v.toMap();
        const QString section = form.value(QStringLiteral("section")).toString();
        if (section != current) {
            flush();
            current = section;
        }
        forms << form;
    }
    flush();
    return sections;
}

QVariantList App::framePresets() const
{
    return Geometry::framePresets();
}

void App::adopt(const Document &doc, const QString &path)
{
    m_editor->setDocument(doc);
    m_path = path;
    m_open = true;
    Q_EMIT documentChanged();
    Q_EMIT versionsChanged();
}

void App::newDocument(const QString &templateKey)
{
    adopt(Templates::make(templateKey), QString());
}

bool App::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        Q_EMIT said(QStringLiteral("«%1» no existe.").arg(info.fileName()));
        return false;
    }
    QString error;
    if (info.suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0) {
        QFile file(path);
        QList<Shape> shapes;
        QMap<QString, QByteArray> resources;
        if (!file.open(QIODevice::ReadOnly) || !Svg::read(file.readAll(), &shapes, &resources, &error, info.absolutePath())) {
            Q_EMIT said(QStringLiteral("«%1» no se pudo leer: %2.").arg(info.fileName(), error.isEmpty() ? QStringLiteral("no se puede abrir") : error));
            return false;
        }
        Document doc = Templates::make(QStringLiteral("blank"));
        doc.title = info.completeBaseName();
        adopt(doc, QString());
        m_editor->insert(shapes, resources, QStringLiteral("Importar SVG"), false);
        m_editor->clearSelection();
        m_editor->fitAll();
        // What came in is the starting point, not a change to save.
        m_editor->markSaved();
        return true;
    }
    Document doc;
    if (!Io::load(path, &doc, &error)) {
        Q_EMIT said(QStringLiteral("«%1» no se pudo abrir: %2.").arg(info.fileName(), error));
        return false;
    }
    adopt(doc, info.absoluteFilePath());
    remember(m_path);
    return true;
}

void App::openInNewWindow(const QString &path)
{
    QStringList arguments;
    if (!path.isEmpty()) {
        arguments << path;
    }
    QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

bool App::save()
{
    if (m_path.isEmpty()) {
        return false;
    }
    QString error;
    if (!Io::save(m_editor->document(), m_path, &error)) {
        Q_EMIT said(QStringLiteral("No se pudo guardar: %1.").arg(error));
        return true;
    }
    m_editor->markSaved();
    // Each save by hand leaves a version behind.
    Io::addVersion(m_dataDir, m_editor->document());
    Io::dropRecovery(m_dataDir, m_editor->document().uid);
    remember(m_path);
    Q_EMIT versionsChanged();
    Q_EMIT said(QStringLiteral("Guardado en «%1».").arg(QFileInfo(m_path).fileName()));
    return true;
}

bool App::saveAs(const QUrl &url)
{
    QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QLatin1String(".wdib"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".wdib");
    }
    const QString before = m_path;
    m_path = QFileInfo(path).absoluteFilePath();
    if (m_editor->title().isEmpty() || m_editor->title() == QLatin1String("Sin título")) {
        m_editor->setTitle(QFileInfo(path).completeBaseName());
    }
    QString error;
    if (!Io::save(m_editor->document(), m_path, &error)) {
        m_path = before;
        Q_EMIT said(QStringLiteral("No se pudo guardar: %1.").arg(error));
        return false;
    }
    Q_EMIT documentChanged();
    return save();
}

void App::closeDocument()
{
    if (m_open) {
        Io::dropRecovery(m_dataDir, m_editor->document().uid);
    }
    m_open = false;
    m_path.clear();
    m_editor->setDocument(Document());
    Q_EMIT documentChanged();
    Q_EMIT recentsChanged();
}

void App::autosave()
{
    if (!m_open) {
        return;
    }
    if (m_editor->dirty()) {
        Io::writeRecovery(m_dataDir, m_editor->document(), m_path);
    } else {
        Io::dropRecovery(m_dataDir, m_editor->document().uid);
    }
}

static QString whenText(const QDateTime &when)
{
    const QDate today = QDate::currentDate();
    const QString time = QLocale(QLocale::Spanish).toString(when.time(), QStringLiteral("H:mm"));
    if (when.date() == today) {
        return QStringLiteral("Hoy, %1").arg(time);
    }
    if (when.date() == today.addDays(-1)) {
        return QStringLiteral("Ayer, %1").arg(time);
    }
    return QLocale(QLocale::Spanish).toString(when.date(), QStringLiteral("d MMM yyyy")) + QStringLiteral(", ") + time;
}

void App::remember(const QString &path)
{
    QJsonArray list = m_store->value(QStringLiteral("recents")).toArray();
    QJsonArray next;
    next.append(path);
    for (const QJsonValue &value : list) {
        if (value.toString() != path && next.size() < 12) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), next);
    Q_EMIT recentsChanged();
}

void App::forgetRecent(const QString &path)
{
    QJsonArray next;
    for (const QJsonValue &value : m_store->value(QStringLiteral("recents")).toArray()) {
        if (value.toString() != path) {
            next.append(value);
        }
    }
    m_store->set(QStringLiteral("recents"), next);
    Q_EMIT recentsChanged();
}

QVariantList App::recents() const
{
    QVariantList list;
    for (const QJsonValue &value : m_store->value(QStringLiteral("recents")).toArray()) {
        const QFileInfo info(value.toString());
        if (!info.isFile()) {
            continue;
        }
        QString folder = info.absolutePath();
        if (folder.startsWith(QDir::homePath())) {
            folder = QStringLiteral("~") + folder.mid(QDir::homePath().size());
        }
        list << QVariantMap{
            {QStringLiteral("path"), info.absoluteFilePath()},
            {QStringLiteral("name"), info.completeBaseName()},
            {QStringLiteral("folder"), folder},
            {QStringLiteral("when"), whenText(info.lastModified())},
        };
    }
    return list;
}

QVariantList App::versions() const
{
    QVariantList list;
    if (!m_open) {
        return list;
    }
    for (const Io::Version &version : Io::versions(m_dataDir, m_editor->document().uid)) {
        list << QVariantMap{{QStringLiteral("path"), version.path}, {QStringLiteral("label"), whenText(version.when)}};
    }
    return list;
}

void App::restoreVersion(const QString &path)
{
    Document doc;
    QString error;
    if (!Io::load(path, &doc, &error)) {
        Q_EMIT said(QStringLiteral("Esa versión no se pudo leer: %1.").arg(error));
        return;
    }
    // One step, so that undo brings back what there was.
    m_editor->edit(QStringLiteral("Restaurar versión"), [&doc](Document &current) {
        const QString uid = current.uid;
        current = doc;
        current.uid = uid;
    });
    m_editor->clearSelection();
    m_editor->fitAll();
    Q_EMIT said(QStringLiteral("Versión restaurada; Deshacer vuelve a la de antes."));
}

QVariantList App::recoveries() const
{
    QVariantList list;
    for (const Io::Recovery &recovery : Io::recoveries(m_dataDir)) {
        list << QVariantMap{
            {QStringLiteral("path"), recovery.path},
            {QStringLiteral("title"), recovery.title.isEmpty() ? QStringLiteral("Sin título") : recovery.title},
            {QStringLiteral("when"), whenText(recovery.when)},
        };
    }
    return list;
}

void App::recover(const QString &path)
{
    Document doc;
    QString error;
    if (!Io::load(path, &doc, &error)) {
        Q_EMIT said(QStringLiteral("La copia no se pudo leer: %1.").arg(error));
        return;
    }
    QString origin;
    for (const Io::Recovery &recovery : Io::recoveries(m_dataDir)) {
        if (recovery.path == path) {
            origin = recovery.origin;
        }
    }
    adopt(doc, origin);
    // Recovered work is unsaved work.
    m_editor->touch();
    Q_EMIT recoveriesChanged();
}

void App::dropRecoveries()
{
    for (const Io::Recovery &recovery : Io::recoveries(m_dataDir)) {
        QFile::remove(recovery.path);
    }
    Q_EMIT recoveriesChanged();
}

// --- In and out -----------------------------------------------------------------

bool App::importSvg(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    QFile file(info.filePath());
    QList<Shape> shapes;
    QMap<QString, QByteArray> resources;
    QString error;
    if (!file.open(QIODevice::ReadOnly) || !Svg::read(file.readAll(), &shapes, &resources, &error, info.absolutePath())) {
        Q_EMIT said(QStringLiteral("«%1» no se pudo leer: %2.").arg(info.fileName(), error.isEmpty() ? QStringLiteral("no se puede abrir") : error));
        return false;
    }
    if (shapes.isEmpty()) {
        Q_EMIT said(QStringLiteral("«%1» no tiene nada que se pueda traer.").arg(info.fileName()));
        return false;
    }
    m_editor->insert(shapes, resources, QStringLiteral("Importar SVG"));
    Q_EMIT said(shapes.size() == 1 ? QStringLiteral("1 objeto importado.") : QStringLiteral("%1 objetos importados.").arg(shapes.size()));
    return true;
}

bool App::insertImage(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > (64 << 20)) {
        Q_EMIT said(QStringLiteral("«%1» no se pudo leer.").arg(info.fileName()));
        return false;
    }
    if (m_editor->addImage(file.readAll(), info.suffix().toLower()).isEmpty()) {
        Q_EMIT said(QStringLiteral("«%1» no es una imagen que se pueda leer.").arg(info.fileName()));
        return false;
    }
    return true;
}

bool App::exportTo(const QUrl &url, const QString &format, const QString &frame, double scale, bool transparent)
{
    QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QLatin1Char('.') + format, Qt::CaseInsensitive)) {
        path += QLatin1Char('.') + format;
    }
    const Document &doc = m_editor->document();
    const QRectF area = Exporter::area(doc, frame);
    QString error;
    bool done = false;
    if (format == QLatin1String("png")) {
        done = Exporter::png(doc, area, qBound(0.1, scale, 8.0), transparent, path, &error);
    } else if (format == QLatin1String("svg")) {
        done = Exporter::svg(doc, area, path, &error);
    } else if (format == QLatin1String("pdf")) {
        QList<QRectF> areas;
        if (frame.isEmpty()) {
            for (const Shape *s : doc.frames()) {
                areas << s->bounds();
            }
        }
        if (areas.isEmpty()) {
            areas << area;
        }
        done = Exporter::pdf(doc, areas, path, &error);
    }
    Q_EMIT said(done ? QStringLiteral("Exportado a «%1».").arg(QFileInfo(path).fileName())
                     : QStringLiteral("No se pudo exportar: %1.").arg(error.isEmpty() ? QStringLiteral("formato desconocido") : error));
    return done;
}

QUrl App::suggestedUrl(const QString &suffix) const
{
    QString folder = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString name = m_editor->title().trimmed();
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    if (name.isEmpty()) {
        name = QStringLiteral("Sin título");
    }
    return QUrl::fromLocalFile(folder + QLatin1Char('/') + name + QLatin1Char('.') + suffix);
}

QString App::fileName(const QString &path) const
{
    return QFileInfo(path).fileName();
}

// --- The desktop ----------------------------------------------------------------

QString App::barStatePath() const
{
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty()) {
        state = QDir::homePath() + QStringLiteral("/.local/state");
    }
    return state + QStringLiteral("/wonderlandwm/bar.json");
}

void App::setAppearance(Appearance appearance)
{
    m_appearance = appearance;
    refreshDark();
}

void App::refreshDark()
{
    bool dark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    bool glass = desktop.contains(QLatin1String("driftwm")) || desktop.contains(QLatin1String("wonderland"));
    // WonderlandWM's bar owns the light/dark and glass switches for the whole desktop.
    QFile file(barStatePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        const QString mode = state.value(QLatin1String("themeMode")).toString();
        if (!mode.isEmpty()) {
            dark = mode == QLatin1String("dark");
        }
        glass = glass && state.value(QLatin1String("glass")).toBool(true);
    }
    if (m_appearance != Appearance::Auto) {
        dark = m_appearance == Appearance::Dark;
    }
    if (qEnvironmentVariableIsSet("WONDERLAND_DIBUJO_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_DIBUJO_GLASS") != 0;
    }
    if (dark != m_dark || glass != m_glass) {
        m_dark = dark;
        m_glass = glass;
        Q_EMIT darkChanged();
    }
}

QVariant App::setting(const QString &key, const QVariant &fallback) const
{
    const QJsonValue value = m_store->value(key);
    return value.isUndefined() || value.isNull() ? fallback : value.toVariant();
}

void App::setSetting(const QString &key, const QVariant &value)
{
    m_store->set(key, QJsonValue::fromVariant(value));
}

// --- Thumbnails -----------------------------------------------------------------

ThumbProvider::ThumbProvider(App *app)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_app(app)
{
}

QImage ThumbProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize wanted = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(360, 240);
    const QString what = id.section(QLatin1Char('/'), 0, 0);
    // A `?…` on the end only makes the address new, to paint again.
    const QString rest = id.section(QLatin1Char('/'), 1).section(QLatin1Char('?'), 0, 0);
    QImage image;
    if (what == QLatin1String("template")) {
        image = Render::thumbnail(Templates::make(rest), wanted, QColor(Qt::transparent), true);
    } else if (what == QLatin1String("file")) {
        image = Io::preview(rest);
    } else if (what == QLatin1String("form")) {
        // form/<form>/<sides>/<radius>/<colour>: one of the library, as a line drawing.
        const QStringList parts = rest.split(QLatin1Char('/'));
        Shape s;
        s.kind = QStringLiteral("shape");
        s.form = parts.value(0);
        s.sides = parts.value(1).toInt();
        const double side = qMin(wanted.width(), wanted.height());
        const bool wide = s.form != QLatin1String("ellipse") && s.form != QLatin1String("star") && s.form != QLatin1String("polygon")
            && s.form != QLatin1String("heart") && s.form != QLatin1String("cross");
        s.w = side - 6;
        s.h = wide ? s.w * 0.68 : s.w;
        s.style.radius = parts.value(2).toDouble() * side / 110;
        QColor ink(QLatin1Char('#') + parts.value(3));
        image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate((wanted.width() - s.w) / 2, (wanted.height() - s.h) / 2);
        QColor wash = ink;
        wash.setAlphaF(0.10f);
        painter.setBrush(wash);
        painter.setPen(QPen(ink, side / 26, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(Geometry::outline(s));
    }
    if (image.isNull()) {
        image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size) {
        *size = image.size();
    }
    return image;
}
