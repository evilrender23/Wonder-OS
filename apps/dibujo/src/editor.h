#pragma once

#include "geometry.h"
#include "history.h"
#include "model.h"

#include <QObject>
#include <QSizeF>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

/// The drawing being worked on: what is selected, where the view is, which
/// tool is in hand and what the pointer is doing. Everything the window does
/// to a drawing goes through here, so it can all be tested without a window.
class Editor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")
    /// select, hand, rect, ellipse, line, arrow, connector, polygon, star,
    /// pen, pencil, text, note, frame, or form:<form> for one of the library.
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    /// What the inspector shows of the selection.
    Q_PROPERTY(QVariantMap info READ info NOTIFY infoChanged)
    /// Top first: {id, name, kind, symbol, locked, hidden, grouped}.
    Q_PROPERTY(QVariantList layers READ layers NOTIFY structureChanged)
    Q_PROPERTY(QVariantList frames READ frameList NOTIFY structureChanged)
    Q_PROPERTY(int count READ count NOTIFY structureChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY settingsChanged)
    Q_PROPERTY(bool snap READ snap WRITE setSnap NOTIFY settingsChanged)
    Q_PROPERTY(QStringList palette READ palette NOTIFY settingsChanged)
    /// The object whose words are being typed.
    Q_PROPERTY(QString editing READ editing WRITE setEditing NOTIFY editingChanged)
    Q_PROPERTY(QString cursor READ cursor NOTIFY cursorChanged)
    /// Around the selection, on the screen; empty with nothing selected.
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY overlayChanged)
    /// A drag is under way.
    Q_PROPERTY(bool busy READ busy NOTIFY overlayChanged)
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY pointerChanged)
    /// New free strokes and words are light, to show on a dark canvas.
    Q_PROPERTY(bool darkCanvas MEMBER m_darkCanvas)

