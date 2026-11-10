#include "app.h"

#include "io.h"
#include "layout.h"
#include "library.h"
#include "mathfont.h"
#include "store.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUuid>

App *App::s_app = nullptr;

namespace {

struct Template {
    const char *key;
    const char *name;
    const char *detail;
    QStringList equations;
};

const QList<Template> &templateList()
{
    static const QList<Template> list = {
        {"blank", "En blanco", "Una fórmula vacía", {QString()}},
        {"sample", "Muestrario", "Fracciones, integrales, matrices", {
            QStringLiteral(R"(x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a})"),
            QStringLiteral(R"(\int_{-\infty}^{\infty} e^{-x^2} \, dx = \sqrt{\pi})"),
            QStringLiteral(R"(\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6})"),
            QStringLiteral(R"(A = \begin{pmatrix} a_{11} & a_{12} \\ a_{21} & a_{22} \end{pmatrix}, \quad \det A = a_{11} a_{22} - a_{12} a_{21})"),
            QStringLiteral(R"(f(x) = \begin{cases} x^2 & \text{si } x \geq 0 \\ -x & \text{si } x < 0 \end{cases})"),
        }},
        {"maxwell", "Ecuaciones de Maxwell", "Cuatro ecuaciones numeradas", {
            QStringLiteral(R"(\nabla \cdot \mathbf{E} = \frac{\rho}{\varepsilon_0})"),
            QStringLiteral(R"(\nabla \cdot \mathbf{B} = 0)"),
            QStringLiteral(R"(\nabla \times \mathbf{E} = -\frac{\partial \mathbf{B}}{\partial t})"),
            QStringLiteral(R"(\nabla \times \mathbf{B} = \mu_0 \mathbf{J} + \mu_0 \varepsilon_0 \frac{\partial \mathbf{E}}{\partial t})"),
        }},
        {"derivation", "Desarrollo paso a paso", "Ecuaciones alineadas", {
            QStringLiteral(R"(\begin{aligned} (a + b)^2 &= (a + b)(a + b) \\ &= a^2 + 2ab + b^2 \end{aligned})"),
        }},
    };
    return list;
}

QString whenText(const QDateTime &when)
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

/// The formats a document can be written back to without asking.
bool keeps(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("wform") || suffix == QLatin1String("odf") || suffix == QLatin1String("tex");
}

/// The equations of a document one under another, to fit `wanted`.
QImage sheet(const FormulaDoc &doc, const QColor &ink, const QSize &wanted)
{
    QList<Box> boxes;
    LayoutOptions options;
    options.family = doc.font;
    options.size = 40;
    qreal width = 1;
    qreal height = 0;
    const qreal gap = 26;
    for (const QString &latex : doc.equations) {
        if (boxes.size() == 6) {
            break;
        }
        boxes << Formula::layout(latex, options).box;
        width = qMax(width, boxes.last().width);
        height += boxes.last().height + boxes.last().depth + (boxes.size() > 1 ? gap : 0);
    }
    QImage image(wanted, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (height <= 0) {
        return image;
    }
    const qreal scale = qMin(1.0, qMin(wanted.width() / width, wanted.height() / height));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((wanted.width() - width * scale) / 2, (wanted.height() - height * scale) / 2);
    painter.scale(scale, scale);
    qreal y = 0;
    for (const Box &box : boxes) {
        Formula::paint(&painter, box, QPointF((width - box.width) / 2, y + box.height), ink);
        y += box.height + box.depth + gap;
    }
    return image;
}

}

QString App::defaultDataDir()
{
    const QString moved = qEnvironmentVariable("WONDERLAND_FORMULAS_DATA");
    return moved.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : moved;
}

App::App(const QString &dataDir, const QString &shotPath, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
    , m_store(new Store(dataDir + QStringLiteral("/settings.json")))
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

    // Unsaved work is copied aside a little after it stops changing.
    m_autosave->setSingleShot(true);
    m_autosave->setInterval(4000);
    connect(m_autosave, &QTimer::timeout, this, &App::autosave);
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

// --- The document and its file ---------------------------------------------------

void App::touched()
{
    Q_EMIT changed();
    if (m_open && !testing()) {
        m_autosave->start();
    }
}

QVariantList App::templates() const
{
    QVariantList out;
    for (const Template &t : templateList()) {
        out << QVariantMap{
            {QStringLiteral("key"), QString::fromLatin1(t.key)},
            {QStringLiteral("name"), QString::fromUtf8(t.name)},
            {QStringLiteral("detail"), QString::fromUtf8(t.detail)},
            {QStringLiteral("latex"), t.equations.first()},
        };
    }
    return out;
}

void App::adopt(FormulaDoc doc, const QString &path)
{
    if (doc.uid.isEmpty()) {
        // A file of another format carries no uid: its path stands for it, so its versions are found again.
        doc.uid = path.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                 : QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex().left(32));
    }
    m_session.setDocument(doc);
    m_path = path;
    m_open = true;
    Q_EMIT documentChanged();
    Q_EMIT changed();
    Q_EMIT versionsChanged();
}

void App::newDocument(const QString &key)
{
    FormulaDoc doc;
    doc.title = QStringLiteral("Sin título");
    doc.equations = {QString()};
    for (const Template &t : templateList()) {
        if (key == QLatin1String(t.key)) {
            doc.equations = t.equations;
            if (key != QLatin1String("blank")) {
                doc.title = QString::fromUtf8(t.name);
            }
        }
    }
    // A formula of the library, the collection's or the user's.
    for (const QVariant &shelf : Library::shelves(m_dataDir)) {
        for (const QVariant &entry : shelf.toMap().value(QStringLiteral("entries")).toList()) {
            const QVariantMap e = entry.toMap();
            if (e.value(QStringLiteral("id")).toString() == key) {
                doc.equations = {e.value(QStringLiteral("latex")).toString()};
                doc.title = e.value(QStringLiteral("name")).toString();
            }
        }
    }
    adopt(doc, QString());
}

bool App::openPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        Q_EMIT said(QStringLiteral("«%1» no existe.").arg(info.fileName()));
        return false;
    }
    FormulaDoc doc;
    QString error;
    if (!Formats::read(info.absoluteFilePath(), &doc, &error)) {
        Q_EMIT said(error.isEmpty() ? QStringLiteral("«%1» no se pudo abrir.").arg(info.fileName()) : error);
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
    if (m_path.isEmpty() || !keeps(m_path)) {
        return false;
    }
    QString error;
    if (!Formats::write(m_path, m_session.document(), &error)) {
        Q_EMIT said(error.isEmpty() ? QStringLiteral("No se pudo guardar.") : error);
        return true;
    }
    m_session.markSaved();
    // Each save by hand leaves a version behind.
    Io::addVersion(m_dataDir, m_session.document());
    Io::dropRecovery(m_dataDir, m_session.document().uid);
    remember(m_path);
    Q_EMIT changed();
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
    if (!keeps(path)) {
        path += QStringLiteral(".wform");
    }
    const QString before = m_path;
    m_path = QFileInfo(path).absoluteFilePath();
    if (title().isEmpty() || title() == QLatin1String("Sin título")) {
        m_session.setTitle(QFileInfo(path).completeBaseName());
    }
    QString error;
    if (!Formats::write(m_path, m_session.document(), &error)) {
        m_path = before;
        Q_EMIT said(error.isEmpty() ? QStringLiteral("No se pudo guardar.") : error);
        return false;
    }
    Q_EMIT documentChanged();
    return save();
}

