#include "editor.h"

#include "render.h"

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QtMath>

#include <cmath>

namespace
{
constexpr char ClipType[] = "application/x-wonderland-dibujo-shapes";
constexpr double MinZoom = 0.05;
constexpr double MaxZoom = 32;
constexpr double HandleReach = 9;
constexpr double RotateGap = 26;
constexpr int ShiftMod = 0x02000000;
constexpr int ControlMod = 0x04000000;

void syncLine(Shape &line)
{
    const QRectF r = QRectF(line.p1, line.p2).normalized();
    line.x = r.x();
    line.y = r.y();
    line.w = r.width();
    line.h = r.height();
    line.rotation = 0;
}

QPointF turned(const QPointF &point, const QPointF &around, double degrees)
{
    const double a = qDegreesToRadians(degrees);
    const QPointF d = point - around;
    return around + QPointF(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
}

/// Shift held: a line keeps to the eighths of a turn.
QPointF constrained(const QPointF &from, const QPointF &to)
{
    const QPointF d = to - from;
    const double length = std::hypot(d.x(), d.y());
    const double step = M_PI / 4;
    const double angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return from + QPointF(std::cos(angle), std::sin(angle)) * length;
}

QString symbolOf(const Shape &s)
{
    if (s.isLine()) {
        return s.from.isEmpty() && s.to.isEmpty() && s.route == QLatin1String("straight")
            ? (s.style.endArrow == QLatin1String("none") ? QStringLiteral("slash") : QStringLiteral("move-up-right"))
            : QStringLiteral("workflow");
    }
    if (s.kind == QLatin1String("path")) {
        return QStringLiteral("spline");
    }
    if (s.kind == QLatin1String("text")) {
        return QStringLiteral("type");
    }
    if (s.kind == QLatin1String("image")) {
        return QStringLiteral("image");
    }
    if (s.kind == QLatin1String("note")) {
        return QStringLiteral("sticky-note");
    }
    if (s.kind == QLatin1String("frame")) {
        return QStringLiteral("proportions");
    }
    if (s.form == QLatin1String("ellipse")) {
        return QStringLiteral("circle");
    }
    if (s.form == QLatin1String("triangle")) {
        return QStringLiteral("triangle");
    }
    if (s.form == QLatin1String("diamond")) {
        return QStringLiteral("diamond");
    }
    if (s.form == QLatin1String("star")) {
        return QStringLiteral("star");
    }
    if (s.form == QLatin1String("rect")) {
        return QStringLiteral("square");
    }
    return QStringLiteral("shapes");
}

/// Stretches an object from one box of the canvas into another.
Shape scaled(Shape s, const QRectF &from, const QRectF &to)
{
    const double sx = from.width() > 0.001 ? to.width() / from.width() : 1;
    const double sy = from.height() > 0.001 ? to.height() / from.height() : 1;
    const auto map = [&](const QPointF &p) {
        return QPointF(to.x() + (p.x() - from.x()) * sx, to.y() + (p.y() - from.y()) * sy);
    };
    if (s.isLine()) {
        s.p1 = map(s.p1);
        s.p2 = map(s.p2);
        syncLine(s);
        return s;
    }
    const QPointF c = map(s.center());
    // A turned object in a stretched group keeps its turn; its box takes
    // the stretch as if it were upright, which is right at 0 and 90 degrees.
    const bool sideways = qAbs(std::fmod(qAbs(s.rotation), 180.0) - 90) < 45;
    s.w = qMax(1.0, s.w * (sideways ? sy : sx));
    s.h = qMax(1.0, s.h * (sideways ? sx : sy));
    s.x = c.x() - s.w / 2;
    s.y = c.y() - s.h / 2;
    return s;
}
}

Editor::Editor(QObject *parent)
    : QObject(parent)
{
    m_doc.uid = QString();
}

// --- The drawing ----------------------------------------------------------------

void Editor::setDocument(const Document &doc)
{
    m_doc = doc;
    Geometry::updateConnectors(m_doc);
    m_history.clear();
    m_touched = false;
    m_depth = 0;
    m_drag = Drag::None;
    m_selection.clear();
    m_pen.clear();
    m_stroke.clear();
    m_guides.clear();
    m_marquee = QRectF();
    m_hover.clear();
    m_attach.clear();
    m_editing.clear();
    m_tool = QStringLiteral("select");
    m_fitPending = true;
    Q_EMIT toolChanged();
    Q_EMIT editingChanged();
    Q_EMIT selectionChanged();
    Q_EMIT titleChanged();
    Q_EMIT settingsChanged();
    m_structure.clear();
    refresh();
    Q_EMIT structureChanged();
    fitAll();
}

void Editor::markSaved()
{
    m_history.markSaved();
    m_touched = false;
    Q_EMIT historyChanged();
}

void Editor::setTitle(const QString &title)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty() || clean == m_doc.title) {
        return;
    }
    m_doc.title = clean;
    m_touched = true;
    Q_EMIT titleChanged();
    Q_EMIT historyChanged();
}

void Editor::setGrid(bool on)
{
    if (m_doc.grid == on) {
        return;
    }
    m_doc.grid = on;
    m_touched = true;
    Q_EMIT settingsChanged();
    Q_EMIT historyChanged();
    Q_EMIT changed();
}

void Editor::setSnap(bool on)
{
    if (m_doc.snap == on) {
        return;
    }
    m_doc.snap = on;
    m_touched = true;
    Q_EMIT settingsChanged();
    Q_EMIT historyChanged();
}

void Editor::begin()
{
    if (m_depth++ == 0) {
        m_before = m_doc;
    }
}

void Editor::commit(const QString &label, const QString &mergeKey)
{
    if (m_depth <= 0) {
        return;
    }
    if (--m_depth > 0) {
        return;
    }
    Geometry::updateConnectors(m_doc);
    m_history.push(Command::between(m_before, m_doc, label), mergeKey);
    m_before = Document();
    refresh();
}

void Editor::cancel()
{
    if (m_depth > 0) {
        m_doc = m_before;
        m_before = Document();
        m_depth = 0;
    }
    refresh();
}

void Editor::edit(const QString &label, const std::function<void(Document &)> &change)
{
    begin();
    change(m_doc);
    commit(label);
}

QString Editor::structureKey() const
{
    QString key;
    key.reserve(m_doc.shapes.size() * 24);
    for (const Shape &s : m_doc.shapes) {
        key += s.id + QLatin1Char('|') + s.name + QLatin1Char('|') + s.kind + s.form + s.group + QLatin1Char(s.locked ? 'L' : 'l')
            + QLatin1Char(s.hidden ? 'H' : 'h') + QLatin1Char('\n');
        if (s.isFrame()) {
            key += QString::number(s.w) + QLatin1Char('x') + QString::number(s.h);
        }
    }
    return key;
}

void Editor::refresh(bool structure)
{
    ++m_revision;
    QStringList kept;
    for (const QString &id : std::as_const(m_selection)) {
        const Shape *s = m_doc.find(id);
        if (s && !s->hidden) {
            kept << id;
        }
    }
    if (kept != m_selection) {
        m_selection = kept;
        Q_EMIT selectionChanged();
    }
    if (!m_editing.isEmpty() && !m_doc.contains(m_editing)) {
        m_editing.clear();
        Q_EMIT editingChanged();
    }
    Q_EMIT changed();
    Q_EMIT overlayChanged();
    Q_EMIT infoChanged();
    Q_EMIT historyChanged();
    if (structure) {
        const QString key = structureKey();
        if (key != m_structure) {
            m_structure = key;
            Q_EMIT structureChanged();
        }
    }
}

void Editor::setCursor(const QString &cursor)
{
    if (m_cursor != cursor) {
        m_cursor = cursor;
        Q_EMIT cursorChanged();
    }
}

void Editor::setGuides(const QList<Geometry::Guide> &guides)
{
    m_guides = guides;
}

void Editor::setTool(const QString &tool)
{
    if (m_tool == tool) {
        return;
    }
    if (m_tool == QLatin1String("pen")) {
        finishPen(false);
    }
    m_tool = tool;
    m_hover.clear();
    Q_EMIT toolChanged();
    setCursor(tool == QLatin1String("select") ? QStringLiteral("arrow") : tool == QLatin1String("hand") ? QStringLiteral("openHand")
            : tool == QLatin1String("text") ? QStringLiteral("text") : QStringLiteral("cross"));
    Q_EMIT overlayChanged();
}

void Editor::setEditing(const QString &id)
{
    if (m_editing == id) {
        return;
    }
    m_editing = id;
    Q_EMIT editingChanged();
    Q_EMIT changed();
    Q_EMIT overlayChanged();
}

// --- The view -------------------------------------------------------------------

QPointF Editor::toWorld(const QPointF &screen) const
{
    return m_center + (screen - QPointF(m_viewport.width() / 2, m_viewport.height() / 2)) / m_zoom;
}

QPointF Editor::toScreen(const QPointF &world) const
{
    return (world - m_center) * m_zoom + QPointF(m_viewport.width() / 2, m_viewport.height() / 2);
}

void Editor::setViewport(double width, double height)
{
    if (width <= 0 || height <= 0 || QSizeF(width, height) == m_viewport) {
        return;
    }
    m_viewport = QSizeF(width, height);
    if (m_fitPending) {
        fitAll();
    }
    Q_EMIT viewChanged();
    Q_EMIT overlayChanged();
}

void Editor::zoomAt(double factor, double screenX, double screenY)
{
    const QPointF screen(screenX, screenY);
    const QPointF anchor = toWorld(screen);
    const double zoom = qBound(MinZoom, m_zoom * factor, MaxZoom);
    if (qFuzzyCompare(zoom, m_zoom)) {
        return;
    }
    m_zoom = zoom;
    // What is under the pointer stays under it.
    m_center = anchor - (screen - QPointF(m_viewport.width() / 2, m_viewport.height() / 2)) / m_zoom;
    m_fitPending = false;
    Q_EMIT viewChanged();
    Q_EMIT overlayChanged();
}

void Editor::setZoom(double zoom)
{
    zoomAt(zoom / m_zoom, m_viewport.width() / 2, m_viewport.height() / 2);
}

