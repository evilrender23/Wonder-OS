#include "canvas.h"

#include "render.h"

#include <QCursor>
#include <QPainter>
#include <QtMath>

#include <cmath>

Canvas::Canvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    setAntialiasing(true);
    setOpaquePainting(true);
    connect(this, &Canvas::lookChanged, this, [this] { update(); });
}

void Canvas::setEditor(Editor *editor)
{
    if (editor == m_editor) {
        return;
    }
    if (m_editor) {
        m_editor->disconnect(this);
    }
    m_editor = editor;
    if (m_editor) {
        const auto repaint = [this] { update(); };
        connect(m_editor, &Editor::changed, this, repaint);
        connect(m_editor, &Editor::viewChanged, this, repaint);
        connect(m_editor, &Editor::overlayChanged, this, repaint);
        connect(m_editor, &Editor::selectionChanged, this, repaint);
        connect(m_editor, &Editor::settingsChanged, this, repaint);
        connect(m_editor, &Editor::editingChanged, this, repaint);
        connect(m_editor, &Editor::toolChanged, this, repaint);
        connect(m_editor, &Editor::cursorChanged, this, &Canvas::applyCursor);
        m_editor->setViewport(width(), height());
    }
    Q_EMIT editorChanged();
    update();
}

void Canvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_editor) {
        m_editor->setViewport(newGeometry.width(), newGeometry.height());
    }
}

void Canvas::applyCursor()
{
    static const QHash<QString, Qt::CursorShape> shapes{
        {QStringLiteral("arrow"), Qt::ArrowCursor},
        {QStringLiteral("cross"), Qt::CrossCursor},
        {QStringLiteral("openHand"), Qt::OpenHandCursor},
        {QStringLiteral("closedHand"), Qt::ClosedHandCursor},
        {QStringLiteral("move"), Qt::SizeAllCursor},
        {QStringLiteral("text"), Qt::IBeamCursor},
        {QStringLiteral("sizeH"), Qt::SizeHorCursor},
        {QStringLiteral("sizeV"), Qt::SizeVerCursor},
        {QStringLiteral("sizeF"), Qt::SizeFDiagCursor},
        {QStringLiteral("sizeB"), Qt::SizeBDiagCursor},
        {QStringLiteral("pointing"), Qt::PointingHandCursor},
    };
    setCursor(QCursor(shapes.value(m_editor ? m_editor->cursor() : QString(), Qt::ArrowCursor)));
}

