#pragma once

#include "model.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>
#include <vector>

/// One step that can be taken back and done again.
class Command
{
public:
    virtual ~Command() = default;
    virtual void undo(Model::Presentation &deck) = 0;
    virtual void redo(Model::Presentation &deck) = 0;
    /// The slide to show once undone or redone.
    virtual int slideAfterUndo() const = 0;
    virtual int slideAfterRedo() const = 0;
    /// «Mover», «Eliminar la diapositiva»: what the Edit menu says after «Deshacer».
    QString text;
    /// Two steps with the same key, one right after the other, are one (a slider being dragged).
    QString mergeKey;
};

/// The open presentation: its slides, what is selected on the current one, and
/// every change the window can make to them, each of which can be undone.
class Deck : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("App.deck is the one")
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY currentChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    /// Goes up with every change: what depends on the deck binds to it.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stackChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stackChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY stackChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY stackChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY stackChanged)
    Q_PROPERTY(QString path READ path NOTIFY fileChanged)
    Q_PROPERTY(QString name READ name NOTIFY fileChanged)
    Q_PROPERTY(double width READ width NOTIFY changed)
    Q_PROPERTY(double height READ height NOTIFY changed)
    Q_PROPERTY(QString theme READ theme NOTIFY changed)
    Q_PROPERTY(QString aspect READ aspect NOTIFY changed)
    /// The alignment lines of the drag in course: [{x1, y1, x2, y2}].
    Q_PROPERTY(QVariantList guides READ guides NOTIFY guidesChanged)
    Q_PROPERTY(QString error READ error NOTIFY fileChanged)