void Editor::zoomIn()
{
    static const double steps[] = {0.05, 0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 1, 1.25, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    for (double step : steps) {
        if (step > m_zoom * 1.001) {
            setZoom(step);
            return;
        }
    }
}

void Editor::zoomOut()
{
    static const double steps[] = {32, 24, 16, 12, 8, 6, 4, 3, 2, 1.5, 1.25, 1, 0.75, 0.67, 0.5, 0.33, 0.25, 0.1, 0.05};
    for (double step : steps) {
        if (step < m_zoom * 0.999) {
            setZoom(step);
            return;
        }
    }
}

void Editor::fitRect(const QRectF &rect, double margin, double maxZoom)
{
    if (rect.isEmpty()) {
        m_zoom = 1;
        m_center = rect.isNull() ? QPointF() : rect.center();
    } else {
        const double zx = (m_viewport.width() - 2 * margin) / rect.width();
        const double zy = (m_viewport.height() - 2 * margin) / rect.height();
        m_zoom = qBound(MinZoom, qMin(qMin(zx, zy), maxZoom), MaxZoom);
        m_center = rect.center();
    }
    Q_EMIT viewChanged();
    Q_EMIT overlayChanged();
}

void Editor::fitAll()
{
    fitRect(m_doc.bounds());
}

void Editor::fitSelection()
{
    if (m_selection.isEmpty()) {
        fitAll();
        return;
    }
    fitRect(boundsOf(m_selection), 80, 4);
    m_fitPending = false;
}

void Editor::panBy(double screenDx, double screenDy)
{
    m_center += QPointF(screenDx, screenDy) / m_zoom;
    m_fitPending = false;
    Q_EMIT viewChanged();
    Q_EMIT overlayChanged();
}

void Editor::centerOn(double worldX, double worldY)
{
    m_center = QPointF(worldX, worldY);
    m_fitPending = false;
    Q_EMIT viewChanged();
    Q_EMIT overlayChanged();
}

void Editor::reveal(const QString &id)
{
    const Shape *s = m_doc.find(id);
    if (!s) {
        return;
    }
    fitRect(s->bounds(), 60, 2);
    m_fitPending = false;
}

void Editor::wheel(double x, double y, double dx, double dy, int modifiers)
{
    if (modifiers & ControlMod) {
        zoomAt(std::pow(1.0018, dy), x, y);
    } else if (modifiers & ShiftMod) {
        panBy(-(qAbs(dx) > qAbs(dy) ? dx : dy) * 0.6, 0);
    } else {
        panBy(-dx * 0.6, -dy * 0.6);
    }
}

// --- Selection ------------------------------------------------------------------

QStringList Editor::withGroups(const QStringList &ids) const
{
    QStringList groups;
    for (const QString &id : ids) {
        const Shape *s = m_doc.find(id);
        if (s && !s->group.isEmpty() && !groups.contains(s->group)) {
            groups << s->group;
        }
    }
    QStringList out;
    for (const Shape &s : m_doc.shapes) {
        if (ids.contains(s.id) || (!s.group.isEmpty() && groups.contains(s.group))) {
            out << s.id;
        }
    }
    return out;
}

void Editor::setSelection(const QStringList &ids)
{
    QStringList clean;
    for (const QString &id : withGroups(ids)) {
        const Shape *s = m_doc.find(id);
        if (s && !s->hidden && !clean.contains(id)) {
            clean << id;
        }
    }
    if (clean == m_selection) {
        return;
    }
    m_selection = clean;
    m_history.seal();
    Q_EMIT selectionChanged();
    Q_EMIT infoChanged();
    Q_EMIT overlayChanged();
}

void Editor::select(const QString &id, bool add)
{
    if (!m_doc.contains(id)) {
        return;
    }
    if (!add) {
        setSelection({id});
        return;
    }
    QStringList ids = m_selection;
    const QStringList unit = withGroups({id});
    if (ids.contains(id)) {
        for (const QString &member : unit) {
            ids.removeAll(member);
        }
    } else {
        ids << unit;
    }
    setSelection(ids);
}

void Editor::selectAll()
{
    QStringList ids;
    for (const Shape &s : m_doc.shapes) {
        if (!s.locked && !s.hidden) {
            ids << s.id;
        }
    }
    setSelection(ids);
}

void Editor::clearSelection()
{
    setSelection({});
}

QString Editor::shapeAt(const QPointF &world, bool lockedToo) const
{
    const double slack = 5 / m_zoom;
    for (int i = m_doc.shapes.size() - 1; i >= 0; --i) {
        const Shape &s = m_doc.shapes.at(i);
        if (s.hidden || (s.locked && !lockedToo)) {
            continue;
        }
        if (Geometry::hit(s, world, s.isFrame() ? slack * 1.4 : slack)) {
            return s.id;
        }
    }
    return {};
}

QRectF Editor::boundsOf(const QStringList &ids) const
{
    QRectF all;
    for (const QString &id : ids) {
        if (const Shape *s = m_doc.find(id)) {
            const QRectF b = s->bounds();
            all = all.isNull() ? b : all.united(b);
        }
    }
    return all;
}

Editor::Frame Editor::selectionFrame() const
{
    Frame frame;
    if (m_selection.size() == 1) {
        if (const Shape *s = m_doc.find(m_selection.first())) {
            frame.box = s->isLine() ? s->bounds() : s->box();
            frame.rotation = s->isLine() ? 0 : s->rotation;
            return frame;
        }
    }
    frame.box = boundsOf(m_selection);
    return frame;
}

QRectF Editor::selectionRect() const
{
    if (m_selection.isEmpty()) {
        return {};
    }
    const QRectF b = boundsOf(m_selection);
    return QRectF(toScreen(b.topLeft()), toScreen(b.bottomRight()));
}

QList<Editor::Handle> Editor::handles() const
{
    QList<Handle> list;
    if (m_selection.isEmpty() || m_tool != QLatin1String("select") || !m_editing.isEmpty()) {
        return list;
    }
    if (m_selection.size() == 1) {
        const Shape *s = m_doc.find(m_selection.first());
        if (!s || s->locked) {
            return list;
        }
        if (s->isLine()) {
            list << Handle{QStringLiteral("p1"), toScreen(s->p1)} << Handle{QStringLiteral("p2"), toScreen(s->p2)};
            return list;
        }
    } else {
        for (const QString &id : m_selection) {
            const Shape *s = m_doc.find(id);
            if (s && s->locked) {
                return list;
            }
        }
    }
    const Frame frame = selectionFrame();
    const QRectF b = frame.box;
    const QPointF c = b.center();
    const auto at = [&](double fx, double fy) {
        return toScreen(turned(QPointF(b.x() + b.width() * fx, b.y() + b.height() * fy), c, frame.rotation));
    };
    const QSizeF onScreen = b.size() * m_zoom;
    list << Handle{QStringLiteral("nw"), at(0, 0)} << Handle{QStringLiteral("ne"), at(1, 0)} << Handle{QStringLiteral("se"), at(1, 1)}
         << Handle{QStringLiteral("sw"), at(0, 1)};
    // Small things keep only their corners, or the grips would hide them.
    if (onScreen.width() > 44) {
        list << Handle{QStringLiteral("n"), at(0.5, 0)} << Handle{QStringLiteral("s"), at(0.5, 1)};
    }
    if (onScreen.height() > 44) {
        list << Handle{QStringLiteral("e"), at(1, 0.5)} << Handle{QStringLiteral("w"), at(0, 0.5)};
    }
    const QPointF top = at(0.5, 0);
    const QPointF centre = toScreen(c);
    QPointF up = top - centre;
    const double length = std::hypot(up.x(), up.y());
    up = length > 0.01 ? up / length : QPointF(0, -1);
    list << Handle{QStringLiteral("rotate"), top + up * RotateGap};
    return list;
}

QString Editor::handleAt(const QPointF &screen) const
{
    QString best;
    double bestDistance = HandleReach;
    for (const Handle &handle : handles()) {
        const QPointF d = handle.pos - screen;
        const double distance = std::hypot(d.x(), d.y());
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = handle.role;
        }
    }
    return best;
}

QList<QPolygonF> Editor::selectionOutlines() const
{
    QList<QPolygonF> list;
    for (const QString &id : m_selection) {
        const Shape *s = m_doc.find(id);
        if (!s) {
            continue;
        }
        if (s->isLine()) {
            list << QPolygonF{s->p1, s->p2};
        } else {
            list << s->transform().map(QPolygonF(QRectF(0, 0, s->w, s->h)));
        }
    }
    return list;
}

QPainterPath Editor::penPreview() const
{
    QPainterPath path;
    if (m_pen.isEmpty()) {
        return path;
    }
    path.moveTo(m_pen.first().p);
    for (int i = 1; i < m_pen.size(); ++i) {
        path.cubicTo(m_pen.at(i - 1).out, m_pen.at(i).in, m_pen.at(i).p);
    }
    if (m_drag != Drag::PenHandle) {
        path.cubicTo(m_pen.last().out, m_penHover, m_penHover);
    }
    return path;
}

QList<QPointF> Editor::penAnchors() const
{
    QList<QPointF> list;
    for (const PenNode &node : m_pen) {
        list << node.p;
    }
    return list;
}

// --- What the inspector shows -----------------------------------------------------

