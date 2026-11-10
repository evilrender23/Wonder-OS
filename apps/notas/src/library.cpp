#include "library.h"

#include "search.h"
#include "store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QUuid>
#include <QColor>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

// Day and month names follow the language of the texts, not the system's.
static QLocale dateLocale()
{
    //: Locale whose day and month names go with this language.
    return QLocale(Library::tr("es_ES"));
}

static const int formatVersion = 1;

QDateTime Library::now()
{
    const QString fixed = qEnvironmentVariable("WONDERLAND_NOTAS_NOW");
    if (!fixed.isEmpty()) {
        const QDateTime when = QDateTime::fromString(fixed, Qt::ISODate);
        if (when.isValid()) {
            return when;
        }
    }
    QDateTime current = QDateTime::currentDateTime();
    // Seconds are all a note needs, and what the files keep.
    current.setTime(QTime(current.time().hour(), current.time().minute(), current.time().second()));
    return current;
}

QStringList Library::sectionColors()
{
    return {QStringLiteral("#8e44d6"), QStringLiteral("#0a84ff"), QStringLiteral("#34c759"), QStringLiteral("#ff9f0a"),
            QStringLiteral("#ff453a"), QStringLiteral("#ff375f"), QStringLiteral("#30b0c7"), QStringLiteral("#8e8e93")};
}

Library::Library(const QString &dir, QObject *parent)
    : QObject(parent)
    , m_dir(dir)
{
    load();
}

QString Library::newId() const
{
    QString id;
    do {
        id = QUuid::createUuid().toString(QUuid::Id128).left(12);
    } while (m_pages.contains(id));
    return id;
}

void Library::touch()
{
    ++m_revision;
    Q_EMIT changed();
}

// --- Disk ---------------------------------------------------------------------------

static QString stamp(const QDateTime &when)
{
    return when.isValid() ? when.toString(Qt::ISODate) : QString();
}

static QJsonObject infoToJson(const PageInfo &info)
{
    QJsonObject o{
        {QLatin1String("v"), formatVersion},
        {QLatin1String("id"), info.id},
        {QLatin1String("section"), info.section},
        {QLatin1String("title"), info.title},
        {QLatin1String("created"), stamp(info.created)},
        {QLatin1String("modified"), stamp(info.modified)},
    };
    if (!info.parent.isEmpty()) o.insert(QLatin1String("parent"), info.parent);
    if (!info.tags.isEmpty()) o.insert(QLatin1String("tags"), QJsonArray::fromStringList(info.tags));
    if (info.pinned) o.insert(QLatin1String("pinned"), true);
    if (info.trashed.isValid()) o.insert(QLatin1String("trashed"), stamp(info.trashed));
    return o;
}

static PageInfo infoFromJson(const QJsonObject &o)
{
    PageInfo info;
    info.id = o.value(QLatin1String("id")).toString();
    info.section = o.value(QLatin1String("section")).toString();
    info.parent = o.value(QLatin1String("parent")).toString();
    info.title = o.value(QLatin1String("title")).toString();
    for (const QJsonValue &tag : o.value(QLatin1String("tags")).toArray()) {
        info.tags << tag.toString();
    }
    info.pinned = o.value(QLatin1String("pinned")).toBool();
    info.created = QDateTime::fromString(o.value(QLatin1String("created")).toString(), Qt::ISODate);
    info.modified = QDateTime::fromString(o.value(QLatin1String("modified")).toString(), Qt::ISODate);
    info.trashed = QDateTime::fromString(o.value(QLatin1String("trashed")).toString(), Qt::ISODate);
    return info;
}

QString Library::pageDir(const QString &id) const
{
    return m_dir + QStringLiteral("/pages/") + id;
}