void App::closeDocument()
{
    if (m_open) {
        Io::dropRecovery(m_dataDir, m_session.document().uid);
    }
    m_autosave->stop();
    m_open = false;
    m_path.clear();
    m_session.setDocument(FormulaDoc());
    Q_EMIT documentChanged();
    Q_EMIT changed();
    Q_EMIT recentsChanged();
    Q_EMIT recoveriesChanged();
}

void App::autosave()
{
    if (!m_open) {
        return;
    }
    if (m_session.dirty()) {
        Io::writeRecovery(m_dataDir, m_session.document(), m_path);
    } else {
        Io::dropRecovery(m_dataDir, m_session.document().uid);
    }
}

void App::remember(const QString &path)
{
    const QJsonArray list = m_store->value(QStringLiteral("recents")).toArray();
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
            {QStringLiteral("name"), info.fileName()},
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
    for (const Io::Version &version : Io::versions(m_dataDir, m_session.document().uid)) {
        list << QVariantMap{{QStringLiteral("path"), version.path}, {QStringLiteral("label"), whenText(version.when)}};
    }
    return list;
}

void App::restoreVersion(const QString &path)
{
    FormulaDoc doc;
    QString error;
    if (!Formats::read(path, &doc, &error)) {
        Q_EMIT said(QStringLiteral("Esa versión no se pudo leer."));
        return;
    }
    // One step, so that undo brings back what there was.
    m_session.replace(doc);
    touched();
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
    FormulaDoc doc;
    QString error;
    if (!Formats::read(path, &doc, &error)) {
        Q_EMIT said(QStringLiteral("La copia no se pudo leer."));
        return;
    }
    QString origin;
    for (const Io::Recovery &recovery : Io::recoveries(m_dataDir)) {
        if (recovery.path == path) {
            origin = recovery.origin;
            if (!recovery.title.isEmpty()) {
                doc.title = recovery.title;
            }
        }
    }
    adopt(doc, origin);
    // Recovered work is unsaved work.
    m_session.touch();
    Q_EMIT changed();
    Q_EMIT recoveriesChanged();
}