QVariantMap Editor::info() const
{
    QVariantMap map;
    map.insert(QStringLiteral("count"), m_selection.size());
    if (m_selection.isEmpty()) {
        return map;
    }
    const Shape *first = m_doc.find(m_selection.first());
    if (!first) {
        return map;
    }
    QString kind = first->kind;
    bool anyText = false;
    bool anyFill = false;
    bool anyLine = false;
    bool allLines = true;
    bool anyLocked = false;
    bool anyGroup = false;
    QStringList units;
    for (const QString &id : m_selection) {
        const Shape *s = m_doc.find(id);
        if (!s) {
            continue;
        }
        if (s->kind != kind) {
            kind = QStringLiteral("mixed");
        }
        anyText = anyText || (s->kind != QLatin1String("image") && s->kind != QLatin1String("path") && !s->isFrame());
        anyFill = anyFill || (!s->isLine() && s->kind != QLatin1String("image"));
        anyLine = anyLine || s->isLine();
        allLines = allLines && s->isLine();
        anyLocked = anyLocked || s->locked;
        anyGroup = anyGroup || !s->group.isEmpty();
        const QString unit = s->group.isEmpty() ? s->id : s->group;
        if (!units.contains(unit)) {
            units << unit;
        }
    }
    const Frame frame = selectionFrame();
    const Style &st = first->style;
    map.insert(QStringLiteral("id"), first->id);
    map.insert(QStringLiteral("kind"), kind);
    map.insert(QStringLiteral("name"), m_selection.size() == 1 ? first->name : QStringLiteral("%1 objetos").arg(m_selection.size()));
    map.insert(QStringLiteral("x"), frame.box.x());
    map.insert(QStringLiteral("y"), frame.box.y());
    map.insert(QStringLiteral("w"), frame.box.width());
    map.insert(QStringLiteral("h"), frame.box.height());
    map.insert(QStringLiteral("rotation"), frame.rotation);
    map.insert(QStringLiteral("single"), m_selection.size() == 1);
    map.insert(QStringLiteral("units"), units.size());
    map.insert(QStringLiteral("form"), first->form);
    map.insert(QStringLiteral("sides"), first->sides);
    map.insert(QStringLiteral("route"), first->route);
    map.insert(QStringLiteral("autoSize"), first->autoSize);
    map.insert(QStringLiteral("text"), first->text);
    map.insert(QStringLiteral("locked"), anyLocked);
    map.insert(QStringLiteral("grouped"), anyGroup);
    map.insert(QStringLiteral("hasFill"), anyFill);
    map.insert(QStringLiteral("hasText"), anyText);
    map.insert(QStringLiteral("hasLine"), anyLine);
    map.insert(QStringLiteral("allLines"), allLines);
    map.insert(QStringLiteral("isFrame"), kind == QLatin1String("frame"));
    map.insert(QStringLiteral("isImage"), kind == QLatin1String("image"));
    map.insert(QStringLiteral("hasRadius"), kind == QLatin1String("shape") && first->form == QLatin1String("rect"));
    map.insert(QStringLiteral("hasSides"), kind == QLatin1String("shape") && (first->form == QLatin1String("polygon") || first->form == QLatin1String("star")));
    map.insert(QStringLiteral("fillKind"), st.fillKind);
    map.insert(QStringLiteral("fill"), Model::colorName(st.fill));
    map.insert(QStringLiteral("fill2"), Model::colorName(st.fill2));
    map.insert(QStringLiteral("gradientAngle"), st.gradientAngle);
    map.insert(QStringLiteral("stroke"), Model::colorName(st.stroke));
    map.insert(QStringLiteral("strokeWidth"), st.strokeWidth);
    map.insert(QStringLiteral("dash"), st.dash);
    map.insert(QStringLiteral("startArrow"), st.startArrow);
    map.insert(QStringLiteral("endArrow"), st.endArrow);
    map.insert(QStringLiteral("radius"), st.radius);
    map.insert(QStringLiteral("shadow"), st.shadow);
    map.insert(QStringLiteral("opacity"), st.opacity);
    map.insert(QStringLiteral("fontFamily"), st.fontFamily);
    map.insert(QStringLiteral("fontSize"), st.fontSize);
    map.insert(QStringLiteral("bold"), st.bold);
    map.insert(QStringLiteral("italic"), st.italic);
    map.insert(QStringLiteral("underline"), st.underline);
    map.insert(QStringLiteral("textColor"), Model::colorName(st.textColor));
    map.insert(QStringLiteral("hAlign"), st.hAlign);
    map.insert(QStringLiteral("vAlign"), st.vAlign);
    // What the selected do not agree on: shown as such, not as the first one's.
    QStringList mixed;
    const auto differs = [&](const char *key, auto value) {
        for (const QString &id : m_selection) {
            const Shape *s = m_doc.find(id);
            if (s && value(*s) != value(*first)) {
                mixed << QLatin1String(key);
                return;
            }
        }
    };
    if (m_selection.size() > 1) {
        differs("fillKind", [](const Shape &s) { return s.style.fillKind; });
        differs("fill", [](const Shape &s) { return s.style.fill; });
        differs("stroke", [](const Shape &s) { return s.style.stroke; });
        differs("strokeWidth", [](const Shape &s) { return s.style.strokeWidth; });
        differs("dash", [](const Shape &s) { return s.style.dash; });
        differs("opacity", [](const Shape &s) { return s.style.opacity; });
        differs("fontSize", [](const Shape &s) { return s.style.fontSize; });
        differs("textColor", [](const Shape &s) { return s.style.textColor; });
        differs("startArrow", [](const Shape &s) { return s.style.startArrow; });
        differs("endArrow", [](const Shape &s) { return s.style.endArrow; });
    }
    map.insert(QStringLiteral("mixed"), mixed);
    return map;
}

QVariantList Editor::layers() const
{
    QVariantList list;
    for (int i = m_doc.shapes.size() - 1; i >= 0; --i) {
        const Shape &s = m_doc.shapes.at(i);
        list << QVariantMap{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("name"), s.name},
            {QStringLiteral("kind"), s.kind},
            {QStringLiteral("symbol"), symbolOf(s)},
            {QStringLiteral("locked"), s.locked},
            {QStringLiteral("hidden"), s.hidden},
            {QStringLiteral("grouped"), !s.group.isEmpty()},
        };
    }
    return list;
}

QVariantList Editor::frameList() const
{
    QVariantList list;
    for (const Shape *s : m_doc.frames()) {
        list << QVariantMap{
            {QStringLiteral("id"), s->id},
            {QStringLiteral("name"), s->name},
            {QStringLiteral("size"), QStringLiteral("%1 × %2").arg(qRound(s->w)).arg(qRound(s->h))},
        };
    }
    return list;
}

// --- New objects ----------------------------------------------------------------

Shape Editor::prototype(const QString &tool) const
{
    Shape s;
    s.style = m_defaults;
    const QColor ink = m_darkCanvas ? QColor(QStringLiteral("#f5f5f7")) : QColor(QStringLiteral("#1d1d1f"));
    if (tool == QLatin1String("line") || tool == QLatin1String("arrow") || tool == QLatin1String("connector")) {
        s.kind = QStringLiteral("line");
        s.style.stroke = ink;
        s.style.textColor = ink;
        s.style.fontSize = 13;
        if (tool != QLatin1String("line")) {
            s.style.endArrow = QStringLiteral("triangle");
        }
        if (tool == QLatin1String("connector")) {
            s.route = QStringLiteral("elbow");
        }
    } else if (tool == QLatin1String("text")) {
        s.kind = QStringLiteral("text");
        s.autoSize = true;
        s.style.fillKind = QStringLiteral("none");
        s.style.strokeWidth = 0;
        s.style.textColor = ink;
        s.style.fontSize = 20;
        s.style.hAlign = QStringLiteral("left");
        s.style.vAlign = QStringLiteral("top");
    } else if (tool == QLatin1String("note")) {
        s.kind = QStringLiteral("note");
        s.style.fill = QColor(QStringLiteral("#ffe07a"));
        s.style.strokeWidth = 0;
        s.style.shadow = true;
    } else if (tool == QLatin1String("frame")) {
        s.kind = QStringLiteral("frame");
        s.style.fill = QColor(QStringLiteral("#ffffff"));
        s.style.strokeWidth = 0;
    } else if (tool == QLatin1String("pen") || tool == QLatin1String("pencil")) {
        s.kind = QStringLiteral("path");
        s.style.fillKind = QStringLiteral("none");
        s.style.stroke = ink;
    } else if (tool == QLatin1String("image")) {
        s.kind = QStringLiteral("image");
        s.style.strokeWidth = 0;
    } else {
        s.kind = QStringLiteral("shape");
        QString form = tool.startsWith(QLatin1String("form:")) ? tool.mid(5) : tool;
        if (form == QLatin1String("rect") || form.isEmpty()) {
            form = QStringLiteral("rect");
        }
        s.form = form;
        if (form == QLatin1String("polygon")) {
            s.sides = 6;
        }
    }
    return s;
}

void Editor::place(Shape &shape) const
{
    shape.x = m_center.x() - shape.w / 2;
    shape.y = m_center.y() - shape.h / 2;
    // Not right on top of the one before.
    bool taken = true;
    while (taken) {
        taken = false;
        for (const Shape &s : m_doc.shapes) {
            if (qAbs(s.x - shape.x) < 1 && qAbs(s.y - shape.y) < 1 && !s.isFrame()) {
                shape.x += 24;
                shape.y += 24;
                taken = true;
                break;
            }
        }
    }
}

void Editor::fitText(Shape &shape) const
{
    if (shape.kind != QLatin1String("text")) {
        return;
    }
    const QRectF inner = Render::textRect(shape);
    const double pad = inner.x();
    Shape probe = shape;
    if (probe.text.isEmpty()) {
        probe.text = QStringLiteral("M");
    }
    if (shape.autoSize) {
        const QSizeF size = Render::textSize(probe, 0);
        const QPointF c = shape.center();
        const double w = size.width() + 2 * 4 + 2;
        const double h = size.height() + 2 * 4;
        // Grows from its left edge, or from its centre once it is turned.
        if (qFuzzyIsNull(shape.rotation)) {
            shape.w = w;
            shape.h = h;
        } else {
            shape.w = w;
            shape.h = h;
            shape.x = c.x() - w / 2;
            shape.y = c.y() - h / 2;
        }
    } else {
        const QSizeF size = Render::textSize(probe, qMax(1.0, shape.w - 2 * pad));
        shape.h = qMax(shape.h, size.height() + 2 * pad);
    }
}

QString Editor::addForm(const QString &form, int sides, double radius)
{
    begin();
    Shape s = prototype(QStringLiteral("form:") + form);
    if (sides > 0) {
        s.sides = sides;
    }
    s.style.radius = radius;
    const bool round = form == QLatin1String("polygon") || form == QLatin1String("star") || form == QLatin1String("heart") || form == QLatin1String("cross");
    s.w = round ? 120 : 160;
    s.h = round ? 120 : 96;
    if (form == QLatin1String("pill")) {
        s.h = 64;
    }
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    place(s);
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Añadir %1").arg(Geometry::baseName(s).toLower()));
    setSelection({id});
    return id;
}