void Library::load()
{
    const QJsonObject index = Store::read(m_dir + QStringLiteral("/library.json"));
    for (const QJsonValue &value : index.value(QLatin1String("notebooks")).toArray()) {
        const QJsonObject o = value.toObject();
        Notebook notebook{o.value(QLatin1String("id")).toString(), o.value(QLatin1String("name")).toString(), {}};
        for (const QJsonValue &inner : o.value(QLatin1String("sections")).toArray()) {
            const QJsonObject s = inner.toObject();
            notebook.sections << Section{s.value(QLatin1String("id")).toString(), s.value(QLatin1String("name")).toString(), s.value(QLatin1String("color")).toString()};
        }
        if (!notebook.id.isEmpty()) {
            m_notebooks << notebook;
        }
    }
    const QDir pages(m_dir + QStringLiteral("/pages"));
    for (const QString &name : pages.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QJsonObject o = Store::read(pages.filePath(name) + QStringLiteral("/page.json"));
        PageInfo info = infoFromJson(o);
        // The directory says which page it is, whatever the file claims.
        if (o.isEmpty() || info.id != name) {
            continue;
        }
        info.text = Doc::fromJson(o.value(QLatin1String("doc")).toObject()).toText();
        m_pages.insert(info.id, info);
    }
    // Pages whose parent is gone are pages of their own.
    for (PageInfo &info : m_pages) {
        if (!info.parent.isEmpty() && (!m_pages.contains(info.parent) || m_pages.value(info.parent).section != info.section)) {
            info.parent.clear();
        }
    }
    if (m_notebooks.isEmpty()) {
        const QString notebook = addNotebook(tr("Mi cuaderno"));
        addSection(notebook, tr("General"));
    }
}

void Library::saveIndex()
{
    QJsonArray notebooks;
    for (const Notebook &notebook : std::as_const(m_notebooks)) {
        QJsonArray sections;
        for (const Section &section : notebook.sections) {
            sections << QJsonObject{{QLatin1String("id"), section.id}, {QLatin1String("name"), section.name}, {QLatin1String("color"), section.color}};
        }
        notebooks << QJsonObject{{QLatin1String("id"), notebook.id}, {QLatin1String("name"), notebook.name}, {QLatin1String("sections"), sections}};
    }
    Store::write(m_dir + QStringLiteral("/library.json"), {{QLatin1String("v"), formatVersion}, {QLatin1String("notebooks"), notebooks}});
}

bool Library::savePage(const PageInfo &info, const Doc &doc)
{
    QJsonObject o = infoToJson(info);
    o.insert(QLatin1String("doc"), doc.toJson());
    return Store::write(pageDir(info.id) + QStringLiteral("/page.json"), o);
}

bool Library::saveInfo(const PageInfo &info)
{
    const QString path = pageDir(info.id) + QStringLiteral("/page.json");
    QJsonObject o = infoToJson(info);
    o.insert(QLatin1String("doc"), Store::read(path).value(QLatin1String("doc")));
    return Store::write(path, o);
}

Doc Library::doc(const QString &id) const
{
    return Doc::fromJson(Store::read(pageDir(id) + QStringLiteral("/page.json")).value(QLatin1String("doc")).toObject());
}

bool Library::setDoc(const QString &id, const Doc &doc, bool keepDate)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || this->doc(id) == doc) {
        return false;
    }
    if (!keepDate) {
        it->modified = now();
    }
    it->text = doc.toText();
    savePage(*it, doc);
    touch();
    return true;
}

// --- Notebooks and sections -----------------------------------------------------------

Library::Section *Library::findSection(const QString &id, Notebook **owner)
{
    for (Notebook &notebook : m_notebooks) {
        for (Section &section : notebook.sections) {
            if (section.id == id) {
                if (owner) *owner = &notebook;
                return &section;
            }
        }
    }
    return nullptr;
}

const Library::Section *Library::findSection(const QString &id, const Notebook **owner) const
{
    for (const Notebook &notebook : m_notebooks) {
        for (const Section &section : notebook.sections) {
            if (section.id == id) {
                if (owner) *owner = &notebook;
                return &section;
            }
        }
    }
    return nullptr;
}

QVariantList Library::notebooks() const
{
    QHash<QString, int> counts;
    for (const PageInfo &info : m_pages) {
        if (!info.trashed.isValid()) {
            ++counts[info.section];
        }
    }
    QVariantList list;
    for (const Notebook &notebook : m_notebooks) {
        QVariantList sections;
        for (const Section &section : notebook.sections) {
            sections << QVariantMap{{QStringLiteral("id"), section.id}, {QStringLiteral("name"), section.name},
                                    {QStringLiteral("color"), section.color}, {QStringLiteral("count"), counts.value(section.id)}};
        }
        list << QVariantMap{{QStringLiteral("id"), notebook.id}, {QStringLiteral("name"), notebook.name}, {QStringLiteral("sections"), sections}};
    }
    return list;
}