void App::dropRecoveries()
{
    for (const Io::Recovery &recovery : Io::recoveries(m_dataDir)) {
        QFile::remove(recovery.path);
        QFile::remove(recovery.path.chopped(6) + QStringLiteral(".json"));
    }
    Q_EMIT recoveriesChanged();
}

// --- Editing -----------------------------------------------------------------------

void App::setTitle(const QString &title)
{
    m_session.setTitle(title.trimmed());
    touched();
}

void App::setCurrent(int index)
{
    if (index != m_session.current()) {
        m_session.setCurrent(index);
        Q_EMIT changed();
    }
}

void App::setLatex(const QString &latex)
{
    if (latex != m_session.latex()) {
        m_session.setLatex(latex);
        touched();
    }
}

void App::setColor(const QString &color)
{
    m_session.setColor(color.isEmpty() || !QColor(color).isValid() ? QString() : QColor(color).name());
    touched();
}

void App::setSize(double points)
{
    m_session.setSize(points);
    touched();
}

void App::setFont(const QString &family)
{
    m_session.setFont(family);
    touched();
}

QString App::fontInUse() const
{
    return MathFont::get(m_session.document().font)->family();
}

QStringList App::fonts() const
{
    return MathFont::available();
}

QVariantList App::errors() const
{
    QVariantList out;
    const QString source = m_session.latex();
    for (const ParseError &error : Latex::parse(source).errors) {
        const QString before = source.left(error.position);
        out << QVariantMap{
            {QStringLiteral("position"), error.position},
            {QStringLiteral("length"), error.length},
            {QStringLiteral("message"), error.message},
            {QStringLiteral("line"), before.count(QLatin1Char('\n')) + 1},
            {QStringLiteral("column"), error.position - before.lastIndexOf(QLatin1Char('\n'))},
        };
    }
    return out;
}

void App::addEquation(const QString &latex)
{
    m_session.add(latex);
    touched();
}

void App::duplicateEquation()
{
    m_session.duplicate();
    touched();
}

void App::removeEquation(int index)
{
    m_session.remove(index);
    touched();
}

void App::moveEquation(int from, int to)
{
    m_session.move(from, to);
    touched();
}

void App::undo()
{
    if (m_session.undo()) {
        touched();
    }
}

void App::redo()
{
    if (m_session.redo()) {
        touched();
    }
}

// --- The palette and the library ---------------------------------------------------

QVariantList App::palette(const QString &query) const
{
    return Symbols::palette(query);
}

QVariantList App::completions(const QString &prefix) const
{
    return Symbols::completions(prefix, 8);
}

int App::matchingBrace(const QString &source, int position) const
{
    return Latex::matchingBrace(source, position);
}

QVariantList App::library(const QString &query) const
{
    return Library::shelves(m_dataDir, query);
}

void App::saveToLibrary(const QString &name)
{
    if (Library::save(m_dataDir, name, m_session.latex()).isEmpty()) {
        Q_EMIT said(QStringLiteral("No hay fórmula que guardar."));
        return;
    }
    ++m_libraryRevision;
    Q_EMIT libraryChanged();
    Q_EMIT said(QStringLiteral("Guardada en la biblioteca."));
}