QString Editor::addFrame(const QString &preset)
{
    QSizeF size(1280, 720);
    QString label = QStringLiteral("Marco");
    for (const QVariant &v : Geometry::framePresets()) {
        const QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("key")).toString() == preset) {
            size = QSizeF(p.value(QStringLiteral("w")).toDouble(), p.value(QStringLiteral("h")).toDouble());
        }
    }
    begin();
    Shape s = prototype(QStringLiteral("frame"));
    s.w = size.width();
    s.h = size.height();
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(label);
    // Beside what there is, never over it.
    const QRectF all = m_doc.bounds();
    s.x = all.isNull() ? -s.w / 2 : all.right() + 100;
    s.y = all.isNull() ? -s.h / 2 : all.top();
    int at = 0;
    while (at < m_doc.shapes.size() && m_doc.shapes.at(at).isFrame()) {
        ++at;
    }
    m_doc.shapes.insert(at, s);
    const QString id = s.id;
    commit(QStringLiteral("Añadir marco"));
    setSelection({id});
    reveal(id);
    return id;
}

QString Editor::addText(const QString &text)
{
    begin();
    Shape s = prototype(QStringLiteral("text"));
    s.text = text;
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    fitText(s);
    place(s);
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Añadir texto"));
    setSelection({id});
    return id;
}

QString Editor::addNote(const QString &text)
{
    begin();
    Shape s = prototype(QStringLiteral("note"));
    s.text = text;
    s.w = 180;
    s.h = 180;
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    place(s);
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Añadir nota"));
    setSelection({id});
    return id;
}

QString Editor::connect(const QString &from, const QString &to, const QString &route, const QString &label)
{
    const Shape *a = m_doc.find(from);
    const Shape *b = m_doc.find(to);
    if (!a || !b || a == b || a->isLine() || b->isLine()) {
        return {};
    }
    begin();
    Shape s = prototype(QStringLiteral("connector"));
    s.route = route;
    s.from = from;
    s.to = to;
    s.p1 = a->center();
    s.p2 = b->center();
    s.text = label;
    syncLine(s);
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Conectar"));
    setSelection({id});
    return id;
}

QString Editor::addImage(const QByteArray &bytes, const QString &suffix)
{
    const QImage picture = QImage::fromData(bytes);
    if (picture.isNull()) {
        Q_EMIT said(QStringLiteral("No se pudo leer la imagen"));
        return {};
    }
    begin();
    Shape s = prototype(QStringLiteral("image"));
    s.image = m_doc.addResource(bytes, suffix.toLower());
    QSizeF size = picture.size();
    const double longest = qMax(size.width(), size.height());
    if (longest > 640) {
        size *= 640 / longest;
    }
    s.w = size.width();
    s.h = size.height();
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    place(s);
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Añadir imagen"));
    setSelection({id});
    return id;
}

QStringList Editor::insertCopies(const QList<Shape> &shapes, const QPointF &offset)
{
    QHash<QString, QString> ids;
    QHash<QString, QString> groups;
    QList<Shape> copies;
    for (Shape s : shapes) {
        const QString fresh = m_doc.freshId();
        ids.insert(s.id, fresh);
        s.id = fresh;
        // Reserve the id before the next is asked for.
        Shape holder;
        holder.id = fresh;
        m_doc.shapes << holder;
        copies << s;
    }
    m_doc.shapes.remove(m_doc.shapes.size() - copies.size(), copies.size());
    QStringList added;
    for (Shape &s : copies) {
        if (!s.group.isEmpty()) {
            if (!groups.contains(s.group)) {
                groups.insert(s.group, QStringLiteral("g") + s.id);
            }
            s.group = groups.value(s.group);
        }
        // A copied connector stays stuck only to what was copied with it.
        s.from = ids.value(s.from);
        s.to = ids.value(s.to);
        translate(s, offset);
        const QString base = Geometry::baseName(s);
        if (s.name.isEmpty() || m_doc.freshName(base) != QStringLiteral("%1 1").arg(base) || true) {
            bool taken = false;
            for (const Shape &other : std::as_const(m_doc.shapes)) {
                taken = taken || other.name == s.name;
            }
            if (taken || s.name.isEmpty()) {
                s.name = m_doc.freshName(base);
            }
        }
        m_doc.shapes << s;
        added << s.id;
    }
    return added;
}

QStringList Editor::insert(QList<Shape> shapes, const QMap<QString, QByteArray> &resources, const QString &label, bool center)
{
    if (shapes.isEmpty()) {
        return {};
    }
    begin();
    QHash<QString, QString> names;
    for (Shape &s : shapes) {
        if (s.image.isEmpty()) {
            continue;
        }
        if (!names.contains(s.image) && resources.contains(s.image)) {
            const int dot = s.image.lastIndexOf(QLatin1Char('.'));
            names.insert(s.image, m_doc.addResource(resources.value(s.image), dot >= 0 ? s.image.mid(dot + 1) : QString()));
        }
        s.image = names.value(s.image, s.image);
    }
    QPointF offset;
    if (center) {
        QRectF all;
        for (const Shape &s : std::as_const(shapes)) {
            all = all.isNull() ? s.bounds() : all.united(s.bounds());
        }
        offset = m_center - all.center();
    }
    const QStringList added = insertCopies(shapes, offset);
    commit(label);
    setSelection(added);
    return added;
}

// --- Changing the drawing ---------------------------------------------------------

void Editor::undo()
{
    if (m_depth > 0) {
        cancel();
    }
    if (m_history.undo(m_doc)) {
        Geometry::updateConnectors(m_doc);
        refresh();
    }
}

void Editor::redo()
{
    if (m_depth > 0) {
        return;
    }
    if (m_history.redo(m_doc)) {
        Geometry::updateConnectors(m_doc);
        refresh();
    }
}

void Editor::deleteSelection()
{
    if (m_selection.isEmpty()) {
        return;
    }
    begin();
    const QStringList gone = m_selection;
    m_doc.shapes.removeIf([&gone](const Shape &s) { return gone.contains(s.id) && !s.locked; });
    commit(QStringLiteral("Eliminar"));
}

void Editor::translate(Shape &shape, const QPointF &delta) const
{
    shape.x += delta.x();
    shape.y += delta.y();
    shape.p1 += delta;
    shape.p2 += delta;
}

void Editor::duplicate()
{
    if (m_selection.isEmpty()) {
        return;
    }
    QList<Shape> originals;
    for (const Shape &s : std::as_const(m_doc.shapes)) {
        if (m_selection.contains(s.id)) {
            originals << s;
        }
    }
    begin();
    const QStringList added = insertCopies(originals, QPointF(24, 24));
    commit(QStringLiteral("Duplicar"));
    setSelection(added);
}

QByteArray Editor::clip() const
{
    QJsonArray shapes;
    QJsonObject resources;
    for (const Shape &s : m_doc.shapes) {
        if (!m_selection.contains(s.id)) {
            continue;
        }
        shapes << s.toJson();
        if (!s.image.isEmpty() && m_doc.resources.contains(s.image)) {
            resources.insert(s.image, QString::fromLatin1(m_doc.resources.value(s.image).toBase64()));
        }
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("shapes"), shapes}, {QStringLiteral("resources"), resources}}).toJson(QJsonDocument::Compact);
}

void Editor::copy()
{
    if (m_selection.isEmpty()) {
        return;
    }
    auto *data = new QMimeData;
    data->setData(QString::fromLatin1(ClipType), clip());
    QStringList words;
    for (const QString &id : std::as_const(m_selection)) {
        const Shape *s = m_doc.find(id);
        if (s && !s->text.isEmpty()) {
            words << s->text;
        }
    }
    if (!words.isEmpty()) {
        data->setText(words.join(QLatin1Char('\n')));
    }
    QGuiApplication::clipboard()->setMimeData(data);
    m_pasteCount = 0;
}

void Editor::cut()
{
    copy();
    deleteSelection();
}

void Editor::paste()
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data) {
        return;
    }
    if (data->hasFormat(QString::fromLatin1(ClipType))) {
        const QJsonObject json = QJsonDocument::fromJson(data->data(QString::fromLatin1(ClipType))).object();
        QList<Shape> shapes;
        for (const QJsonValue &v : json.value(QLatin1String("shapes")).toArray()) {
            shapes << Shape::fromJson(v.toObject());
        }
        QMap<QString, QByteArray> resources;
        const QJsonObject packed = json.value(QLatin1String("resources")).toObject();
        for (auto it = packed.constBegin(); it != packed.constEnd(); ++it) {
            resources.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
        }
        ++m_pasteCount;
        const QStringList added = insert(shapes, resources, QStringLiteral("Pegar"), false);
        if (!added.isEmpty()) {
            begin();
            for (const QString &id : added) {
                if (Shape *s = m_doc.find(id)) {
                    translate(*s, QPointF(24, 24) * m_pasteCount);
                }
            }
            commit(QStringLiteral("Pegar"), QStringLiteral("paste"));
        }
        return;
    }
    if (data->hasImage()) {
        const QImage picture = qvariant_cast<QImage>(data->imageData());
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!picture.isNull() && picture.save(&buffer, "PNG")) {
            addImage(png, QStringLiteral("png"));
        }
        return;
    }
    if (data->hasText() && !data->text().trimmed().isEmpty()) {
        addText(data->text().trimmed());
    }
}

void Editor::group()
{
    if (m_selection.size() < 2) {
        return;
    }
    begin();
    const QString group = QStringLiteral("g") + m_doc.freshId();
    for (Shape &s : m_doc.shapes) {
        if (m_selection.contains(s.id)) {
            s.group = group;
        }
    }
    commit(QStringLiteral("Agrupar"));
    Q_EMIT infoChanged();
}

void Editor::ungroup()
{
    begin();
    for (Shape &s : m_doc.shapes) {
        if (m_selection.contains(s.id)) {
            s.group.clear();
        }
    }
    commit(QStringLiteral("Desagrupar"));
    Q_EMIT infoChanged();
}

void Editor::reorder(const QString &how)
{
    if (m_selection.isEmpty()) {
        return;
    }
    begin();
    QList<Shape> &list = m_doc.shapes;
    const auto picked = [this](const Shape &s) { return m_selection.contains(s.id); };
    if (how == QLatin1String("front") || how == QLatin1String("back")) {
        QList<Shape> chosen;
        QList<Shape> rest;
        for (const Shape &s : std::as_const(list)) {
            (picked(s) ? chosen : rest) << s;
        }
        list = how == QLatin1String("front") ? rest + chosen : chosen + rest;
    } else if (how == QLatin1String("forward")) {
        for (int i = list.size() - 2; i >= 0; --i) {
            if (picked(list.at(i)) && !picked(list.at(i + 1))) {
                list.swapItemsAt(i, i + 1);
            }
        }
    } else if (how == QLatin1String("backward")) {
        for (int i = 1; i < list.size(); ++i) {
            if (picked(list.at(i)) && !picked(list.at(i - 1))) {
                list.swapItemsAt(i, i - 1);
            }
        }
    }
    commit(QStringLiteral("Cambiar orden"));
    Q_EMIT structureChanged();
}