QString Library::addNotebook(const QString &name)
{
    Notebook notebook{newId(), name.trimmed().isEmpty() ? tr("Cuaderno") : name.trimmed(), {}};
    m_notebooks << notebook;
    saveIndex();
    touch();
    return notebook.id;
}

void Library::renameNotebook(const QString &id, const QString &name)
{
    for (Notebook &notebook : m_notebooks) {
        if (notebook.id == id && !name.trimmed().isEmpty() && notebook.name != name.trimmed()) {
            notebook.name = name.trimmed();
            saveIndex();
            touch();
        }
    }
}

bool Library::removeNotebook(const QString &id)
{
    if (m_notebooks.size() < 2) {
        return false;
    }
    for (int i = 0; i < m_notebooks.size(); ++i) {
        if (m_notebooks.at(i).id != id) {
            continue;
        }
        const QDateTime when = now();
        for (const Section &section : m_notebooks.at(i).sections) {
            for (const QString &page : pageIds(section.id)) {
                trashOne(page, when);
            }
        }
        m_notebooks.removeAt(i);
        saveIndex();
        touch();
        return true;
    }
    return false;
}

QString Library::addSection(const QString &notebook, const QString &name, const QString &color)
{
    for (Notebook &owner : m_notebooks) {
        if (owner.id != notebook) {
            continue;
        }
        int total = 0;
        for (const Notebook &each : std::as_const(m_notebooks)) {
            total += each.sections.size();
        }
        const QStringList colors = sectionColors();
        Section section{newId(), name.trimmed().isEmpty() ? tr("Sección") : name.trimmed(),
                        QColor::isValidColorName(color) ? color : colors.at(total % colors.size())};
        owner.sections << section;
        saveIndex();
        touch();
        return section.id;
    }
    return {};
}

void Library::renameSection(const QString &id, const QString &name)
{
    Section *section = findSection(id);
    if (section && !name.trimmed().isEmpty() && section->name != name.trimmed()) {
        section->name = name.trimmed();
        saveIndex();
        touch();
    }
}

void Library::setSectionColor(const QString &id, const QString &color)
{
    Section *section = findSection(id);
    if (section && QColor::isValidColorName(color) && section->color != color) {
        section->color = color;
        saveIndex();
        touch();
    }
}

bool Library::removeSection(const QString &id)
{
    Notebook *owner = nullptr;
    if (!findSection(id, &owner)) {
        return false;
    }
    const QDateTime when = now();
    for (const QString &page : pageIds(id)) {
        trashOne(page, when);
    }
    owner->sections.removeIf([&id](const Section &section) { return section.id == id; });
    saveIndex();
    touch();
    return true;
}

QVariantMap Library::section(const QString &id) const
{
    const Notebook *owner = nullptr;
    const Section *section = findSection(id, &owner);
    if (!section) {
        return {};
    }
    return {{QStringLiteral("id"), section->id}, {QStringLiteral("name"), section->name}, {QStringLiteral("color"), section->color},
            {QStringLiteral("notebook"), owner->id}, {QStringLiteral("notebookName"), owner->name}};
}

QVariantMap Library::notebook(const QString &id) const
{
    for (const Notebook &notebook : m_notebooks) {
        if (notebook.id == id) {
            return {{QStringLiteral("id"), notebook.id}, {QStringLiteral("name"), notebook.name}, {QStringLiteral("sections"), int(notebook.sections.size())}};
        }
    }
    return {};
}

QString Library::firstSection() const
{
    for (const Notebook &notebook : m_notebooks) {
        if (!notebook.sections.isEmpty()) {
            return notebook.sections.first().id;
        }
    }
    return {};
}