public:
    explicit Deck(QObject *parent = nullptr);
    ~Deck() override;

    const Model::Presentation &data() const { return m_data; }
    int count() const { return int(m_data.slides.size()); }
    int current() const { return m_current; }
    void setCurrent(int index);
    QStringList selection() const { return m_selection; }
    int revision() const { return m_revision; }
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString undoText() const;
    QString redoText() const;
    bool modified() const { return m_savedAt != int(m_undo.size()); }
    QString path() const { return m_path; }
    QString name() const;
    double width() const { return m_data.width; }
    double height() const { return m_data.height; }
    QString theme() const { return m_data.theme; }
    QString aspect() const;
    QVariantList guides() const { return m_guides; }
    QString error() const { return m_error; }

    // --- Files ---
    /// A deck of one title slide.
    Q_INVOKABLE void reset(const QString &theme = QStringLiteral("blanco"), const QString &aspect = QStringLiteral("16:9"));
    /// Reads a .pptx or an .odp; false, with `error` set, when it cannot.
    Q_INVOKABLE bool load(const QString &path);
    /// Writes a .pptx or, when the name ends in it, an .odp.
    Q_INVOKABLE bool save(const QString &path);
    Q_INVOKABLE bool exportPdf(const QString &path);
    /// One PNG per slide beside `path`: name-01.png, name-02.png… The number written.
    Q_INVOKABLE int exportPng(const QString &path);

    // --- Slides ---
    /// {id, layout, hidden, notes, transition, background, builds}.
    Q_INVOKABLE QVariantMap slide(int index) const;
    Q_INVOKABLE int addSlide(const QString &layout, int at = -1);
    Q_INVOKABLE void duplicateSlide(int index);
    Q_INVOKABLE void removeSlide(int index);
    Q_INVOKABLE void moveSlide(int from, int to);
    /// layout, hidden, notes, transition or background.
    Q_INVOKABLE void setSlide(int index, const QString &key, const QVariant &value);
    /// The slides a presentation shows: the ones not hidden.
    Q_INVOKABLE QVariantList shown() const;
    /// [{id, name}] of the layouts.
    Q_INVOKABLE QVariantList layouts() const;

    // --- Items of the current slide ---
    /// What the canvas needs of each item to frame it: geometry, kind and group.
    Q_INVOKABLE QVariantList items(int index) const;
    /// Everything about one item, with its colours resolved and its text format summed up.
    Q_INVOKABLE QVariantMap item(const QString &id) const;
    Q_INVOKABLE QString addText();
    Q_INVOKABLE QString addShape(const QString &shape);
    Q_INVOKABLE QString addImage(const QString &path);
    Q_INVOKABLE QString addTable(int rows, int cols);
    /// Sets one property of every selected item.
    Q_INVOKABLE void setProp(const QString &key, const QVariant &value);
    /// Formats all the text of every selected item: bold, italic, underline, strike, size, color, font, align, bullet.
    Q_INVOKABLE void setFormat(const QString &key, const QVariant &value);
    Q_INVOKABLE QString html(const QString &id) const;
    Q_INVOKABLE void setHtml(const QString &id, const QString &html);
    Q_INVOKABLE void setPlain(const QString &id, const QString &text);
    /// {font, size, color, align, bold}: what text typed into the item starts as.
    Q_INVOKABLE QVariantMap textDefaults(const QString &id) const;
    Q_INVOKABLE void setCell(const QString &id, int row, int col, const QString &text);
    Q_INVOKABLE void setTableSize(const QString &id, int rows, int cols);

    // --- Selection ---
    Q_INVOKABLE void select(const QString &id, bool extend = false);
    Q_INVOKABLE void selectOnly(const QStringList &ids);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectRect(double x, double y, double w, double h);
    /// The topmost item under a point of the slide, or nothing.
    Q_INVOKABLE QString hitTest(double x, double y, double slack = 0) const;
    /// {x, y, w, h} around the selection.
    Q_INVOKABLE QVariantMap selectionBounds() const;

    // --- Dragging: start, any number of moves, finish ---
    Q_INVOKABLE void startDrag();
    /// Moves the selection `dx`, `dy` from where the drag started; `snap` is how near
    /// (in points) an edge has to come to a line to jump to it, 0 for never.
    Q_INVOKABLE void moveBy(double dx, double dy, double snap = 0);
    Q_INVOKABLE void resizeBy(const QString &handle, double dx, double dy, bool keepAspect = false);
    /// Moves one end of a line: 0 the start, 1 the end.
    Q_INVOKABLE void moveEnd(int end, double dx, double dy);
    Q_INVOKABLE void rotateBy(double degrees);
    Q_INVOKABLE void finishDrag(const QString &text);

    // --- Arranging ---
    Q_INVOKABLE void nudge(double dx, double dy);
    /// left, center, right, top, middle or bottom: against the slide for one item, against each other for several.
    Q_INVOKABLE void align(const QString &mode);
    /// h or v: equal gaps between three items or more.
    Q_INVOKABLE void distribute(const QString &axis);
    Q_INVOKABLE void group();
    Q_INVOKABLE void ungroup();
    /// front, back, forward or backward.
    Q_INVOKABLE void order(const QString &mode);
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE void remove();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // --- The look of the whole deck ---
    /// [{id, name, bg1, bg2, title, text, accents}].
    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE void setTheme(const QString &id);
    Q_INVOKABLE QVariantMap master() const;
    Q_INVOKABLE void setMaster(const QVariantMap &master);
    /// 16:9 or 4:3.
    Q_INVOKABLE void setAspect(const QString &aspect);
    /// The colours of the theme for the colour wells: [{token, color, name}].
    Q_INVOKABLE QVariantList swatches() const;
    Q_INVOKABLE QStringList fonts() const;

    // --- The outline ---
    /// The deck as Markdown: «# » a slide's title, «- » its bullets, «---» the next slide.
    Q_INVOKABLE QString outline() const;
    /// Makes the deck follow the outline, as one step.
    Q_INVOKABLE void setOutline(const QString &markdown);

    /// Takes the slides of another presentation (an earlier version), as one step.
    void restore(const Model::Presentation &data, const QString &text);
    /// Gives the deck the file it is to be saved in, and counts it as changed.
    void adopt(const QString &path, const QString &name);
    Q_INVOKABLE void setName(const QString &name);
    /// The bytes of the deck in the format of `suffix` (pptx or odp).
    QByteArray bytes(const QString &suffix) const;

    /// For the tests: the item itself.
    const Model::Item *find(const QString &id) const;
    void setData(const Model::Presentation &data);

Q_SIGNALS:
    void changed();
    void currentChanged();
    void selectionChanged();
    void stackChanged();
    void fileChanged();
    void guidesChanged();

private:
    Model::Slide *now();
    const Model::Slide *now() const;
    Model::Item *find(const QString &id);
    QStringList withGroups(const QStringList &ids) const;
    QList<Model::Item> selected() const;
    /// Changes the current slide through `change`, as one undoable step.
    void edit(const QString &text, const std::function<void(Model::Slide &)> &change, const QString &mergeKey = QString());
    void push(std::unique_ptr<Command> command);
    void bump();
    void setSelection(const QStringList &ids);
    void pruneSelection();
    QString place(Model::Item item, const QString &text);
    void touchStack();

    Model::Presentation m_data;
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    QStringList m_selection;
    QVariantList m_guides;
    QString m_path;
    QString m_error;
    QString m_name;
    /// The slide as it was when the drag started.
    Model::Slide m_dragStart;
    bool m_dragging = false;
    QElapsedTimer m_lastPush;
    int m_current = 0;
    int m_revision = 0;
    int m_savedAt = 0;
};
