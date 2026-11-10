#pragma once

#include "doc.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVariant>

/// What is known of a page without opening it.
struct PageInfo {
    QString id;
    QString section;
    /// The page this one hangs from, or empty.
    QString parent;
    QString title;
    QStringList tags;
    bool pinned = false;
    QDateTime created;
    QDateTime modified;
    /// When it went to the trash; invalid while it is not there.
    QDateTime trashed;
    /// Its words, for the list and for searching.
    QString text;
};

/// The notebooks, their sections and their pages. Each page is a directory
/// under pages/ with a page.json and the files it uses; the notebooks and
/// sections are in library.json. Everything is written as it changes.
class Library : public QObject
{
    Q_OBJECT
    /// [{id, name, sections: [{id, name, color, count}]}]
    Q_PROPERTY(QVariantList notebooks READ notebooks NOTIFY changed)
    /// [{name, count}], by name.
    Q_PROPERTY(QVariantList tags READ tags NOTIFY changed)
    Q_PROPERTY(int trashCount READ trashCount NOTIFY changed)
    /// Goes up with every change: what QML bindings hang on.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit Library(const QString &dir, QObject *parent = nullptr);

    QString dir() const { return m_dir; }
    QVariantList notebooks() const;
    QVariantList tags() const;
    int trashCount() const;
    int revision() const { return m_revision; }
    /// The clock; WONDERLAND_NOTAS_NOW (an ISO date and time) stops it, for the tests.
    static QDateTime now();
    static QStringList sectionColors();

    // --- Notebooks and sections ---
    Q_INVOKABLE QString addNotebook(const QString &name);
    Q_INVOKABLE void renameNotebook(const QString &id, const QString &name);
    /// Its pages go to the trash. The last notebook stays.
    Q_INVOKABLE bool removeNotebook(const QString &id);
    Q_INVOKABLE QString addSection(const QString &notebook, const QString &name, const QString &color = QString());
    Q_INVOKABLE void renameSection(const QString &id, const QString &name);
    Q_INVOKABLE void setSectionColor(const QString &id, const QString &color);
    Q_INVOKABLE bool removeSection(const QString &id);
    /// {id, name, color, notebook, notebookName} or empty.
    Q_INVOKABLE QVariantMap section(const QString &id) const;
    Q_INVOKABLE QVariantMap notebook(const QString &id) const;
    Q_INVOKABLE QString firstSection() const;
    /// The section quick notes go to, made when there is none.
    Q_INVOKABLE QString quickSection();

    // --- Pages ---
    /// The pages of a section, as the list shows them: the pinned ones first,
    /// then the latest, each followed by its subpages. With a tag, only those
    /// that carry it (and then from every section when `section` is empty).
    /// {id, title, excerpt, date, pinned, tags, depth, parent, children, section, sectionName, color}
    Q_INVOKABLE QVariantList pages(const QString &section, const QString &tag = QString()) const;
    Q_INVOKABLE QVariantList trash() const;
    /// Every word of the query in the title or the text, whatever the case or
    /// the accents. `title` and `excerpt` come as HTML with the matches in <b>.
    Q_INVOKABLE QVariantList search(const QString &query, const QString &tag = QString()) const;
    /// {id, title, section, parent, tags, pinned, created, modified, trashed, dir} or empty.
    Q_INVOKABLE QVariantMap page(const QString &id) const;
    Q_INVOKABLE bool hasPage(const QString &id) const { return m_pages.contains(id); }
    /// `templateName`: "", "reunion", "tareas" or "diario".
    Q_INVOKABLE QString addPage(const QString &section, const QString &parent = QString(), const QString &title = QString(), const QString &templateName = QString());
    Q_INVOKABLE void setTitle(const QString &id, const QString &title);
    Q_INVOKABLE void setTags(const QString &id, const QStringList &tags);
    Q_INVOKABLE void setPinned(const QString &id, bool pinned);
    /// To another section, with its subpages; it stops being a subpage.
    Q_INVOKABLE void movePage(const QString &id, const QString &section);
    /// Makes it a subpage of `parent` (in the same section), or a page again with "".
    Q_INVOKABLE bool setParent(const QString &id, const QString &parent);
    Q_INVOKABLE QString duplicatePage(const QString &id);
    Q_INVOKABLE void trashPage(const QString &id);
    Q_INVOKABLE void restorePage(const QString &id);
    /// For good, with its files.
    Q_INVOKABLE void deletePage(const QString &id);
    Q_INVOKABLE void emptyTrash();
    /// The page a [[link]] names: the title, whatever the case or the accents. Not the trash.
    Q_INVOKABLE QString pageByTitle(const QString &title) const;
    /// The pages that link to this one: [{id, title}].
    Q_INVOKABLE QVariantList backlinks(const QString &id) const;
    /// "tag one, Tag Two" → ["tag one", "tag two"], without repeats.
    Q_INVOKABLE static QStringList parseTags(const QString &text);

    Doc doc(const QString &id) const;
    /// False when nothing changed (and then nothing is written).
    /// `keepDate`: an import, which is not an edit.
    bool setDoc(const QString &id, const Doc &doc, bool keepDate = false);
    PageInfo info(const QString &id) const { return m_pages.value(id); }
    QStringList pageIds(const QString &section) const;
    QString pageDir(const QString &id) const;
    /// Copies a file into the page; answers "files/<name>", or empty.
    QString addFile(const QString &id, const QString &sourcePath);
    QString addFile(const QString &id, const QString &name, const QByteArray &data);
    /// A page written whole, as an import brings it. Answers its id.
    QString importPage(const QString &section, const QString &parent, const QString &title, const Doc &doc, const QStringList &tags = {},
                       const QDateTime &created = {}, const QDateTime &modified = {}, bool pinned = false);

    static Doc templateDoc(const QString &name, const QDate &day);
    static QString templateTitle(const QString &name, const QDate &day);
    static QString dateText(const QDateTime &when, const QDateTime &now);

Q_SIGNALS:
    void changed();
    /// The title, tags or pin of a page changed (not its text).
    void pageChanged(const QString &id);

private:
    struct Section {
        QString id;
        QString name;
        QString color;
    };
    struct Notebook {
        QString id;
        QString name;
        QList<Section> sections;
    };
    void load();
    void saveIndex();
    bool savePage(const PageInfo &info, const Doc &doc);
    bool saveInfo(const PageInfo &info);
    void touch();
    QString newId() const;
    Section *findSection(const QString &id, Notebook **owner = nullptr);
    const Section *findSection(const QString &id, const Notebook **owner = nullptr) const;
    QList<const PageInfo *> children(const QString &parent) const;
    int depth(const QString &id) const;
    QVariantMap row(const PageInfo &info, int depth, const QString &query = QString()) const;
    void trashOne(const QString &id, const QDateTime &when);

    QString m_dir;
    QList<Notebook> m_notebooks;
    QHash<QString, PageInfo> m_pages;
    int m_revision = 0;
};