namespace
{
struct Unit {
    QStringList ids;
    QRectF bounds;
};
}

static QList<Unit> unitsOf(const Document &doc, const QStringList &selection)
{
    QList<Unit> units;
    QHash<QString, int> byGroup;
    for (const Shape &s : doc.shapes) {
        if (!selection.contains(s.id)) {
            continue;
        }
        int at = s.group.isEmpty() ? -1 : byGroup.value(s.group, -1);
        if (at < 0) {
            units << Unit{};
            at = units.size() - 1;
            if (!s.group.isEmpty()) {
                byGroup.insert(s.group, at);
            }
        }
        units[at].ids << s.id;
        units[at].bounds = units[at].bounds.isNull() ? s.bounds() : units[at].bounds.united(s.bounds());
    }
    return units;
}

void Editor::align(const QString &how)
{
    const QList<Unit> units = unitsOf(m_doc, m_selection);
    if (units.isEmpty()) {
        return;
    }
    QRectF target = boundsOf(m_selection);
    if (units.size() == 1) {
        // One thing alone lines up with the frame it sits on.
        target = QRectF();
        const QPointF c = units.first().bounds.center();
        for (const Shape *frame : m_doc.frames()) {
            if (frame->box().contains(c) && !m_selection.contains(frame->id)) {
                target = frame->box();
            }
        }
        if (target.isNull()) {
            return;
        }
    }
    begin();
    for (const Unit &unit : units) {
        QPointF d;
        const QRectF &b = unit.bounds;
        if (how == QLatin1String("left")) {
            d.setX(target.left() - b.left());
        } else if (how == QLatin1String("hcenter")) {
            d.setX(target.center().x() - b.center().x());
        } else if (how == QLatin1String("right")) {
            d.setX(target.right() - b.right());
        } else if (how == QLatin1String("top")) {
            d.setY(target.top() - b.top());
        } else if (how == QLatin1String("vcenter")) {
            d.setY(target.center().y() - b.center().y());
        } else if (how == QLatin1String("bottom")) {
            d.setY(target.bottom() - b.bottom());
        }
        for (const QString &id : unit.ids) {
            if (Shape *s = m_doc.find(id); s && !s->locked) {
                translate(*s, d);
            }
        }
    }
    commit(QStringLiteral("Alinear"));
}

void Editor::distribute(const QString &axis)
{
    QList<Unit> units = unitsOf(m_doc, m_selection);
    if (units.size() < 3) {
        return;
    }
    const bool vertical = axis == QLatin1String("v");
    std::sort(units.begin(), units.end(), [vertical](const Unit &a, const Unit &b) {
        return vertical ? a.bounds.center().y() < b.bounds.center().y() : a.bounds.center().x() < b.bounds.center().x();
    });
    double used = 0;
    for (const Unit &unit : std::as_const(units)) {
        used += vertical ? unit.bounds.height() : unit.bounds.width();
    }
    const double start = vertical ? units.first().bounds.top() : units.first().bounds.left();
    const double end = vertical ? units.last().bounds.bottom() : units.last().bounds.right();
    const double gap = (end - start - used) / (units.size() - 1);
    begin();
    double at = start;
    for (const Unit &unit : std::as_const(units)) {
        const double from = vertical ? unit.bounds.top() : unit.bounds.left();
        const QPointF d = vertical ? QPointF(0, at - from) : QPointF(at - from, 0);
        for (const QString &id : unit.ids) {
            if (Shape *s = m_doc.find(id)) {
                translate(*s, d);
            }
        }
        at += (vertical ? unit.bounds.height() : unit.bounds.width()) + gap;
    }
    commit(QStringLiteral("Distribuir"));
}

void Editor::toggleLock()
{
    if (m_selection.isEmpty()) {
        return;
    }
    bool anyLocked = false;
    for (const QString &id : std::as_const(m_selection)) {
        const Shape *s = m_doc.find(id);
        anyLocked = anyLocked || (s && s->locked);
    }
    begin();
    for (Shape &s : m_doc.shapes) {
        if (m_selection.contains(s.id)) {
            s.locked = !anyLocked;
        }
    }
    commit(anyLocked ? QStringLiteral("Desbloquear") : QStringLiteral("Bloquear"));
}

QStringList Editor::movable() const
{
    QStringList ids;
    QList<QRectF> frames;
    for (const QString &id : m_selection) {
        const Shape *s = m_doc.find(id);
        if (!s || s->locked) {
            continue;
        }
        ids << id;
        if (s->isFrame()) {
            frames << s->box();
        }
    }
    // A frame takes along what lies on it.
    if (!frames.isEmpty()) {
        for (const Shape &s : m_doc.shapes) {
            if (ids.contains(s.id) || s.locked || s.isFrame()) {
                continue;
            }
            for (const QRectF &frame : std::as_const(frames)) {
                if (frame.contains(s.bounds())) {
                    ids << s.id;
                    break;
                }
            }
        }
    }
    return ids;
}

void Editor::nudge(double dx, double dy)
{
    const QStringList ids = movable();
    if (ids.isEmpty()) {
        return;
    }
    begin();
    for (const QString &id : ids) {
        if (Shape *s = m_doc.find(id)) {
            translate(*s, QPointF(dx, dy));
            if (s->isLine()) {
                if (!ids.contains(s->from)) {
                    s->from.clear();
                }
                if (!ids.contains(s->to)) {
                    s->to.clear();
                }
            }
        }
    }
    commit(QStringLiteral("Mover"), QStringLiteral("nudge:") + m_selection.join(QLatin1Char(',')));
}

void Editor::setProp(const QString &key, const QVariant &value)
{
    if (m_selection.isEmpty()) {
        return;
    }
    const Frame frame = selectionFrame();
    const bool single = m_selection.size() == 1;
    begin();
    if (key == QLatin1String("x") || key == QLatin1String("y")) {
        const QPointF d = key == QLatin1String("x") ? QPointF(value.toDouble() - frame.box.x(), 0) : QPointF(0, value.toDouble() - frame.box.y());
        for (const QString &id : movable()) {
            translate(*m_doc.find(id), d);
        }
    } else if (key == QLatin1String("w") || key == QLatin1String("h")) {
        const double size = qMax(1.0, value.toDouble());
        QRectF to = frame.box;
        if (key == QLatin1String("w")) {
            to.setWidth(size);
        } else {
            to.setHeight(size);
        }
        for (const QString &id : std::as_const(m_selection)) {
            Shape *s = m_doc.find(id);
            if (single && !s->isLine()) {
                // Around its own centre when turned, from its corner when not.
                const QPointF c = s->center();
                s->w = to.width();
                s->h = to.height();
                if (!qFuzzyIsNull(s->rotation)) {
                    s->x = c.x() - s->w / 2;
                    s->y = c.y() - s->h / 2;
                }
                if (s->kind == QLatin1String("text")) {
                    s->autoSize = false;
                    fitText(*s);
                }
            } else {
                *s = scaled(*s, frame.box, to);
            }
        }
    } else if (key == QLatin1String("rotation")) {
        double turn = std::fmod(value.toDouble(), 360.0);
        const QPointF c = frame.box.center();
        for (const QString &id : std::as_const(m_selection)) {
            Shape *s = m_doc.find(id);
            if (s->isLine()) {
                continue;
            }
            if (single) {
                s->rotation = turn;
            } else {
                const QPointF moved = turned(s->center(), c, turn);
                s->x = moved.x() - s->w / 2;
                s->y = moved.y() - s->h / 2;
                s->rotation = std::fmod(s->rotation + turn, 360.0);
            }
        }
    } else {
        for (const QString &id : std::as_const(m_selection)) {
            Shape &s = *m_doc.find(id);
            Style &st = s.style;
            if (key == QLatin1String("name")) {
                if (!value.toString().trimmed().isEmpty()) {
                    s.name = value.toString().trimmed();
                }
            } else if (key == QLatin1String("form")) {
                if (s.kind == QLatin1String("shape")) {
                    s.form = value.toString();
                }
            } else if (key == QLatin1String("sides")) {
                s.sides = qBound(3, value.toInt(), 24);
            } else if (key == QLatin1String("route")) {
                if (s.isLine()) {
                    s.route = value.toString();
                }
            } else if (key == QLatin1String("autoSize")) {
                s.autoSize = value.toBool();
            } else if (key == QLatin1String("fillKind")) {
                st.fillKind = value.toString();
            } else if (key == QLatin1String("fill")) {
                st.fill = Model::color(value.toString(), st.fill);
                if (st.fillKind == QLatin1String("none")) {
                    st.fillKind = QStringLiteral("solid");
                }
            } else if (key == QLatin1String("fill2")) {
                st.fill2 = Model::color(value.toString(), st.fill2);
            } else if (key == QLatin1String("gradientAngle")) {
                st.gradientAngle = value.toDouble();
            } else if (key == QLatin1String("stroke")) {
                st.stroke = Model::color(value.toString(), st.stroke);
                if (st.strokeWidth <= 0) {
                    st.strokeWidth = 2;
                }
            } else if (key == QLatin1String("strokeWidth")) {
                st.strokeWidth = qBound(0.0, value.toDouble(), 64.0);
            } else if (key == QLatin1String("dash")) {
                st.dash = value.toString();
            } else if (key == QLatin1String("startArrow")) {
                st.startArrow = value.toString();
            } else if (key == QLatin1String("endArrow")) {
                st.endArrow = value.toString();
            } else if (key == QLatin1String("radius")) {
                st.radius = qMax(0.0, value.toDouble());
            } else if (key == QLatin1String("shadow")) {
                st.shadow = value.toBool();
            } else if (key == QLatin1String("opacity")) {
                st.opacity = qBound(0.05, value.toDouble(), 1.0);
            } else if (key == QLatin1String("fontFamily")) {
                st.fontFamily = value.toString();
            } else if (key == QLatin1String("fontSize")) {
                st.fontSize = qBound(4.0, value.toDouble(), 400.0);
            } else if (key == QLatin1String("bold")) {
                st.bold = value.toBool();
            } else if (key == QLatin1String("italic")) {
                st.italic = value.toBool();
            } else if (key == QLatin1String("underline")) {
                st.underline = value.toBool();
            } else if (key == QLatin1String("textColor")) {
                st.textColor = Model::color(value.toString(), st.textColor);
            } else if (key == QLatin1String("hAlign")) {
                st.hAlign = value.toString();
            } else if (key == QLatin1String("vAlign")) {
                st.vAlign = value.toString();
            }
            fitText(s);
        }
    }
    commit(QStringLiteral("Cambiar formato"), QStringLiteral("prop:") + key + m_selection.join(QLatin1Char(',')));
}