void App::removeFromLibrary(const QString &id)
{
    if (Library::remove(m_dataDir, id)) {
        ++m_libraryRevision;
        Q_EMIT libraryChanged();
    }
}

// --- Out ---------------------------------------------------------------------------

QString App::textAs(const QString &kind) const
{
    if (kind == QLatin1String("mathml")) {
        return Formats::toMathML(m_session.latex());
    }
    if (kind == QLatin1String("svg")) {
        return QString::fromUtf8(Formats::toSvg(m_session.document(), m_session.current()));
    }
    if (kind == QLatin1String("starmath")) {
        return Formats::toStarMath(m_session.latex());
    }
    return m_session.latex();
}

bool App::copyAs(const QString &kind, double scale)
{
    if (m_session.latex().trimmed().isEmpty()) {
        Q_EMIT said(QStringLiteral("No hay fórmula que copiar."));
        return false;
    }
    auto *data = new QMimeData;
    QString what;
    if (kind == QLatin1String("png")) {
        const QImage image = Formats::toImage(m_session.document(), m_session.current(), qBound(0.5, scale, 8.0));
        data->setImageData(image);
        what = QStringLiteral("imagen PNG (%1 × %2)").arg(image.width()).arg(image.height());
    } else if (kind == QLatin1String("svg")) {
        const QString svg = textAs(kind);
        data->setData(QStringLiteral("image/svg+xml"), svg.toUtf8());
        data->setText(svg);
        what = QStringLiteral("SVG");
    } else if (kind == QLatin1String("mathml")) {
        const QString mathml = textAs(kind);
        data->setData(QStringLiteral("application/mathml+xml"), mathml.toUtf8());
        data->setText(mathml);
        what = QStringLiteral("MathML");
    } else {
        data->setText(m_session.latex());
        what = QStringLiteral("LaTeX");
    }
    QGuiApplication::clipboard()->setMimeData(data);
    Q_EMIT said(QStringLiteral("Copiada como %1.").arg(what));
    return true;
}

bool App::exportTo(const QUrl &url, const QString &format, double scale, bool transparent)
{
    QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QLatin1Char('.') + format, Qt::CaseInsensitive)) {
        path += QLatin1Char('.') + format;
    }
    QString error;
    bool done = false;
    if (format == QLatin1String("png")) {
        const QImage image = Formats::toImage(m_session.document(), m_session.current(), qBound(0.5, scale, 8.0), transparent ? QColor(Qt::transparent) : QColor(Qt::white));
        done = image.save(path, "PNG");
    } else {
        done = Formats::write(path, m_session.document(), &error, m_session.current(), scale);
    }
    Q_EMIT said(done ? QStringLiteral("Exportado a «%1».").arg(QFileInfo(path).fileName())
                     : (error.isEmpty() ? QStringLiteral("No se pudo exportar a «%1».").arg(QFileInfo(path).fileName()) : error));
    return done;
}

QUrl App::suggestedUrl(const QString &suffix) const
{
    const QString folder = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) : QFileInfo(m_path).absolutePath();
    QString name = title().trimmed();
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

// --- The desktop ---------------------------------------------------------------------

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
    if (qEnvironmentVariableIsSet("WONDERLAND_FORMULAS_GLASS")) {
        glass = qEnvironmentVariableIntValue("WONDERLAND_FORMULAS_GLASS") != 0;
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

// --- Pictures ------------------------------------------------------------------------

FormulaProvider::FormulaProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage FormulaProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString what = id.section(QLatin1Char('/'), 0, 0);
    const QColor ink(QLatin1Char('#') + id.section(QLatin1Char('/'), 1, 1));
    QImage image;
    if (what == QLatin1String("file")) {
        const QSize wanted = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(480, 320);
        FormulaDoc doc;
        if (Formats::read(id.section(QLatin1Char('/'), 2).section(QLatin1Char('?'), 0, 0), &doc, nullptr)) {
            image = sheet(doc, ink, wanted);
        }
    }
    if (image.isNull()) {
        image = QImage(4, 4, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size) {
        *size = image.size();
    }
    return image;
}