QString Library::quickSection()
{
    // The section keeps the name it was made with: one made in Spanish is still the one.
    static const QString original = QStringLiteral("Notas rápidas");
    const QString name = tr("Notas rápidas");
    for (const Notebook &notebook : std::as_const(m_notebooks)) {
        for (const Section &section : notebook.sections) {
            if (section.name == name || section.name == original) {
                return section.id;
            }
        }
    }
    return addSection(m_notebooks.first().id, name, QStringLiteral("#ff9f0a"));
}

// --- Pages --------------------------------------------------------------------------

QString Library::dateText(const QDateTime &when, const QDateTime &now)
{
    if (!when.isValid()) {
        return {};
    }
    const QLocale locale = dateLocale();
    const qint64 days = when.date().daysTo(now.date());
    if (days == 0) {
        return when.toString(QStringLiteral("HH:mm"));
    }
    if (days == 1) {
        return tr("Ayer");
    }
    if (days > 1 && days < 7) {
        return locale.dayName(when.date().dayOfWeek(), QLocale::LongFormat);
    }
    return when.date().year() == now.date().year() ? locale.toString(when.date(), tr("d MMM")) : locale.toString(when.date(), tr("d/M/yy"));
}

static QString excerptOf(const QString &text)
{
    QString flat = text.left(600);
    flat.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\t'), QLatin1Char(' ')).remove(QChar::ObjectReplacementCharacter);
    return flat.simplified().left(160);
}

QVariantMap Library::row(const PageInfo &info, int depth, const QString &query) const
{
    const Notebook *owner = nullptr;
    const Section *section = findSection(info.section, &owner);
    const QString title = info.title.isEmpty() ? tr("Sin título") : info.title;
    int kids = 0;
    for (const PageInfo &other : m_pages) {
        if (other.parent == info.id && other.trashed.isValid() == info.trashed.isValid()) {
            ++kids;
        }
    }
    return {
        {QStringLiteral("id"), info.id},
        {QStringLiteral("title"), query.isEmpty() ? title : Search::marked(title, query)},
        {QStringLiteral("excerpt"), query.isEmpty() ? excerptOf(info.text) : Search::snippet(info.text, query, 110)},
        {QStringLiteral("date"), dateText(info.trashed.isValid() ? info.trashed : info.modified, now())},
        {QStringLiteral("pinned"), info.pinned},
        {QStringLiteral("tags"), info.tags},
        {QStringLiteral("depth"), depth},
        {QStringLiteral("parent"), info.parent},
        {QStringLiteral("children"), kids},
        {QStringLiteral("section"), info.section},
        {QStringLiteral("sectionName"), section ? section->name : QString()},
        {QStringLiteral("notebookName"), owner ? owner->name : QString()},
        {QStringLiteral("color"), section ? section->color : QStringLiteral("#8e8e93")},
    };
}

QList<const PageInfo *> Library::children(const QString &parent) const
{
    QList<const PageInfo *> list;
    for (const PageInfo &info : m_pages) {
        if (info.parent == parent && !info.trashed.isValid()) {
            list << &info;
        }
    }
    // Subpages keep the order they were made in: they are chapters, not news.
    std::sort(list.begin(), list.end(), [](const PageInfo *a, const PageInfo *b) {
        return a->created != b->created ? a->created < b->created : a->id < b->id;
    });
    return list;
}

int Library::depth(const QString &id) const
{
    int depth = 0;
    QString at = m_pages.value(id).parent;
    while (!at.isEmpty() && depth < 8) {
        ++depth;
        at = m_pages.value(at).parent;
    }
    return depth;
}

static bool newerFirst(const PageInfo *a, const PageInfo *b)
{
    if (a->pinned != b->pinned) {
        return a->pinned;
    }
    if (a->modified != b->modified) {
        return a->modified > b->modified;
    }
    return a->id < b->id;
}

QStringList Library::pageIds(const QString &section) const
{
    QList<const PageInfo *> top;
    for (const PageInfo &info : m_pages) {
        if (info.section == section && info.parent.isEmpty() && !info.trashed.isValid()) {
            top << &info;
        }
    }
    std::sort(top.begin(), top.end(), newerFirst);
    QStringList ids;
    std::function<void(const PageInfo *)> add = [&](const PageInfo *info) {
        ids << info->id;
        for (const PageInfo *child : children(info->id)) {
            add(child);
        }
    };
    for (const PageInfo *info : std::as_const(top)) {
        add(info);
    }
    return ids;
}