void Editor::setLayerHidden(const QString &id, bool hidden)
{
    if (!m_doc.contains(id)) {
        return;
    }
    begin();
    for (const QString &member : withGroups({id})) {
        m_doc.find(member)->hidden = hidden;
    }
    commit(hidden ? QStringLiteral("Ocultar") : QStringLiteral("Mostrar"));
}

void Editor::setLayerLocked(const QString &id, bool locked)
{
    if (!m_doc.contains(id)) {
        return;
    }
    begin();
    for (const QString &member : withGroups({id})) {
        m_doc.find(member)->locked = locked;
    }
    commit(locked ? QStringLiteral("Bloquear") : QStringLiteral("Desbloquear"));
}

void Editor::rename(const QString &id, const QString &name)
{
    Shape *s = m_doc.find(id);
    if (!s || name.trimmed().isEmpty() || s->name == name.trimmed()) {
        return;
    }
    begin();
    m_doc.find(id)->name = name.trimmed();
    commit(QStringLiteral("Renombrar"));
}

void Editor::setText(const QString &id, const QString &text)
{
    const Shape *was = m_doc.find(id);
    if (!was) {
        return;
    }
    if (was->text == text && !(was->kind == QLatin1String("text") && text.trimmed().isEmpty())) {
        return;
    }
    begin();
    Shape *s = m_doc.find(id);
    s->text = text;
    fitText(*s);
    if (s->kind == QLatin1String("text") && text.trimmed().isEmpty()) {
        // A text with no words is nothing.
        m_doc.shapes.removeAt(m_doc.indexOf(id));
    }
    commit(QStringLiteral("Escribir"));
}

QVariantMap Editor::textBox(const QString &id) const
{
    const Shape *s = m_doc.find(id);
    if (!s) {
        return {};
    }
    QRectF box = s->box();
    if (s->isLine()) {
        const QPointF mid = Geometry::linePath(*s).pointAtPercent(0.5);
        box = QRectF(mid.x() - 90, mid.y() - s->style.fontSize * 1.6 - 4, 180, s->style.fontSize * 1.6);
    }
    const QRectF inner = Render::textRect(*s);
    const QPointF centre = toScreen(box.center());
    return {
        {QStringLiteral("cx"), centre.x()},
        {QStringLiteral("cy"), centre.y()},
        {QStringLiteral("w"), box.width()},
        {QStringLiteral("h"), box.height()},
        {QStringLiteral("zoom"), m_zoom},
        {QStringLiteral("rotation"), s->isLine() ? 0 : s->rotation},
        {QStringLiteral("pad"), s->isLine() ? 0 : inner.x()},
        {QStringLiteral("text"), s->text},
        {QStringLiteral("fontSize"), qMax(1, qRound(s->style.fontSize))},
        {QStringLiteral("fontFamily"), s->style.fontFamily},
        {QStringLiteral("bold"), s->style.bold},
        {QStringLiteral("italic"), s->style.italic},
        {QStringLiteral("underline"), s->style.underline},
        {QStringLiteral("color"), Model::colorName(s->style.textColor)},
        {QStringLiteral("hAlign"), s->isLine() ? QStringLiteral("center") : s->style.hAlign},
        {QStringLiteral("vAlign"), s->isLine() ? QStringLiteral("bottom") : s->style.vAlign},
        {QStringLiteral("autoSize"), s->autoSize},
    };
}

void Editor::addPaletteColor(const QString &color)
{
    const QColor parsed = Model::color(color);
    if (!parsed.isValid()) {
        return;
    }
    const QString name = Model::colorName(parsed);
    if (m_doc.palette.contains(name)) {
        return;
    }
    m_doc.palette << name;
    m_touched = true;
    Q_EMIT settingsChanged();
    Q_EMIT historyChanged();
}

void Editor::removePaletteColor(const QString &color)
{
    if (m_doc.palette.removeAll(color) > 0) {
        m_touched = true;
        Q_EMIT settingsChanged();
        Q_EMIT historyChanged();
    }
}

QStringList Editor::usedColors() const
{
    QHash<QString, int> uses;
    for (const Shape &s : m_doc.shapes) {
        if (!s.isLine() && s.style.fillKind != QLatin1String("none") && s.kind != QLatin1String("image")) {
            ++uses[Model::colorName(s.style.fill)];
        }
        if (s.style.strokeWidth > 0) {
            ++uses[Model::colorName(s.style.stroke)];
        }
    }
    QStringList names = uses.keys();
    std::sort(names.begin(), names.end(), [&uses](const QString &a, const QString &b) {
        return uses.value(a) != uses.value(b) ? uses.value(a) > uses.value(b) : a < b;
    });
    return names.mid(0, 12);
}

// --- The pointer ----------------------------------------------------------------

QString Editor::attachableAt(const QPointF &world, const QString &except) const
{
    for (int i = m_doc.shapes.size() - 1; i >= 0; --i) {
        const Shape &s = m_doc.shapes.at(i);
        if (s.hidden || s.isLine() || s.isFrame() || s.id == except || s.kind == QLatin1String("path")) {
            continue;
        }
        if (s.bounds().adjusted(-6 / m_zoom, -6 / m_zoom, 6 / m_zoom, 6 / m_zoom).contains(world)
            && Geometry::worldOutline(s).boundingRect().adjusted(-6 / m_zoom, -6 / m_zoom, 6 / m_zoom, 6 / m_zoom).contains(world)) {
            return s.id;
        }
    }
    return {};
}

QList<QRectF> Editor::snapTargets(const QStringList &moving) const
{
    QList<QRectF> list;
    const QRectF seen(toWorld(QPointF(0, 0)), toWorld(QPointF(m_viewport.width(), m_viewport.height())));
    for (const Shape &s : m_doc.shapes) {
        if (s.hidden || moving.contains(s.id) || s.isLine()) {
            continue;
        }
        const QRectF b = s.bounds();
        if (b.intersects(seen)) {
            list << b;
        }
    }
    return list;
}

void Editor::setSpacePan(bool on)
{
    if (m_spacePan == on) {
        return;
    }
    m_spacePan = on;
    if (m_drag == Drag::None) {
        setCursor(on ? QStringLiteral("openHand") : m_tool == QLatin1String("select") ? QStringLiteral("arrow") : m_tool == QLatin1String("hand") ? QStringLiteral("openHand") : QStringLiteral("cross"));
    }
}

void Editor::press(double x, double y, int button, int modifiers)
{
    const QPointF screen(x, y);
    const QPointF world = toWorld(screen);
    m_pressScreen = screen;
    m_pressWorld = world;
    m_pressCenter = m_center;
    m_moved = false;
    m_history.seal();
    if (m_drag != Drag::None) {
        return;
    }
    // The middle button, the hand and the space bar drag the canvas.
    if (button == 4 || m_tool == QLatin1String("hand") || m_spacePan) {
        m_drag = Drag::Pan;
        setCursor(QStringLiteral("closedHand"));
        return;
    }
    if (button != 1) {
        // The other button picks what is under it, for its menu.
        const QString id = shapeAt(world);
        if (!id.isEmpty() && !m_selection.contains(id)) {
            setSelection({id});
        }
        return;
    }
    if (m_tool == QLatin1String("select")) {
        const QString handle = handleAt(screen);
        if (!handle.isEmpty()) {
            m_handle = handle;
            m_frame0 = selectionFrame();
            m_moving = m_selection;
            begin();
            m_drag = handle == QLatin1String("rotate") ? Drag::Rotate : handle.startsWith(QLatin1Char('p')) ? Drag::LineEnd : Drag::Resize;
            Q_EMIT overlayChanged();
            return;
        }
        const QString id = shapeAt(world);
        if (id.isEmpty()) {
            m_marqueeBase = (modifiers & ShiftMod) ? m_selection : QStringList();
            if (!(modifiers & ShiftMod)) {
                clearSelection();
            }
            m_drag = Drag::Marquee;
            m_marquee = QRectF(world, world);
            return;
        }
        if (modifiers & ShiftMod) {
            select(id, true);
            return;
        }
        if (!m_selection.contains(id)) {
            setSelection({id});
        }
        m_pressed = id;
        m_drag = Drag::Maybe;
        return;
    }
    if (m_tool == QLatin1String("pencil")) {
        m_stroke = {world};
        m_drag = Drag::Pencil;
        return;
    }
    if (m_tool == QLatin1String("pen")) {
        if (m_pen.size() >= 2) {
            const QPointF d = toScreen(m_pen.first().p) - screen;
            if (std::hypot(d.x(), d.y()) < 9) {
                finishPen(true);
                return;
            }
        }
        m_pen << PenNode{world, world, world};
        m_penHover = world;
        m_drag = Drag::PenHandle;
        Q_EMIT overlayChanged();
        return;
    }
    // Every other tool draws a new object from here to where it is let go.
    begin();
    Shape s = prototype(m_tool);
    s.id = m_doc.freshId();
    s.x = world.x();
    s.y = world.y();
    s.p1 = world;
    s.p2 = world;
    if (s.isLine() && m_tool == QLatin1String("connector")) {
        s.from = attachableAt(world, s.id);
    }
    s.name = m_doc.freshName(Geometry::baseName(s));
    if (s.isFrame()) {
        int at = 0;
        while (at < m_doc.shapes.size() && m_doc.shapes.at(at).isFrame()) {
            ++at;
        }
        m_doc.shapes.insert(at, s);
    } else {
        m_doc.shapes << s;
    }
    m_created = s.id;
    m_drag = Drag::Create;
}