void Canvas::paint(QPainter *painter)
{
    painter->fillRect(QRectF(0, 0, width(), height()), m_paper);
    if (!m_editor) {
        return;
    }
    const double zoom = m_editor->zoom();
    const QRectF visible(m_editor->toWorld(QPointF(0, 0)), m_editor->toWorld(QPointF(width(), height())));

    // The grid: dots that thin out as the view pulls back, so they never crowd.
    if (m_editor->grid()) {
        double step = 20;
        while (step * zoom < 14) {
            step *= 5;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_dots);
        const double r = 1.1;
        for (double wy = std::floor(visible.top() / step) * step; wy < visible.bottom(); wy += step) {
            for (double wx = std::floor(visible.left() / step) * step; wx < visible.right(); wx += step) {
                const QPointF p = m_editor->toScreen(QPointF(wx, wy));
                painter->drawEllipse(p, r, r);
            }
        }
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    const QPointF origin = m_editor->toScreen(QPointF(0, 0));
    painter->translate(origin);
    painter->scale(zoom, zoom);
    Render::Options options;
    options.frameChrome = true;
    options.zoom = zoom;
    options.chromeColor = m_chrome;
    options.editing = m_editor->editing();
    Render::paint(painter, m_editor->document(), visible, options);

    // --- Over the drawing, still in its coordinates ---
    const auto hair = [&](const QColor &color, double width = 1.5) {
        QPen pen(color, width / zoom);
        pen.setCosmetic(false);
        pen.setJoinStyle(Qt::RoundJoin);
        return pen;
    };
    const Document &doc = m_editor->document();
    painter->setBrush(Qt::NoBrush);
    for (const QString &id : {m_editor->hovered(), m_editor->attachTarget()}) {
        const Shape *s = doc.find(id);
        if (!s || m_editor->selection().contains(id)) {
            continue;
        }
        painter->setPen(hair(m_accent, id == m_editor->attachTarget() ? 3 : 1.5));
        if (s->isLine()) {
            painter->drawPath(Geometry::linePath(*s));
        } else {
            painter->drawPath(Geometry::worldOutline(*s));
        }
    }
    painter->setPen(hair(m_accent, 1.5));
    const QList<QPolygonF> outlines = m_editor->selectionOutlines();
    for (const QPolygonF &outline : outlines) {
        if (outline.size() > 2) {
            painter->drawPolygon(outline);
        }
    }
    for (const QString &id : m_editor->selection()) {
        const Shape *s = doc.find(id);
        if (s && s->isLine()) {
            painter->drawPath(Geometry::linePath(*s));
        }
    }
    const QPainterPath pen = m_editor->penPreview();
    if (!pen.isEmpty()) {
        painter->setPen(hair(m_accent, 2));
        painter->drawPath(pen);
    }
    const QList<QPointF> stroke = m_editor->strokePreview();
    if (stroke.size() > 1) {
        painter->setPen(hair(m_accent, 2));
        painter->drawPolyline(QPolygonF(stroke));
    }
    const QRectF marquee = m_editor->marquee();
    if (!marquee.isNull() && (marquee.width() > 0 || marquee.height() > 0)) {
        QColor wash = m_accent;
        wash.setAlpha(36);
        painter->setPen(hair(m_accent, 1));
        painter->setBrush(wash);
        painter->drawRect(marquee);
    }
    painter->restore();

    // --- On the screen: guides, grips ---
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor guide(QStringLiteral("#ff3b6b"));
    for (const Geometry::Guide &g : m_editor->guides()) {
        const QPointF a = m_editor->toScreen(g.line.p1());
        const QPointF b = m_editor->toScreen(g.line.p2());
        painter->setPen(QPen(guide, 1));
        painter->drawLine(a, b);
        if (!g.label.isEmpty()) {
            QFont font = painter->font();
            font.setPixelSize(10);
            painter->setFont(font);
            const QRectF box = QRectF(0, 0, QFontMetricsF(font).horizontalAdvance(g.label) + 10, 16).translated((a + b) / 2 - QPointF(0, 20));
            painter->setPen(Qt::NoPen);
            painter->setBrush(guide);
            painter->drawRoundedRect(box.translated(-box.width() / 2, 0), 4, 4);
            painter->setPen(Qt::white);
            painter->drawText(box.translated(-box.width() / 2, 0), Qt::AlignCenter, g.label);
        }
    }
    for (const QPointF &anchor : m_editor->penAnchors()) {
        const QPointF p = m_editor->toScreen(anchor);
        painter->setPen(QPen(m_accent, 1.5));
        painter->setBrush(Qt::white);
        painter->drawRect(QRectF(p.x() - 3.5, p.y() - 3.5, 7, 7));
    }
    const QList<Editor::Handle> handles = m_editor->handles();
    QPointF top;
    QPointF turn;
    bool hasTop = false;
    for (const Editor::Handle &h : handles) {
        if (h.role == QLatin1String("rotate")) {
            turn = h.pos;
        }
    }
    // The stem of the turning grip, from the middle of the top edge.
    QPointF nw;
    QPointF ne;
    for (const Editor::Handle &h : handles) {
        if (h.role == QLatin1String("nw")) {
            nw = h.pos;
            hasTop = true;
        } else if (h.role == QLatin1String("ne")) {
            ne = h.pos;
        }
    }
    if (hasTop && m_editor->selection().size() > 1) {
        // Several at once: the box their grips stand on.
        QPolygonF box;
        for (const char *role : {"nw", "ne", "se", "sw"}) {
            for (const Editor::Handle &h : handles) {
                if (h.role == QLatin1String(role)) {
                    box << h.pos;
                }
            }
        }
        QPen dashes(m_accent, 1);
        dashes.setDashPattern({4, 3});
        painter->setPen(dashes);
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(box);
    }
    if (hasTop) {
        top = (nw + ne) / 2;
        painter->setPen(QPen(m_accent, 1));
        painter->drawLine(top, turn);
    }
    for (const Editor::Handle &h : handles) {
        painter->setPen(QPen(m_accent, 1.5));
        painter->setBrush(Qt::white);
        if (h.role == QLatin1String("rotate") || h.role == QLatin1String("p1") || h.role == QLatin1String("p2")) {
            painter->drawEllipse(h.pos, 5, 5);
        } else {
            painter->drawRoundedRect(QRectF(h.pos.x() - 4.5, h.pos.y() - 4.5, 9, 9), 2, 2);
        }
    }
}

static int buttonOf(Qt::MouseButton button)
{
    return button == Qt::LeftButton ? 1 : button == Qt::MiddleButton ? 4 : 2;
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus();
    if (!m_editor) {
        return;
    }
    m_editor->press(event->position().x(), event->position().y(), buttonOf(event->button()), int(event->modifiers()));
    if (event->button() == Qt::RightButton) {
        Q_EMIT menuRequested(event->position().x(), event->position().y());
    }
    event->accept();
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_editor) {
        m_editor->move(event->position().x(), event->position().y(), int(event->modifiers()));
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_editor) {
        m_editor->release(event->position().x(), event->position().y(), int(event->modifiers()));
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_editor && event->button() == Qt::LeftButton) {
        m_editor->doubleClick(event->position().x(), event->position().y());
    }
}