QVariantList Library::pages(const QString &section, const QString &tag) const
{
    QVariantList list;
    if (tag.isEmpty()) {
        for (const QString &id : pageIds(section)) {
            list << row(m_pages.value(id), depth(id));
        }
        return list;
    }
    QList<const PageInfo *> found;
    for (const PageInfo &info : m_pages) {
        if (!info.trashed.isValid() && info.tags.contains(tag) && (section.isEmpty() || info.section == section)) {
            found << &info;
        }
    }
    std::sort(found.begin(), found.end(), newerFirst);
    for (const PageInfo *info : std::as_const(found)) {
        list << row(*info, 0);
    }
    return list;
}

QVariantList Library::trash() const
{
    QList<const PageInfo *> found;
    for (const PageInfo &info : m_pages) {
        if (info.trashed.isValid()) {
            found << &info;
        }
    }
    std::sort(found.begin(), found.end(), [](const PageInfo *a, const PageInfo *b) {
        return a->trashed != b->trashed ? a->trashed > b->trashed : a->id < b->id;
    });
    QVariantList list;
    for (const PageInfo *info : std::as_const(found)) {
        list << row(*info, 0);
    }
    return list;
}

int Library::trashCount() const
{
    return int(std::count_if(m_pages.begin(), m_pages.end(), [](const PageInfo &info) { return info.trashed.isValid(); }));
}

QVariantList Library::search(const QString &query, const QString &tag) const
{
    QVariantList list;
    if (Search::terms(query).isEmpty()) {
        return list;
    }
    QList<const PageInfo *> found;
    for (const PageInfo &info : m_pages) {
        if (!info.trashed.isValid() && (tag.isEmpty() || info.tags.contains(tag)) && Search::matches(info.title + QLatin1Char('\n') + info.text, query)) {
            found << &info;
        }
    }
    // What says it in the title first, then the latest.
    std::sort(found.begin(), found.end(), [&query](const PageInfo *a, const PageInfo *b) {
        const bool inA = Search::matches(a->title, query);
        const bool inB = Search::matches(b->title, query);
        if (inA != inB) {
            return inA;
        }
        return a->modified != b->modified ? a->modified > b->modified : a->id < b->id;
    });
    for (const PageInfo *info : std::as_const(found)) {
        list << row(*info, 0, query);
    }
    return list;
}

QVariantList Library::tags() const
{
    QMap<QString, int> counts;
    for (const PageInfo &info : m_pages) {
        if (!info.trashed.isValid()) {
            for (const QString &tag : info.tags) {
                ++counts[tag];
            }
        }
    }
    QVariantList list;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        list << QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("count"), it.value()}};
    }
    return list;
}

QStringList Library::parseTags(const QString &text)
{
    QStringList tags;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;#\\n]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString tag = part.simplified().toLower();
        if (!tag.isEmpty() && !tags.contains(tag)) {
            tags << tag;
        }
    }
    return tags;
}

QVariantMap Library::page(const QString &id) const
{
    const auto it = m_pages.constFind(id);
    if (it == m_pages.constEnd()) {
        return {};
    }
    const QLocale locale = dateLocale();
    const QString format = tr("d 'de' MMMM 'de' yyyy, HH:mm");
    return {
        {QStringLiteral("id"), it->id},
        {QStringLiteral("title"), it->title},
        {QStringLiteral("section"), it->section},
        {QStringLiteral("parent"), it->parent},
        {QStringLiteral("tags"), it->tags},
        {QStringLiteral("pinned"), it->pinned},
        {QStringLiteral("created"), locale.toString(it->created, format)},
        {QStringLiteral("modified"), locale.toString(it->modified, format)},
        {QStringLiteral("trashed"), it->trashed.isValid()},
        {QStringLiteral("depth"), depth(id)},
        {QStringLiteral("dir"), pageDir(id)},
    };
}