void Editor::dragMove(const QPointF &world, int modifiers)
{
    QPointF delta = world - m_pressWorld;
    if (modifiers & ShiftMod) {
        if (qAbs(delta.x()) > qAbs(delta.y())) {
            delta.setY(0);
        } else {
            delta.setX(0);
        }
    }
    QList<Geometry::Guide> guides;
    if (m_doc.snap && !(modifiers & ControlMod)) {
        QRectF moving;
        for (const QString &id : std::as_const(m_moving)) {
            if (const Shape *s = m_before.find(id)) {
                moving = moving.isNull() ? s->bounds() : moving.united(s->bounds());
            }
        }
        moving.translate(delta);
        const Geometry::Snap snap = Geometry::snapMove(moving, snapTargets(m_moving), 6 / m_zoom);
        if (!snap.guides.isEmpty()) {
            delta += snap.delta;
            guides = snap.guides;
        } else if (m_doc.grid) {
            const double step = 10;
            delta += QPointF(std::round(moving.x() / step) * step - moving.x(), std::round(moving.y() / step) * step - moving.y());
        }
    }
    setGuides(guides);
    for (const QString &id : std::as_const(m_moving)) {
        const Shape *was = m_before.find(id);
        Shape *s = m_doc.find(id);
        if (!was || !s) {
            continue;
        }
        *s = *was;
        translate(*s, delta);
        // A line dragged away from what it was stuck to lets go.
        if (s->isLine()) {
            if (!s->from.isEmpty() && !m_moving.contains(s->from)) {
                s->from.clear();
            }
            if (!s->to.isEmpty() && !m_moving.contains(s->to)) {
                s->to.clear();
            }
        }
    }
    Geometry::updateConnectors(m_doc);
}

void Editor::dragResize(const QPointF &pointer, int modifiers)
{
    const QRectF b0 = m_frame0.box;
    const QPointF c0 = b0.center();
    QPointF world = pointer;
    QList<Geometry::Guide> guides;
    if (m_doc.snap && !(modifiers & ControlMod) && qFuzzyIsNull(m_frame0.rotation)) {
        const Geometry::Snap snap = Geometry::snapPoint(world, snapTargets(m_moving), 6 / m_zoom);
        world += snap.delta;
        guides = snap.guides;
    }
    setGuides(guides);
    // In the frame of the box before it was turned.
    const QPointF local = turned(world, c0, -m_frame0.rotation);
    double left = b0.left();
    double right = b0.right();
    double top = b0.top();
    double bottom = b0.bottom();
    const bool west = m_handle.contains(QLatin1Char('w'));
    const bool east = m_handle.contains(QLatin1Char('e'));
    const bool north = m_handle.contains(QLatin1Char('n'));
    const bool south = m_handle.contains(QLatin1Char('s'));
    if (west) {
        left = qMin(local.x(), right - 1);
    }
    if (east) {
        right = qMax(local.x(), left + 1);
    }
    if (north) {
        top = qMin(local.y(), bottom - 1);
    }
    if (south) {
        bottom = qMax(local.y(), top + 1);
    }
    bool keep = modifiers & ShiftMod;
    if (m_moving.size() == 1) {
        const Shape *s = m_before.find(m_moving.first());
        keep = keep != (s && s->kind == QLatin1String("image"));
    }
    if (keep && b0.width() > 0.5 && b0.height() > 0.5) {
        const double ratio = b0.width() / b0.height();
        double w = right - left;
        double h = bottom - top;
        if ((west || east) && (north || south)) {
            if (w / ratio > h) {
                h = w / ratio;
            } else {
                w = h * ratio;
            }
        } else if (west || east) {
            h = w / ratio;
        } else {
            w = h * ratio;
        }
        if (west) {
            left = right - w;
        } else if (east) {
            right = left + w;
        } else {
            left = b0.center().x() - w / 2;
            right = left + w;
        }
        if (north) {
            top = bottom - h;
        } else if (south) {
            bottom = top + h;
        } else {
            top = b0.center().y() - h / 2;
            bottom = top + h;
        }
    }
    QRectF box(left, top, right - left, bottom - top);
    if (m_moving.size() == 1 && m_before.find(m_moving.first()) && !m_before.find(m_moving.first())->isLine()) {
        const Shape *was = m_before.find(m_moving.first());
        Shape *s = m_doc.find(m_moving.first());
        if (!s) {
            return;
        }
        *s = *was;
        // The turn is about the centre, and the centre moves with the box:
        // put the new centre where the turn leaves the far corner still.
        const QPointF centre = turned(box.center(), c0, m_frame0.rotation);
        s->w = box.width();
        s->h = box.height();
        s->x = centre.x() - s->w / 2;
        s->y = centre.y() - s->h / 2;
        if (s->kind == QLatin1String("text")) {
            s->autoSize = false;
            fitText(*s);
        }
    } else {
        for (const QString &id : std::as_const(m_moving)) {
            const Shape *was = m_before.find(id);
            Shape *s = m_doc.find(id);
            if (was && s && !was->locked) {
                *s = scaled(*was, b0, box);
            }
        }
    }
    Geometry::updateConnectors(m_doc);
}

void Editor::dragRotate(const QPointF &world, int modifiers)
{
    const QPointF c = m_frame0.box.center();
    const auto angle = [&c](const QPointF &p) { return qRadiansToDegrees(std::atan2(p.y() - c.y(), p.x() - c.x())); };
    // The grip sits straight above the centre of the turned box.
    double turn = angle(world) + 90 - m_frame0.rotation;
    double target = m_frame0.rotation + turn;
    if (modifiers & ShiftMod) {
        target = std::round(target / 15) * 15;
    } else if (qAbs(std::fmod(std::fmod(target, 90) + 90, 90)) < 2 || qAbs(std::fmod(std::fmod(target, 90) + 90, 90)) > 88) {
        target = std::round(target / 90) * 90;
    }
    turn = target - m_frame0.rotation;
    for (const QString &id : std::as_const(m_moving)) {
        const Shape *was = m_before.find(id);
        Shape *s = m_doc.find(id);
        if (!was || !s) {
            continue;
        }
        *s = *was;
        if (s->isLine()) {
            s->p1 = turned(was->p1, c, turn);
            s->p2 = turned(was->p2, c, turn);
            syncLine(*s);
            continue;
        }
        const QPointF centre = turned(was->center(), c, turn);
        s->x = centre.x() - s->w / 2;
        s->y = centre.y() - s->h / 2;
        double r = std::fmod(was->rotation + turn, 360.0);
        if (r > 180) {
            r -= 360;
        } else if (r <= -180) {
            r += 360;
        }
        s->rotation = qAbs(r) < 0.001 ? 0 : r;
    }
    Geometry::updateConnectors(m_doc);
}

void Editor::dragCreate(const QPointF &pointer, int modifiers)
{
    Shape *s = m_doc.find(m_created);
    if (!s) {
        return;
    }
    QPointF world = pointer;
    QList<Geometry::Guide> guides;
    m_attach.clear();
    if (s->isLine()) {
        if (m_tool == QLatin1String("connector") && !(modifiers & ControlMod)) {
            m_attach = attachableAt(world, s->id);
            if (m_attach == s->from) {
                m_attach.clear();
            }
        }
        if (modifiers & ShiftMod) {
            world = constrained(m_pressWorld, world);
        }
        s->to = m_attach;
        s->p2 = world;
        syncLine(*s);
        Geometry::updateConnectors(m_doc);
        setGuides({});
        return;
    }
    if (m_doc.snap && !(modifiers & ControlMod)) {
        const Geometry::Snap snap = Geometry::snapPoint(world, snapTargets({m_created}), 6 / m_zoom);
        world += snap.delta;
        guides = snap.guides;
    }
    setGuides(guides);
    QPointF d = world - m_pressWorld;
    if (modifiers & ShiftMod) {
        const double side = qMax(qAbs(d.x()), qAbs(d.y()));
        d = QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }
    const QRectF box = QRectF(m_pressWorld, m_pressWorld + d).normalized();
    s->x = box.x();
    s->y = box.y();
    s->w = box.width();
    s->h = box.height();
    if (s->kind == QLatin1String("text")) {
        s->autoSize = false;
    }
}

void Editor::dragLineEnd(const QPointF &pointer, int modifiers)
{
    const Shape *was = m_before.find(m_moving.value(0));
    Shape *s = m_doc.find(m_moving.value(0));
    if (!was || !s) {
        return;
    }
    const bool first = m_handle == QLatin1String("p1");
    QPointF world = pointer;
    m_attach.clear();
    if (!(modifiers & ControlMod)) {
        m_attach = attachableAt(world, s->id);
        if (m_attach == (first ? s->to : s->from)) {
            m_attach.clear();
        }
    }
    if (m_attach.isEmpty() && (modifiers & ShiftMod)) {
        world = constrained(first ? was->p2 : was->p1, world);
    }
    if (first) {
        s->from = m_attach;
        s->p1 = world;
    } else {
        s->to = m_attach;
        s->p2 = world;
    }
    syncLine(*s);
    Geometry::updateConnectors(m_doc);
}

void Editor::hover(const QPointF &screen)
{
    if (m_spacePan || m_tool == QLatin1String("hand")) {
        setCursor(QStringLiteral("openHand"));
        return;
    }
    if (m_tool != QLatin1String("select")) {
        setCursor(m_tool == QLatin1String("text") ? QStringLiteral("text") : QStringLiteral("cross"));
        if (m_tool == QLatin1String("connector")) {
            const QString over = attachableAt(toWorld(screen), QString());
            if (over != m_attach) {
                m_attach = over;
                Q_EMIT overlayChanged();
            }
        }
        return;
    }
    const QString handle = handleAt(screen);
    QString over;
    if (handle.isEmpty()) {
        over = shapeAt(toWorld(screen));
        setCursor(over.isEmpty() ? QStringLiteral("arrow") : QStringLiteral("sizeAll"));
    } else if (handle == QLatin1String("rotate")) {
        setCursor(QStringLiteral("cross"));
    } else if (handle.startsWith(QLatin1Char('p'))) {
        setCursor(QStringLiteral("cross"));
    } else {
        // The grips turn with the box: pick the arrow nearest to how they lie now.
        static const QStringList order{QStringLiteral("e"), QStringLiteral("se"), QStringLiteral("s"), QStringLiteral("sw"),
                                       QStringLiteral("w"), QStringLiteral("nw"), QStringLiteral("n"), QStringLiteral("ne")};
        static const QStringList cursors{QStringLiteral("sizeH"), QStringLiteral("sizeF"), QStringLiteral("sizeV"), QStringLiteral("sizeB")};
        const int turnSteps = qRound(selectionFrame().rotation / 45.0);
        const int at = ((order.indexOf(handle) + turnSteps) % 8 + 8) % 8;
        setCursor(cursors.at(at % 4));
    }
    if (over != m_hover) {
        m_hover = over;
        Q_EMIT overlayChanged();
    }
}