public:
    explicit Editor(QObject *parent = nullptr);

    const Document &document() const { return m_doc; }
    /// Another drawing: nothing selected, nothing to undo.
    void setDocument(const Document &doc);
    void markSaved();
    /// There is something to save that no step of the history says (recovered work).
    void touch() { m_touched = true; Q_EMIT historyChanged(); }

    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    double zoom() const { return m_zoom; }
    QPointF viewCenter() const { return m_center; }
    QSizeF viewport() const { return m_viewport; }
    int revision() const { return m_revision; }
    QStringList selection() const { return m_selection; }
    QVariantMap info() const;
    QVariantList layers() const;
    QVariantList frameList() const;
    int count() const { return m_doc.shapes.size(); }
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    bool dirty() const { return m_touched || !m_history.atSaved(); }
    QString title() const { return m_doc.title; }
    void setTitle(const QString &title);
    bool grid() const { return m_doc.grid; }
    void setGrid(bool on);
    bool snap() const { return m_doc.snap; }
    void setSnap(bool on);
    QStringList palette() const { return m_doc.palette; }
    QString editing() const { return m_editing; }
    void setEditing(const QString &id);
    QString cursor() const { return m_cursor; }
    QRectF selectionRect() const;
    bool busy() const { return m_drag != Drag::None; }
    QPointF pointer() const { return m_pointer; }

    QPointF toWorld(const QPointF &screen) const;
    QPointF toScreen(const QPointF &world) const;

    // --- What the canvas paints over the drawing --------------------------------
    struct Handle {
        QString role;
        QPointF pos;
    };
    /// The grips of the selection, on the screen.
    QList<Handle> handles() const;
    /// The outline of each selected object, on the canvas.
    QList<QPolygonF> selectionOutlines() const;
    QRectF marquee() const { return m_marquee; }
    QList<Geometry::Guide> guides() const { return m_guides; }
    QString hovered() const { return m_hover; }
    /// The object a line end would stick to if dropped now.
    QString attachTarget() const { return m_attach; }
    /// The path the pen has laid so far, with the stretch to the pointer.
    QPainterPath penPreview() const;
    QList<QPointF> penAnchors() const;
    QList<QPointF> strokePreview() const { return m_stroke; }

    // --- The view ---------------------------------------------------------------
    Q_INVOKABLE void setViewport(double width, double height);
    Q_INVOKABLE void zoomAt(double factor, double screenX, double screenY);
    Q_INVOKABLE void setZoom(double zoom);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void fitAll();
    Q_INVOKABLE void fitSelection();
    Q_INVOKABLE void panBy(double screenDx, double screenDy);
    Q_INVOKABLE void centerOn(double worldX, double worldY);
    /// Brings an object (a frame of the list) into view.
    Q_INVOKABLE void reveal(const QString &id);
    Q_INVOKABLE void wheel(double x, double y, double dx, double dy, int modifiers);
    void fitRect(const QRectF &rect, double margin = 60, double maxZoom = 1);

    // --- The pointer ------------------------------------------------------------
    Q_INVOKABLE void press(double x, double y, int button, int modifiers);
    Q_INVOKABLE void move(double x, double y, int modifiers);
    Q_INVOKABLE void release(double x, double y, int modifiers);
    Q_INVOKABLE void doubleClick(double x, double y);
    /// Escape: drops a drag, a path half drawn, the selection, in that order.
    Q_INVOKABLE void escape();
    /// Enter: ends the path the pen is drawing.
    Q_INVOKABLE void finishPen(bool close = false);
    /// The space bar held: the pointer drags the canvas.
    Q_INVOKABLE void setSpacePan(bool on);

    // --- Selection --------------------------------------------------------------
    Q_INVOKABLE void select(const QString &id, bool add = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    void setSelection(const QStringList &ids);
    /// The topmost object under a point of the canvas; empty if none.
    QString shapeAt(const QPointF &world, bool lockedToo = false) const;

    // --- Changing the drawing ---------------------------------------------------
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicate();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void group();
    Q_INVOKABLE void ungroup();
    /// front, back, forward or backward.
    Q_INVOKABLE void reorder(const QString &how);
    /// left, hcenter, right, top, vcenter or bottom.
    Q_INVOKABLE void align(const QString &how);
    /// h or v: equal gaps between three or more.
    Q_INVOKABLE void distribute(const QString &axis);
    Q_INVOKABLE void toggleLock();
    Q_INVOKABLE void nudge(double dx, double dy);
    /// Sets one thing of the selection: x, y, w, h, rotation, name, form,
    /// sides, route, autoSize, or any key of the style.
    Q_INVOKABLE void setProp(const QString &key, const QVariant &value);
    Q_INVOKABLE void setLayerHidden(const QString &id, bool hidden);
    Q_INVOKABLE void setLayerLocked(const QString &id, bool locked);
    Q_INVOKABLE void rename(const QString &id, const QString &name);
    Q_INVOKABLE void setText(const QString &id, const QString &text);
    /// Where to put the field that edits the words of an object, on the screen.
    Q_INVOKABLE QVariantMap textBox(const QString &id) const;

    /// A form of the library in the middle of the view. Returns its id.
    Q_INVOKABLE QString addForm(const QString &form, int sides = 0, double radius = 0);
    /// A frame of a preset size (see Geometry::framePresets), beside what there is.
    Q_INVOKABLE QString addFrame(const QString &preset);
    Q_INVOKABLE QString addText(const QString &text);
    Q_INVOKABLE QString addNote(const QString &text);
    /// straight, elbow or curve, between two objects; with or without a head.
    Q_INVOKABLE QString connect(const QString &from, const QString &to, const QString &route = QStringLiteral("elbow"), const QString &label = {});
    QString addImage(const QByteArray &bytes, const QString &suffix);
    /// Adds finished objects (an import, a paste) in the middle of the view
    /// and selects them.
    QStringList insert(QList<Shape> shapes, const QMap<QString, QByteArray> &resources, const QString &label, bool center = true);

    Q_INVOKABLE void addPaletteColor(const QString &color);
    Q_INVOKABLE void removePaletteColor(const QString &color);
    /// The colours the drawing uses, most used first.
    Q_INVOKABLE QStringList usedColors() const;

    /// For the tests: one change as a step of its own.
    void edit(const QString &label, const std::function<void(Document &)> &change);

Q_SIGNALS:
    void toolChanged();
    void viewChanged();
    void changed();
    void overlayChanged();
    void selectionChanged();
    void infoChanged();
    void structureChanged();
    void historyChanged();
    void titleChanged();
    void settingsChanged();
    void editingChanged();
    void cursorChanged();
    void pointerChanged();
    void said(const QString &message);
    /// The words of this object want typing: the window puts a field over it.
    void editRequested(const QString &id);

private:
    enum class Drag { None, Maybe, Move, Resize, Rotate, Marquee, Create, LineEnd, Pencil, PenHandle, Pan };
    struct Frame {
        QRectF box;
        double rotation = 0;
    };
    struct PenNode {
        QPointF p;
        QPointF in;
        QPointF out;
    };

    void begin();
    void commit(const QString &label, const QString &mergeKey = {});
    void cancel();
    void refresh(bool structure = true);
    void setCursor(const QString &cursor);
    void setGuides(const QList<Geometry::Guide> &guides);

    QStringList withGroups(const QStringList &ids) const;
    QStringList movable() const;
    QRectF boundsOf(const QStringList &ids) const;
    Frame selectionFrame() const;
    QString handleAt(const QPointF &screen) const;
    QString attachableAt(const QPointF &world, const QString &except) const;
    QList<QRectF> snapTargets(const QStringList &moving) const;
    Shape prototype(const QString &tool) const;
    void place(Shape &shape) const;
    void fitText(Shape &shape) const;
    bool onFrame(const QPointF &world) const;
    QStringList insertCopies(const QList<Shape> &shapes, const QPointF &offset);
    QByteArray clip() const;
    void translate(Shape &shape, const QPointF &delta) const;
    void dragMove(const QPointF &world, int modifiers);
    void dragResize(const QPointF &world, int modifiers);
    void dragRotate(const QPointF &world, int modifiers);
    void dragCreate(const QPointF &world, int modifiers);
    void dragLineEnd(const QPointF &world, int modifiers);
    void hover(const QPointF &screen);
    QString structureKey() const;

    Document m_doc;
    Document m_before;
    int m_depth = 0;
    History m_history;
    bool m_touched = false;
    QStringList m_selection;
    QString m_tool = QStringLiteral("select");
    QString m_editing;
    QString m_cursor = QStringLiteral("arrow");
    QString m_structure;
    int m_revision = 0;
    bool m_darkCanvas = false;
    Style m_defaults;

    QPointF m_center;
    double m_zoom = 1;
    QSizeF m_viewport = QSizeF(800, 600);
    bool m_fitPending = false;

    Drag m_drag = Drag::None;
    bool m_spacePan = false;
    QPointF m_pressScreen;
    QPointF m_pressWorld;
    QPointF m_pressCenter;
    QPointF m_pointer;
    QString m_handle;
    QString m_pressed;
    bool m_toggleOnRelease = false;
    bool m_moved = false;
    QStringList m_moving;
    QStringList m_marqueeBase;
    Frame m_frame0;
    QString m_created;
    QRectF m_marquee;
    QList<Geometry::Guide> m_guides;
    QString m_hover;
    QString m_attach;
    QList<QPointF> m_stroke;
    QList<PenNode> m_pen;
    QPointF m_penHover;
    int m_pasteCount = 0;
};