QString Library::importPage(const QString &section, const QString &parent, const QString &title, const Doc &doc, const QStringList &tags,
                            const QDateTime &created, const QDateTime &modified, bool pinned)
{
    if (!findSection(section)) {
        return {};
    }
    PageInfo info;
    info.id = newId();
    info.section = section;
    const auto owner = m_pages.constFind(parent);
    if (owner != m_pages.constEnd() && owner->section == section && !owner->trashed.isValid() && depth(parent) < 2) {
        info.parent = parent;
    }
    info.title = title.simplified();
    info.tags = tags;
    info.pinned = pinned;
    info.created = created.isValid() ? created : now();
    info.modified = modified.isValid() ? modified : info.created;
    info.text = doc.toText();
    if (!savePage(info, doc)) {
        return {};
    }
    m_pages.insert(info.id, info);
    touch();
    return info.id;
}

QString Library::addPage(const QString &section, const QString &parent, const QString &title, const QString &templateName)
{
    const QDate day = now().date();
    const QString named = title.isEmpty() ? templateTitle(templateName, day) : title;
    return importPage(section, parent, named, templateDoc(templateName, day));
}

void Library::setTitle(const QString &id, const QString &title)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->title == title.simplified()) {
        return;
    }
    it->title = title.simplified();
    it->modified = now();
    saveInfo(*it);
    touch();
    Q_EMIT pageChanged(id);
}

void Library::setTags(const QString &id, const QStringList &tags)
{
    auto it = m_pages.find(id);
    const QStringList clean = parseTags(tags.join(QLatin1Char(',')));
    if (it == m_pages.end() || it->tags == clean) {
        return;
    }
    it->tags = clean;
    saveInfo(*it);
    touch();
    Q_EMIT pageChanged(id);
}

void Library::setPinned(const QString &id, bool pinned)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->pinned == pinned) {
        return;
    }
    it->pinned = pinned;
    saveInfo(*it);
    touch();
    Q_EMIT pageChanged(id);
}

void Library::movePage(const QString &id, const QString &section)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || !findSection(section) || it->section == section) {
        return;
    }
    // Its subpages go with it, whatever their depth.
    QStringList moving{id};
    for (int i = 0; i < moving.size(); ++i) {
        for (const PageInfo &info : std::as_const(m_pages)) {
            if (info.parent == moving.at(i)) {
                moving << info.id;
            }
        }
    }
    it->parent.clear();
    for (const QString &each : std::as_const(moving)) {
        PageInfo &info = m_pages[each];
        info.section = section;
        saveInfo(info);
    }
    touch();
    Q_EMIT pageChanged(id);
}

bool Library::setParent(const QString &id, const QString &parent)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->parent == parent) {
        return false;
    }
    if (!parent.isEmpty()) {
        const auto owner = m_pages.constFind(parent);
        if (owner == m_pages.constEnd() || owner->section != it->section || owner->trashed.isValid() || depth(parent) >= 2) {
            return false;
        }
        // Not under itself, nor under one of its own.
        for (QString at = parent; !at.isEmpty(); at = m_pages.value(at).parent) {
            if (at == id) {
                return false;
            }
        }
    }
    it->parent = parent;
    saveInfo(*it);
    touch();
    Q_EMIT pageChanged(id);
    return true;
}

static void copyDir(const QString &from, const QString &to)
{
    const QDir source(from);
    if (!source.exists()) {
        return;
    }
    QDir().mkpath(to);
    for (const QString &name : source.entryList(QDir::Files)) {
        QFile::copy(source.filePath(name), to + QLatin1Char('/') + name);
    }
}

QString Library::duplicatePage(const QString &id)
{
    const auto it = m_pages.constFind(id);
    if (it == m_pages.constEnd()) {
        return {};
    }
    const QString section = it->section;
    const QString parent = it->parent;
    const QString title = (it->title.isEmpty() ? tr("Sin título") : it->title) + tr(" (copia)");
    const QStringList tags = it->tags;
    const QString copy = importPage(section, parent, title, doc(id), tags);
    if (!copy.isEmpty()) {
        copyDir(pageDir(id) + QStringLiteral("/files"), pageDir(copy) + QStringLiteral("/files"));
    }
    return copy;
}

void Library::trashOne(const QString &id, const QDateTime &when)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->trashed.isValid()) {
        return;
    }
    it->trashed = when;
    it->pinned = false;
    saveInfo(*it);
}