void Canvas::hoverMoveEvent(QHoverEvent *event)
{
    if (m_editor) {
        m_editor->move(event->position().x(), event->position().y(), int(event->modifiers()));
    }
}

void Canvas::wheelEvent(QWheelEvent *event)
{
    if (!m_editor) {
        return;
    }
    // A touchpad says pixels; a wheel says eighths of a degree.
    QPointF delta = event->pixelDelta();
    if (delta.isNull()) {
        delta = QPointF(event->angleDelta()) / 1.2;
    } else {
        delta /= 0.6;
    }
    m_editor->wheel(event->position().x(), event->position().y(), delta.x(), delta.y(), int(event->modifiers()));
    event->accept();
}

// --- The minimap ----------------------------------------------------------------

Minimap::Minimap(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    connect(this, &Minimap::lookChanged, this, [this] { update(); });
}

void Minimap::setEditor(Editor *editor)
{
    if (editor == m_editor) {
        return;
    }
    if (m_editor) {
        m_editor->disconnect(this);
    }
    m_editor = editor;
    if (m_editor) {
        const auto repaint = [this] { update(); };
        connect(m_editor, &Editor::changed, this, repaint);
        connect(m_editor, &Editor::viewChanged, this, repaint);
    }
    Q_EMIT editorChanged();
    update();
}

QRectF Minimap::world() const
{
    const QSizeF viewport = m_editor->viewport();
    const QRectF view(m_editor->toWorld(QPointF(0, 0)), m_editor->toWorld(QPointF(viewport.width(), viewport.height())));
    QRectF all = m_editor->document().bounds();
    all = all.isNull() ? view : all.united(view);
    const double margin = qMax(all.width(), all.height()) * 0.05;
    return all.adjusted(-margin, -margin, margin, margin);
}

QTransform Minimap::toItem() const
{
    const QRectF w = world();
    const double scale = qMin(width() / w.width(), height() / w.height());
    QTransform t;
    t.translate((width() - w.width() * scale) / 2, (height() - w.height() * scale) / 2);
    t.scale(scale, scale);
    t.translate(-w.x(), -w.y());
    return t;
}

void Minimap::paint(QPainter *painter)
{
    if (!m_editor || width() < 4 || height() < 4) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);
    const QTransform t = toItem();
    painter->save();
    painter->setTransform(t, true);
    // Blocks, not the real thing: at this size the look is all that reads.
    for (const Shape &s : m_editor->document().shapes) {
        if (s.hidden) {
            continue;
        }
        if (s.isLine()) {
            QColor ink = s.style.stroke;
            ink.setAlpha(150);
            painter->setPen(QPen(ink, 1.2 / t.m11()));
            painter->drawPath(Geometry::linePath(s));
            continue;
        }
        painter->setPen(Qt::NoPen);
        if (s.isFrame()) {
            painter->setBrush(s.style.fill);
            painter->setPen(QPen(QColor(128, 128, 128, 110), 1 / t.m11()));
            painter->drawRect(s.box());
            continue;
        }
        QColor fill = s.style.fillKind == QLatin1String("none") ? s.style.stroke : s.style.fill;
        if (s.kind == QLatin1String("text")) {
            fill = s.style.textColor;
            fill.setAlpha(90);
        } else if (s.kind == QLatin1String("image")) {
            fill = QColor(128, 128, 128, 140);
        } else if (fill.lightness() > 235) {
            // White on white paper would not show.
            fill = QColor(150, 150, 156, 150);
        }
        painter->setBrush(fill);
        painter->drawPath(Geometry::worldOutline(s));
    }
    painter->restore();
    const QSizeF viewport = m_editor->viewport();
    const QRectF view = t.mapRect(QRectF(m_editor->toWorld(QPointF(0, 0)), m_editor->toWorld(QPointF(viewport.width(), viewport.height()))));
    QColor wash = m_accent;
    wash.setAlpha(30);
    painter->setBrush(wash);
    painter->setPen(QPen(m_accent, 1.5));
    painter->drawRoundedRect(view.intersected(QRectF(1, 1, width() - 2, height() - 2)), 3, 3);
}

void Minimap::go(const QPointF &at)
{
    if (!m_editor) {
        return;
    }
    const QPointF world = toItem().inverted().map(at);
    m_editor->centerOn(world.x(), world.y());
}

void Minimap::mousePressEvent(QMouseEvent *event)
{
    go(event->position());
    event->accept();
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    go(event->position());
}