void Editor::move(double x, double y, int modifiers)
{
    const QPointF screen(x, y);
    const QPointF world = toWorld(screen);
    m_pointer = world;
    Q_EMIT pointerChanged();
    const QPointF travelled = screen - m_pressScreen;
    const bool far = std::hypot(travelled.x(), travelled.y()) > 3;
    switch (m_drag) {
    case Drag::None:
        if (m_tool == QLatin1String("pen") && !m_pen.isEmpty()) {
            m_penHover = world;
            Q_EMIT overlayChanged();
        }
        hover(screen);
        return;
    case Drag::Pan:
        m_center = m_pressCenter - travelled / m_zoom;
        m_fitPending = false;
        Q_EMIT viewChanged();
        Q_EMIT overlayChanged();
        return;
    case Drag::Maybe:
        if (!far) {
            return;
        }
        m_moving = movable();
        if (m_moving.isEmpty()) {
            m_drag = Drag::None;
            return;
        }
        begin();
        m_drag = Drag::Move;
        [[fallthrough]];
    case Drag::Move:
        m_moved = true;
        dragMove(world, modifiers);
        break;
    case Drag::Resize:
        m_moved = true;
        dragResize(world, modifiers);
        break;
    case Drag::Rotate:
        m_moved = true;
        dragRotate(world, modifiers);
        break;
    case Drag::LineEnd:
        m_moved = true;
        dragLineEnd(world, modifiers);
        break;
    case Drag::Create:
        if (far) {
            m_moved = true;
        }
        if (m_moved) {
            dragCreate(world, modifiers);
        }
        break;
    case Drag::Marquee: {
        m_marquee = QRectF(m_pressWorld, world).normalized();
        QStringList ids = m_marqueeBase;
        for (const Shape &s : m_doc.shapes) {
            if (s.hidden || s.locked || ids.contains(s.id)) {
                continue;
            }
            const QRectF b = s.bounds();
            // A frame is taken only whole, or every sweep on it would take it.
            if (s.isFrame() ? m_marquee.contains(b) : m_marquee.intersects(b.isEmpty() ? b.adjusted(-0.5, -0.5, 0.5, 0.5) : b)) {
                ids << s.id;
            }
        }
        setSelection(ids);
        Q_EMIT overlayChanged();
        return;
    }
    case Drag::Pencil:
        if (m_stroke.isEmpty() || QLineF(toScreen(m_stroke.last()), screen).length() >= 1.5) {
            m_stroke << world;
            Q_EMIT overlayChanged();
        }
        return;
    case Drag::PenHandle:
        if (far && !m_pen.isEmpty()) {
            m_pen.last().out = world;
            m_pen.last().in = m_pen.last().p * 2 - world;
            Q_EMIT overlayChanged();
        }
        return;
    }
    ++m_revision;
    Q_EMIT changed();
    Q_EMIT overlayChanged();
    Q_EMIT infoChanged();
}

void Editor::release(double x, double y, int modifiers)
{
    Q_UNUSED(modifiers)
    const QPointF world = toWorld(QPointF(x, y));
    const Drag drag = m_drag;
    m_drag = Drag::None;
    m_guides.clear();
    m_attach.clear();
    switch (drag) {
    case Drag::None:
        break;
    case Drag::Pan:
        setCursor(m_tool == QLatin1String("hand") || m_spacePan ? QStringLiteral("openHand") : QStringLiteral("arrow"));
        break;
    case Drag::Maybe:
        // A click on one of several picks just that one.
        if (m_selection.size() > 1 && !m_pressed.isEmpty()) {
            setSelection({m_pressed});
        }
        break;
    case Drag::Move:
        commit(QStringLiteral("Mover"));
        break;
    case Drag::Resize:
        commit(QStringLiteral("Cambiar tamaño"));
        break;
    case Drag::Rotate:
        commit(QStringLiteral("Girar"));
        break;
    case Drag::LineEnd:
        commit(QStringLiteral("Mover extremo"));
        break;
    case Drag::Marquee:
        m_marquee = QRectF();
        break;
    case Drag::Pencil: {
        const QList<QPointF> points = m_stroke;
        m_stroke.clear();
        if (points.size() >= 2) {
            begin();
            Shape s = prototype(QStringLiteral("pencil"));
            s.style.strokeWidth = 3;
            s.path = Geometry::smooth(points, 1.2 / m_zoom);
            s.ref = s.path.boundingRect();
            // A dead straight stroke has no height: give its box some.
            if (s.ref.width() < 0.01) {
                s.ref.adjust(-0.5, 0, 0.5, 0);
            }
            if (s.ref.height() < 0.01) {
                s.ref.adjust(0, -0.5, 0, 0.5);
            }
            s.x = s.ref.x();
            s.y = s.ref.y();
            s.w = s.ref.width();
            s.h = s.ref.height();
            s.id = m_doc.freshId();
            s.name = m_doc.freshName(Geometry::baseName(s));
            m_doc.shapes << s;
            commit(QStringLiteral("Dibujar trazo"));
        }
        break;
    }
    case Drag::PenHandle:
        m_penHover = world;
        break;
    case Drag::Create: {
        Shape *s = m_doc.find(m_created);
        if (!s) {
            cancel();
            break;
        }
        const QString id = m_created;
        m_created.clear();
        if (!m_moved) {
            // A click with no drag: a line is nothing, anything else gets
            // the size it usually has, around the click.
            if (s->isLine()) {
                cancel();
                break;
            }
            QSizeF size(150, 100);
            if (s->kind == QLatin1String("note")) {
                size = QSizeF(180, 180);
            } else if (s->isFrame()) {
                size = QSizeF(1280, 720);
            } else if (s->kind == QLatin1String("shape") && (s->form == QLatin1String("polygon") || s->form == QLatin1String("star") || s->form == QLatin1String("ellipse"))) {
                size = QSizeF(120, 120);
            }
            if (s->kind == QLatin1String("text")) {
                s->autoSize = true;
                fitText(*s);
                s->y -= s->h / 2;
            } else {
                s->w = size.width();
                s->h = size.height();
                s->x = m_pressWorld.x() - s->w / 2;
                s->y = m_pressWorld.y() - s->h / 2;
            }
        } else if (s->kind == QLatin1String("text")) {
            fitText(*s);
        } else if (!s->isLine() && (s->w < 2 || s->h < 2)) {
            s->w = qMax(s->w, 2.0);
            s->h = qMax(s->h, 2.0);
        }
        const bool words = s->kind == QLatin1String("text") || s->kind == QLatin1String("note");
        const QString what = Geometry::baseName(*s).toLower();
        commit(QStringLiteral("Dibujar %1").arg(what));
        setSelection({id});
        setTool(QStringLiteral("select"));
        if (words) {
            setEditing(id);
            Q_EMIT editRequested(id);
        }
        break;
    }
    }
    Q_EMIT overlayChanged();
}

void Editor::doubleClick(double x, double y)
{
    if (m_tool == QLatin1String("pen")) {
        // The second click of the pair laid a node on top of the last.
        if (m_pen.size() >= 2) {
            m_pen.removeLast();
        }
        finishPen(false);
        return;
    }
    if (m_tool != QLatin1String("select")) {
        return;
    }
    const QString id = shapeAt(toWorld(QPointF(x, y)));
    const Shape *s = m_doc.find(id);
    if (!s || s->kind == QLatin1String("image") || s->kind == QLatin1String("path") || s->isFrame()) {
        return;
    }
    if (m_depth > 0) {
        cancel();
    }
    m_drag = Drag::None;
    setSelection({id});
    setEditing(id);
    Q_EMIT editRequested(id);
}

void Editor::finishPen(bool close)
{
    const QList<PenNode> nodes = m_pen;
    m_pen.clear();
    m_drag = Drag::None;
    if (nodes.size() < 2) {
        Q_EMIT overlayChanged();
        return;
    }
    QPainterPath path;
    path.moveTo(nodes.first().p);
    const auto segment = [&path](const PenNode &a, const PenNode &b) {
        if (a.out == a.p && b.in == b.p) {
            path.lineTo(b.p);
        } else {
            path.cubicTo(a.out, b.in, b.p);
        }
    };
    for (int i = 1; i < nodes.size(); ++i) {
        segment(nodes.at(i - 1), nodes.at(i));
    }
    if (close) {
        segment(nodes.last(), nodes.first());
        path.closeSubpath();
    }
    begin();
    Shape s = prototype(QStringLiteral("pen"));
    if (close) {
        s.style.fillKind = QStringLiteral("solid");
        s.style.fill = m_defaults.fill;
        s.style.stroke = m_defaults.stroke;
    }
    s.path = path;
    s.ref = path.boundingRect();
    if (s.ref.width() < 0.01) {
        s.ref.adjust(-0.5, 0, 0.5, 0);
    }
    if (s.ref.height() < 0.01) {
        s.ref.adjust(0, -0.5, 0, 0.5);
    }
    s.x = s.ref.x();
    s.y = s.ref.y();
    s.w = s.ref.width();
    s.h = s.ref.height();
    s.id = m_doc.freshId();
    s.name = m_doc.freshName(Geometry::baseName(s));
    m_doc.shapes << s;
    const QString id = s.id;
    commit(QStringLiteral("Dibujar trazo"));
    setSelection({id});
    setTool(QStringLiteral("select"));
}

void Editor::escape()
{
    if (m_drag != Drag::None && m_drag != Drag::PenHandle) {
        m_drag = Drag::None;
        m_stroke.clear();
        m_marquee = QRectF();
        m_guides.clear();
        m_attach.clear();
        m_created.clear();
        cancel();
        return;
    }
    if (!m_pen.isEmpty()) {
        finishPen(false);
        return;
    }
    if (m_tool != QLatin1String("select")) {
        setTool(QStringLiteral("select"));
        return;
    }
    clearSelection();
}