void Library::trashPage(const QString &id)
{
    if (!m_pages.contains(id) || m_pages.value(id).trashed.isValid()) {
        return;
    }
    const QDateTime when = now();
    QStringList going{id};
    for (int i = 0; i < going.size(); ++i) {
        for (const PageInfo &info : std::as_const(m_pages)) {
            if (info.parent == going.at(i) && !info.trashed.isValid()) {
                going << info.id;
            }
        }
    }
    for (const QString &each : std::as_const(going)) {
        trashOne(each, when);
    }
    touch();
    Q_EMIT pageChanged(id);
}

void Library::restorePage(const QString &id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || !it->trashed.isValid()) {
        return;
    }
    // Where it was; and when that is gone, somewhere it can be found.
    QString home = it->section;
    if (!findSection(home)) {
        home = firstSection();
        if (home.isEmpty()) {
            home = addSection(m_notebooks.first().id, tr("Recuperadas"));
        }
    }
    const QDateTime when = it->trashed;
    // What went with it comes back with it.
    QStringList coming{id};
    for (int i = 0; i < coming.size(); ++i) {
        for (const PageInfo &info : std::as_const(m_pages)) {
            if (info.parent == coming.at(i) && info.trashed == when) {
                coming << info.id;
            }
        }
    }
    const auto owner = m_pages.constFind(it->parent);
    if (owner == m_pages.constEnd() || owner->trashed.isValid() || owner->section != home) {
        it->parent.clear();
    }
    for (const QString &each : std::as_const(coming)) {
        PageInfo &info = m_pages[each];
        info.trashed = QDateTime();
        info.section = home;
        saveInfo(info);
    }
    touch();
    Q_EMIT pageChanged(id);
}

void Library::deletePage(const QString &id)
{
    if (!m_pages.contains(id)) {
        return;
    }
    for (PageInfo &info : m_pages) {
        if (info.parent == id) {
            info.parent.clear();
            saveInfo(info);
        }
    }
    m_pages.remove(id);
    // Only ever a directory of ours: an id is twelve hex digits.
    static const QRegularExpression safe(QStringLiteral("^[0-9a-f]{12}$"));
    if (safe.match(id).hasMatch()) {
        QDir(pageDir(id)).removeRecursively();
    }
    touch();
    Q_EMIT pageChanged(id);
}

void Library::emptyTrash()
{
    QStringList going;
    for (const PageInfo &info : std::as_const(m_pages)) {
        if (info.trashed.isValid()) {
            going << info.id;
        }
    }
    for (const QString &id : std::as_const(going)) {
        deletePage(id);
    }
}

QString Library::pageByTitle(const QString &title) const
{
    const QString wanted = Search::fold(title.simplified());
    const PageInfo *best = nullptr;
    for (const PageInfo &info : m_pages) {
        if (!info.trashed.isValid() && Search::fold(info.title) == wanted && (!best || newerFirst(&info, best))) {
            best = &info;
        }
    }
    return best ? best->id : QString();
}

QVariantList Library::backlinks(const QString &id) const
{
    const QString title = m_pages.value(id).title;
    QVariantList list;
    if (title.isEmpty()) {
        return list;
    }
    const QString wanted = Search::fold(title);
    QList<const PageInfo *> found;
    for (const PageInfo &info : m_pages) {
        if (info.id == id || info.trashed.isValid()) {
            continue;
        }
        Doc page = doc(info.id);
        bool links = false;
        page.mapPaths([&](const QString &path, bool) {
            links = links || Search::fold(Doc::wikiTitle(path)) == wanted;
            return path;
        });
        if (links) {
            found << &info;
        }
    }
    std::sort(found.begin(), found.end(), [](const PageInfo *a, const PageInfo *b) { return Search::fold(a->title) < Search::fold(b->title); });
    for (const PageInfo *info : std::as_const(found)) {
        list << QVariantMap{{QStringLiteral("id"), info->id}, {QStringLiteral("title"), info->title.isEmpty() ? tr("Sin título") : info->title}};
    }
    return list;
}

// --- Files ----------------------------------------------------------------------------

static QString cleanName(const QString &name)
{
    QString clean = QFileInfo(name).fileName();
    static const QRegularExpression unsafe(QStringLiteral("[\\x00-\\x1f/\\\\:*?\"<>|#%]"));
    clean.replace(unsafe, QStringLiteral("_"));
    while (clean.startsWith(QLatin1Char('.'))) {
        clean.remove(0, 1);
    }
    return clean.isEmpty() ? QStringLiteral("archivo") : clean.right(120);
}

QString Library::addFile(const QString &id, const QString &name, const QByteArray &data)
{
    if (!m_pages.contains(id)) {
        return {};
    }
    const QString dir = pageDir(id) + QStringLiteral("/files");
    QDir().mkpath(dir);
    const QFileInfo wanted(cleanName(name));
    QString final = wanted.fileName();
    for (int n = 2; QFileInfo::exists(dir + QLatin1Char('/') + final); ++n) {
        // The same file twice is the same file.
        QFile there(dir + QLatin1Char('/') + final);
        if (there.size() == data.size() && there.open(QIODevice::ReadOnly) && there.readAll() == data) {
            return QStringLiteral("files/") + final;
        }
        final = wanted.completeBaseName() + QLatin1Char('-') + QString::number(n) + (wanted.suffix().isEmpty() ? QString() : QLatin1Char('.') + wanted.suffix());
    }
    QSaveFile file(dir + QLatin1Char('/') + final);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        return {};
    }
    return QStringLiteral("files/") + final;
}

QString Library::addFile(const QString &id, const QString &sourcePath)
{
    QFile file(sourcePath);
    // A note is not the place for a film.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 200 * 1024 * 1024) {
        return {};
    }
    return addFile(id, QFileInfo(sourcePath).fileName(), file.readAll());
}

// --- Templates ------------------------------------------------------------------------

static Block line(Block::Type type, const QString &text = QString())
{
    Block block;
    block.type = type;
    if (!text.isEmpty()) {
        block.runs << Run{.text = text};
    }
    return block;
}

QString Library::templateTitle(const QString &name, const QDate &day)
{
    const QLocale locale = dateLocale();
    if (name == QLatin1String("reunion")) {
        return tr("Reunión del %1").arg(locale.toString(day, tr("d 'de' MMMM")));
    }
    if (name == QLatin1String("tareas")) {
        return tr("Tareas");
    }
    if (name == QLatin1String("diario")) {
        QString title = locale.toString(day, tr("dddd, d 'de' MMMM 'de' yyyy"));
        if (!title.isEmpty()) {
            title[0] = title.at(0).toUpper();
        }
        return title;
    }
    return {};
}

Doc Library::templateDoc(const QString &name, const QDate &day)
{
    Doc doc;
    const QLocale locale = dateLocale();
    if (name == QLatin1String("reunion")) {
        Block when = line(Block::Paragraph);
        when.runs << Run{.text = tr("Fecha: "), .bold = true} << Run{.text = locale.toString(day, tr("d 'de' MMMM 'de' yyyy"))};
        Block who = line(Block::Paragraph);
        who.runs << Run{.text = tr("Asistentes: "), .bold = true};
        doc.blocks << when << who << line(Block::Heading2, tr("Orden del día")) << line(Block::Number) << line(Block::Heading2, tr("Notas"))
                   << line(Block::Paragraph) << line(Block::Heading2, tr("Acuerdos")) << line(Block::Bullet)
                   << line(Block::Heading2, tr("Tareas")) << line(Block::Task);
    } else if (name == QLatin1String("tareas")) {
        doc.blocks << line(Block::Heading2, tr("Hoy")) << line(Block::Task) << line(Block::Heading2, tr("Esta semana")) << line(Block::Task)
                   << line(Block::Heading2, tr("Más adelante")) << line(Block::Task);
    } else if (name == QLatin1String("diario")) {
        doc.blocks << line(Block::Heading2, tr("Lo mejor del día")) << line(Block::Paragraph) << line(Block::Heading2, tr("Qué he hecho"))
                   << line(Block::Bullet) << line(Block::Heading2, tr("Para mañana")) << line(Block::Task);
    } else {
        doc.blocks << line(Block::Paragraph);
    }
    return doc;
}
