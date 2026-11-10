#include "deck.h"

#include "odp.h"
#include "outline.h"
#include "pptx.h"
#include "render.h"
#include "richtext.h"

#include <QBuffer>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QSaveFile>
#include <QTransform>

#include <algorithm>
#include <cmath>

using namespace Model;

static const QString clipboardType = QStringLiteral("application/x-wonderland-diapositivas");

// --- Commands --------------------------------------------------------------------

namespace {

class SlideChange : public Command
{
public:
    SlideChange(int index, const Slide &before, const Slide &after)
        : index(index), before(before), after(after) {}
    void undo(Presentation &deck) override { deck.slides[index] = before; }
    void redo(Presentation &deck) override { deck.slides[index] = after; }
    int slideAfterUndo() const override { return index; }
    int slideAfterRedo() const override { return index; }
    int index;
    Slide before;
    Slide after;
};

class SlideInsert : public Command
{
public:
    SlideInsert(int index, const Slide &slide)
        : index(index), slide(slide) {}
    void undo(Presentation &deck) override { deck.slides.removeAt(index); }
    void redo(Presentation &deck) override { deck.slides.insert(index, slide); }
    int slideAfterUndo() const override { return qMax(0, index - 1); }
    int slideAfterRedo() const override { return index; }
    int index;
    Slide slide;
};

class SlideRemove : public Command
{
public:
    SlideRemove(int index, const Slide &slide)
        : index(index), slide(slide) {}
    void undo(Presentation &deck) override { deck.slides.insert(index, slide); }
    void redo(Presentation &deck) override { deck.slides.removeAt(index); }
    int slideAfterUndo() const override { return index; }
    int slideAfterRedo() const override { return qMax(0, index - 1); }
    int index;
    Slide slide;
};

class SlideMove : public Command
{
public:
    SlideMove(int from, int to)
        : from(from), to(to) {}
    void undo(Presentation &deck) override { deck.slides.move(to, from); }
    void redo(Presentation &deck) override { deck.slides.move(from, to); }
    int slideAfterUndo() const override { return from; }
    int slideAfterRedo() const override { return to; }
    int from;
    int to;
};

/// The theme, the master or the size of the slides: the whole deck before and after.
class DeckChange : public Command
{
public:
    DeckChange(const Presentation &before, const Presentation &after, int slide)
        : before(before), after(after), slide(slide) {}
    void undo(Presentation &deck) override { apply(deck, before); }
    void redo(Presentation &deck) override { apply(deck, after); }
    int slideAfterUndo() const override { return slide; }
    int slideAfterRedo() const override { return slide; }
    static void apply(Presentation &deck, const Presentation &from)
    {
        // The media only ever grow: what a later step added stays reachable.
        const QHash<QString, QByteArray> media = deck.media;
        deck = from;
        deck.media = media;
    }
    Presentation before;
    Presentation after;
    int slide;
};

double normalAngle(double degrees)
{
    double angle = std::fmod(degrees, 360.0);
    if (angle < 0) {
        angle += 360;
    }
    return angle;
}

}

// --- The deck --------------------------------------------------------------------

Deck::Deck(QObject *parent)
    : QObject(parent)
{
    reset();
}

Deck::~Deck() = default;

void Deck::bump()
{
    ++m_revision;
    Q_EMIT changed();
}

void Deck::touchStack()
{
    Q_EMIT stackChanged();
}

QString Deck::undoText() const
{
    return m_undo.empty() ? QString() : m_undo.back()->text;
}

QString Deck::redoText() const
{
    return m_redo.empty() ? QString() : m_redo.back()->text;
}

QString Deck::name() const
{
    return m_name.isEmpty() ? QStringLiteral("Sin título") : m_name;
}

QString Deck::aspect() const
{
    return std::abs(m_data.width / m_data.height - 4.0 / 3.0) < 0.02 ? QStringLiteral("4:3") : QStringLiteral("16:9");
}

Slide *Deck::now()
{
    return m_current >= 0 && m_current < m_data.slides.size() ? &m_data.slides[m_current] : nullptr;
}

const Slide *Deck::now() const
{
    return m_current >= 0 && m_current < m_data.slides.size() ? &m_data.slides.at(m_current) : nullptr;
}

Item *Deck::find(const QString &id)
{
    Slide *slide = now();
    const int index = slide ? slide->indexOf(id) : -1;
    return index < 0 ? nullptr : &slide->items[index];
}

const Item *Deck::find(const QString &id) const
{
    const Slide *slide = now();
    const int index = slide ? slide->indexOf(id) : -1;
    return index < 0 ? nullptr : &slide->items.at(index);
}

void Deck::setData(const Presentation &data)
{
    m_data = data;
    m_undo.clear();
    m_redo.clear();
    m_savedAt = 0;
    m_current = 0;
    m_selection.clear();
    if (m_data.slides.isEmpty()) {
        m_data.slides << newSlide(QStringLiteral("title"), m_data.size());
    }
    bump();
    Q_EMIT currentChanged();
    Q_EMIT selectionChanged();
    touchStack();
}

void Deck::setCurrent(int index)
{
    index = qBound(0, index, qMax(0, count() - 1));
    if (index == m_current) {
        return;
    }
    m_current = index;
    setSelection({});
    Q_EMIT currentChanged();
}

void Deck::push(std::unique_ptr<Command> command)
{
    m_redo.clear();
    if (m_savedAt > int(m_undo.size())) {
        m_savedAt = -1; // The saved state was among the steps just dropped.
    }
    // A slider dragged is a burst of steps on the same thing: keep the first «before».
    if (!command->mergeKey.isEmpty() && !m_undo.empty() && m_undo.back()->mergeKey == command->mergeKey
        && m_lastPush.isValid() && m_lastPush.elapsed() < 1200 && m_savedAt != int(m_undo.size())) {
        auto *lastDeck = dynamic_cast<DeckChange *>(m_undo.back().get());
        auto *nextDeck = dynamic_cast<DeckChange *>(command.get());
        if (lastDeck && nextDeck) {
            lastDeck->after = nextDeck->after;
            m_lastPush.restart();
            bump();
            touchStack();
            return;
        }
        auto *last = dynamic_cast<SlideChange *>(m_undo.back().get());
        auto *next = dynamic_cast<SlideChange *>(command.get());
        if (last && next && last->index == next->index) {
            last->after = next->after;
            m_lastPush.restart();
            bump();
            touchStack();
            return;
        }
    }
    m_undo.push_back(std::move(command));
    m_lastPush.restart();
    bump();
    touchStack();
}

void Deck::edit(const QString &text, const std::function<void(Slide &)> &change, const QString &mergeKey)
{
    Slide *slide = now();
    if (!slide) {
        return;
    }
    const Slide before = *slide;
    change(*slide);
    if (m_dragging) {
        bump();
        return;
    }
    if (*slide == before) {
        return;
    }
    auto command = std::make_unique<SlideChange>(m_current, before, *slide);
    command->text = text;
    command->mergeKey = mergeKey.isEmpty() ? QString() : mergeKey + QLatin1Char('|') + m_selection.join(QLatin1Char(','));
    push(std::move(command));
    pruneSelection();
}

void Deck::undo()
{
    if (m_undo.empty() || m_dragging) {
        return;
    }
    std::unique_ptr<Command> command = std::move(m_undo.back());
    m_undo.pop_back();
    command->undo(m_data);
    const int show = command->slideAfterUndo();
    m_redo.push_back(std::move(command));
    m_lastPush.invalidate();
    m_current = qBound(0, show, qMax(0, count() - 1));
    bump();
    Q_EMIT currentChanged();
    pruneSelection();
    touchStack();
}

void Deck::redo()
{
    if (m_redo.empty() || m_dragging) {
        return;
    }
    std::unique_ptr<Command> command = std::move(m_redo.back());
    m_redo.pop_back();
    command->redo(m_data);
    const int show = command->slideAfterRedo();
    m_undo.push_back(std::move(command));
    m_lastPush.invalidate();
    m_current = qBound(0, show, qMax(0, count() - 1));
    bump();
    Q_EMIT currentChanged();
    pruneSelection();
    touchStack();
}

// --- Files -----------------------------------------------------------------------

void Deck::reset(const QString &theme, const QString &aspect)
{
    Presentation fresh;
    fresh.theme = Model::theme(theme).id;
    if (aspect == QLatin1String("4:3")) {
        fresh.width = 720;
    }
    fresh.slides << newSlide(QStringLiteral("title"), fresh.size());
    m_path.clear();
    m_name.clear();
    m_error.clear();
    setData(fresh);
    Q_EMIT fileChanged();
}

bool Deck::load(const QString &path)
{
    QFile file(path);
    const QFileInfo info(path);
    m_error.clear();
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("No se puede abrir «%1».").arg(info.fileName());
        Q_EMIT fileChanged();
        return false;
    }
    const QByteArray bytes = file.readAll();
    Presentation loaded;
    QString why;
    const bool odp = info.suffix().compare(QLatin1String("odp"), Qt::CaseInsensitive) == 0;
    const bool ok = odp ? Odp::read(bytes, loaded, &why) : Pptx::read(bytes, loaded, &why);
    if (!ok) {
        m_error = QStringLiteral("«%1» no se puede leer: %2").arg(info.fileName(), why.isEmpty() ? QStringLiteral("no es una presentación") : why);
        Q_EMIT fileChanged();
        return false;
    }
    m_path = info.absoluteFilePath();
    m_name = info.completeBaseName();
    setData(loaded);
    Q_EMIT fileChanged();
    return true;
}

bool Deck::save(const QString &path)
{
    m_error.clear();
    QString target = path;
    const QString suffix = QFileInfo(target).suffix().toLower();
    if (suffix != QLatin1String("pptx") && suffix != QLatin1String("odp")) {
        target += QStringLiteral(".pptx");
    }
    const QByteArray bytes = this->bytes(QFileInfo(target).suffix().toLower());
    QSaveFile file(target);
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = QStringLiteral("No se pudo guardar en «%1».").arg(QFileInfo(target).fileName());
        Q_EMIT fileChanged();
        return false;
    }
    m_path = QFileInfo(target).absoluteFilePath();
    m_name = QFileInfo(target).completeBaseName();
    m_savedAt = int(m_undo.size());
    m_lastPush.invalidate();
    Q_EMIT fileChanged();
    touchStack();
    return true;
}

bool Deck::exportPdf(const QString &path)
{
    return Render::savePdf(m_data, path);
}

int Deck::exportPng(const QString &path)
{
    const QFileInfo info(path);
    const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    int written = 0;
    const int digits = count() > 99 ? 3 : 2;
    for (int i = 0; i < count(); ++i) {
        if (m_data.slides.at(i).hidden) {
            continue;
        }
        const QString file = QStringLiteral("%1-%2.png").arg(base).arg(i + 1, digits, 10, QLatin1Char('0'));
        if (Render::savePng(m_data, i, file)) {
            ++written;
        }
    }
    return written;
}

QByteArray Deck::bytes(const QString &suffix) const
{
    return suffix == QLatin1String("odp") ? Odp::write(m_data) : Pptx::write(m_data);
}

void Deck::adopt(const QString &path, const QString &name)
{
    m_path = path;
    m_name = name;
    m_savedAt = -1;
    Q_EMIT fileChanged();
    touchStack();
}

void Deck::setName(const QString &name)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty() || clean == m_name) {
        return;
    }
    m_name = clean;
    Q_EMIT fileChanged();
}

void Deck::restore(const Presentation &data, const QString &text)
{
    const Presentation before = m_data;
    for (auto it = data.media.constBegin(); it != data.media.constEnd(); ++it) {
        m_data.media.insert(it.key(), it.value());
    }
    Presentation after = data;
    after.media = m_data.media;
    if (after.slides.isEmpty()) {
        after.slides << newSlide(QStringLiteral("title"), after.size());
    }
    m_data = after;
    m_current = qBound(0, m_current, count() - 1);
    setSelection({});
    auto command = std::make_unique<DeckChange>(before, m_data, m_current);
    command->text = text;
    push(std::move(command));
    Q_EMIT currentChanged();
}

// --- The outline -----------------------------------------------------------------

QString Deck::outline() const
{
    return Outline::fromDeck(m_data);
}

void Deck::setOutline(const QString &markdown)
{
    const Presentation before = m_data;
    Outline::apply(m_data, markdown);
    if (m_data == before) {
        return;
    }
    const bool moved = m_current >= count();
    m_current = qBound(0, m_current, count() - 1);
    auto command = std::make_unique<DeckChange>(before, m_data, m_current);
    command->text = QStringLiteral("Editar esquema");
    command->mergeKey = QStringLiteral("outline");
    push(std::move(command));
    pruneSelection();
    if (moved) {
        Q_EMIT currentChanged();
    }
}

// --- Slides ----------------------------------------------------------------------

QVariantMap Deck::slide(int index) const
{
    if (index < 0 || index >= count()) {
        return {};
    }
    const Slide &slide = m_data.slides.at(index);
    return {
        {QStringLiteral("id"), slide.id},
        {QStringLiteral("layout"), slide.layout},
        {QStringLiteral("hidden"), slide.hidden},
        {QStringLiteral("notes"), slide.notes},
        {QStringLiteral("transition"), slide.transition},
        {QStringLiteral("background"), slide.background},
        {QStringLiteral("builds"), int(slide.builds().size())},
        {QStringLiteral("buildIds"), slide.builds()},
    };
}

QVariantList Deck::layouts() const
{
    QVariantList out;
    for (const QString &layout : Model::layouts()) {
        out << QVariantMap{{QStringLiteral("id"), layout}, {QStringLiteral("name"), layoutName(layout)}};
    }
    return out;
}

QVariantList Deck::shown() const
{
    QVariantList out;
    for (int i = 0; i < count(); ++i) {
        if (!m_data.slides.at(i).hidden) {
            out << i;
        }
    }
    return out;
}

int Deck::addSlide(const QString &layout, int at)
{
    const int index = at < 0 || at > count() ? qMin(count(), m_current + 1) : at;
    auto command = std::make_unique<SlideInsert>(index, newSlide(layout, m_data.size()));
    command->text = QStringLiteral("Añadir diapositiva");
    command->redo(m_data);
    m_current = index;
    setSelection({});
    push(std::move(command));
    Q_EMIT currentChanged();
    return index;
}

void Deck::duplicateSlide(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    Slide copy = m_data.slides.at(index);
    copy.id = newId();
    QHash<QString, QString> groups;
    for (Item &item : copy.items) {
        item.id = newId();
        if (!item.group.isEmpty()) {
            if (!groups.contains(item.group)) {
                groups.insert(item.group, newId());
            }
            item.group = groups.value(item.group);
        }
    }
    auto command = std::make_unique<SlideInsert>(index + 1, copy);
    command->text = QStringLiteral("Duplicar diapositiva");
    command->redo(m_data);
    m_current = index + 1;
    setSelection({});
    push(std::move(command));
    Q_EMIT currentChanged();
}

void Deck::removeSlide(int index)
{
    if (index < 0 || index >= count() || count() <= 1) {
        return;
    }
    auto command = std::make_unique<SlideRemove>(index, m_data.slides.at(index));
    command->text = QStringLiteral("Eliminar diapositiva");
    command->redo(m_data);
    if (m_current > index || m_current >= count()) {
        m_current = qMax(0, m_current - 1);
    }
    setSelection({});
    push(std::move(command));
    Q_EMIT currentChanged();
}

void Deck::moveSlide(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || from == to) {
        return;
    }
    auto command = std::make_unique<SlideMove>(from, to);
    command->text = QStringLiteral("Mover diapositiva");
    command->redo(m_data);
    if (m_current == from) {
        m_current = to;
    } else if (from < m_current && to >= m_current) {
        --m_current;
    } else if (from > m_current && to <= m_current) {
        ++m_current;
    }
    push(std::move(command));
    Q_EMIT currentChanged();
}

void Deck::setSlide(int index, const QString &key, const QVariant &value)
{
    if (index < 0 || index >= count()) {
        return;
    }
    const Slide before = m_data.slides.at(index);
    Slide after = before;
    QString text = QStringLiteral("Cambiar diapositiva");
    QString mergeKey;
    if (key == QLatin1String("layout")) {
        applyLayout(after, value.toString(), m_data.size());
        text = QStringLiteral("Cambiar diseño");
    } else if (key == QLatin1String("hidden")) {
        after.hidden = value.toBool();
        text = after.hidden ? QStringLiteral("Ocultar diapositiva") : QStringLiteral("Mostrar diapositiva");
    } else if (key == QLatin1String("notes")) {
        after.notes = value.toString();
        text = QStringLiteral("Escribir notas");
        mergeKey = QStringLiteral("notes|") + before.id;
    } else if (key == QLatin1String("transition")) {
        after.transition = value.toString();
        text = QStringLiteral("Cambiar transición");
    } else if (key == QLatin1String("background")) {
        after.background = value.toString();
        text = QStringLiteral("Cambiar fondo");
        mergeKey = QStringLiteral("background|") + before.id;
    }
    if (after == before) {
        return;
    }
    m_data.slides[index] = after;
    auto command = std::make_unique<SlideChange>(index, before, after);
    command->text = text;
    command->mergeKey = mergeKey;
    push(std::move(command));
    if (index == m_current) {
        pruneSelection();
    }
}

// --- Items -----------------------------------------------------------------------

QVariantList Deck::items(int index) const
{
    QVariantList out;
    if (index < 0 || index >= count()) {
        return out;
    }
    for (const Item &item : m_data.slides.at(index).items) {
        const QRectF bounds = item.bounds();
        out << QVariantMap{
            {QStringLiteral("id"), item.id}, {QStringLiteral("type"), item.type}, {QStringLiteral("shape"), item.shape},
            {QStringLiteral("x"), item.x}, {QStringLiteral("y"), item.y}, {QStringLiteral("w"), item.w}, {QStringLiteral("h"), item.h},
            {QStringLiteral("rotation"), item.rotation}, {QStringLiteral("role"), item.role}, {QStringLiteral("group"), item.group},
            {QStringLiteral("flipH"), item.flipH}, {QStringLiteral("flipV"), item.flipV}, {QStringLiteral("anim"), item.anim},
            {QStringLiteral("line"), item.type == QLatin1String("shape") && item.shape == QLatin1String("line")},
            {QStringLiteral("bx"), bounds.x()}, {QStringLiteral("by"), bounds.y()}, {QStringLiteral("bw"), bounds.width()}, {QStringLiteral("bh"), bounds.height()},
        };
    }
    return out;
}

QVariantMap Deck::item(const QString &id) const
{
    const Item *item = find(id);
    if (!item) {
        return {};
    }
    const Look look = m_data.look();
    QVariantMap map = item->toJson().toVariantMap();
    map.remove(QStringLiteral("text"));
    const bool line = item->type == QLatin1String("shape") && item->shape == QLatin1String("line");
    map.insert(QStringLiteral("line"), line);
    map.insert(QStringLiteral("texty"), item->type == QLatin1String("text") || (item->type == QLatin1String("shape") && !line));
    map.insert(QStringLiteral("plain"), item->plainText());
    // The file leaves out what is as it starts; the window wants every one.
    map.insert(QStringLiteral("x"), item->x);
    map.insert(QStringLiteral("y"), item->y);
    map.insert(QStringLiteral("w"), item->w);
    map.insert(QStringLiteral("h"), item->h);
    map.insert(QStringLiteral("rotation"), item->rotation);
    map.insert(QStringLiteral("opacity"), item->opacity);
    map.insert(QStringLiteral("strokeWidth"), item->strokeWidth);
    map.insert(QStringLiteral("shadow"), item->shadow);
    map.insert(QStringLiteral("valign"), item->valign);
    map.insert(QStringLiteral("anim"), item->anim);
    map.insert(QStringLiteral("arrow"), item->arrow);
    map.insert(QStringLiteral("flipH"), item->flipH);
    map.insert(QStringLiteral("flipV"), item->flipV);
    map.insert(QStringLiteral("fill"), item->fill);
    map.insert(QStringLiteral("stroke"), item->stroke);
    map.insert(QStringLiteral("role"), item->role);
    map.insert(QStringLiteral("group"), item->group);
    map.insert(QStringLiteral("shape"), item->shape);
    map.insert(QStringLiteral("rows"), item->rows);
    map.insert(QStringLiteral("cols"), item->cols);
    map.insert(QStringLiteral("cells"), item->cells);
    map.insert(QStringLiteral("hasImage"), !item->image.isEmpty());
    const QColor fill = look.color(item->fill);
    const QColor stroke = look.color(item->stroke);
    map.insert(QStringLiteral("fillColor"), fill.isValid() ? fill.name() : QString());
    map.insert(QStringLiteral("strokeColor"), stroke.isValid() ? stroke.name() : QString());
    // The format of the text, as its first run has it.
    const TextDefaults d = Model::textDefaults(*item, look);
    const Paragraph paragraph = item->text.isEmpty() ? Paragraph() : item->text.first();
    const Run run = paragraph.runs.isEmpty() ? Run() : paragraph.runs.first();
    const QColor ink = look.color(run.color);
    map.insert(QStringLiteral("font"), run.font.isEmpty() ? d.font : run.font);
    map.insert(QStringLiteral("size"), run.size > 0 ? run.size : d.size);
    map.insert(QStringLiteral("color"), (ink.isValid() ? ink : d.color).name());
    map.insert(QStringLiteral("bold"), run.bold < 0 ? d.bold : run.bold > 0);
    map.insert(QStringLiteral("italic"), run.italic);
    map.insert(QStringLiteral("underline"), run.underline);
    map.insert(QStringLiteral("strike"), run.strike);
    map.insert(QStringLiteral("align"), paragraph.align < 0 ? d.align : paragraph.align);
    map.insert(QStringLiteral("bullet"), paragraph.bullet);
    return map;
}

QString Deck::place(Item item, const QString &text)
{
    item.id = newId();
    const QString id = item.id;
    edit(text, [&](Slide &slide) {
        slide.items << item;
    });
    setSelection({id});
    return id;
}

QString Deck::addText()
{
    Item item;
    item.type = QStringLiteral("text");
    item.w = std::round(m_data.width * 0.4);
    item.h = 44;
    item.x = std::round((m_data.width - item.w) / 2);
    item.y = std::round((m_data.height - item.h) / 2);
    Run run;
    run.text = QStringLiteral("Texto");
    Paragraph paragraph;
    paragraph.runs << run;
    item.text << paragraph;
    return place(item, QStringLiteral("Añadir texto"));
}

QString Deck::addShape(const QString &shape)
{
    Item item;
    item.type = QStringLiteral("shape");
    item.shape = shape;
    item.valign = 1;
    if (shape == QLatin1String("line") || shape == QLatin1String("connector")) {
        item.shape = QStringLiteral("line");
        item.arrow = shape == QLatin1String("connector");
        item.w = 240;
        item.h = 0;
        item.stroke = QStringLiteral("@text");
        item.strokeWidth = 2;
    } else {
        item.w = shape == QLatin1String("arrow") || shape == QLatin1String("rect") || shape == QLatin1String("rounded") ? 220 : 150;
        item.h = shape == QLatin1String("arrow") ? 110 : 150;
        item.fill = QStringLiteral("@accent1");
    }
    item.x = std::round((m_data.width - item.w) / 2);
    item.y = std::round((m_data.height - item.h) / 2);
    return place(item, QStringLiteral("Añadir forma"));
}

QString Deck::addImage(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray bytes = file.readAll();
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    QString format = QString::fromLatin1(reader.format());
    QSize size = reader.size();
    if (!reader.canRead() || !size.isValid()) {
        return {};
    }
    if (reader.transformation() & QImageIOHandler::TransformationRotate90) {
        size.transpose();
    }
    // What a .pptx may hold as it is stays as it is; anything else becomes a PNG.
    static const QStringList kept = {QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("jpg"), QStringLiteral("gif")};
    if (!kept.contains(format)) {
        const QImage image = reader.read();
        QByteArray png;
        QBuffer out(&png);
        out.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&out, "PNG")) {
            return {};
        }
        bytes = png;
        format = QStringLiteral("png");
    }
    const QString key = m_data.addMedia(bytes, format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format);

    // A selected, empty picture placeholder takes the picture.
    if (m_selection.size() == 1) {
        const Item *target = find(m_selection.first());
        if (target && target->type == QLatin1String("image") && target->image.isEmpty()) {
            const QString id = target->id;
            edit(QStringLiteral("Añadir imagen"), [&](Slide &slide) {
                Item &item = slide.items[slide.indexOf(id)];
                // Fitted inside the placeholder, keeping its proportions.
                const QSizeF fitted = QSizeF(size).scaled(item.rect().size(), Qt::KeepAspectRatio);
                const QPointF center = item.center();
                item.setRect(QRectF(center.x() - fitted.width() / 2, center.y() - fitted.height() / 2, fitted.width(), fitted.height()));
                item.image = key;
            });
            return id;
        }
    }
    Item item;
    item.type = QStringLiteral("image");
    item.image = key;
    const QSizeF fitted = QSizeF(size).scaled(m_data.width * 0.6, m_data.height * 0.6, Qt::KeepAspectRatio);
    item.w = std::round(fitted.width());
    item.h = std::round(fitted.height());
    item.x = std::round((m_data.width - item.w) / 2);
    item.y = std::round((m_data.height - item.h) / 2);
    return place(item, QStringLiteral("Añadir imagen"));
}

QString Deck::addTable(int rows, int cols)
{
    Item item;
    item.type = QStringLiteral("table");
    item.rows = qBound(1, rows, 20);
    item.cols = qBound(1, cols, 12);
    for (int i = 0; i < item.rows * item.cols; ++i) {
        item.cells << QString();
    }
    item.w = std::round(m_data.width * 0.7);
    item.h = item.rows * 36;
    item.x = std::round((m_data.width - item.w) / 2);
    item.y = std::round((m_data.height - item.h) / 2);
    return place(item, QStringLiteral("Añadir tabla"));
}

void Deck::setProp(const QString &key, const QVariant &value)
{
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        return;
    }
    edit(QStringLiteral("Cambiar formato"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            if (!ids.contains(item.id)) {
                continue;
            }
            if (key == QLatin1String("x")) item.x = value.toDouble();
            else if (key == QLatin1String("y")) item.y = value.toDouble();
            else if (key == QLatin1String("w")) item.w = qMax(item.shape == QLatin1String("line") ? 0.0 : 4.0, value.toDouble());
            else if (key == QLatin1String("h")) item.h = qMax(item.shape == QLatin1String("line") ? 0.0 : 4.0, value.toDouble());
            else if (key == QLatin1String("rotation")) item.rotation = normalAngle(value.toDouble());
            else if (key == QLatin1String("fill")) item.fill = value.toString();
            else if (key == QLatin1String("stroke")) {
                item.stroke = value.toString();
                if (!item.stroke.isEmpty() && item.strokeWidth <= 0) item.strokeWidth = 2;
            }
            else if (key == QLatin1String("strokeWidth")) {
                item.strokeWidth = qBound(0.0, value.toDouble(), 60.0);
                if (item.strokeWidth > 0 && item.stroke.isEmpty()) item.stroke = QStringLiteral("@text");
            }
            else if (key == QLatin1String("shadow")) item.shadow = value.toBool();
            else if (key == QLatin1String("opacity")) item.opacity = qBound(0.0, value.toDouble(), 1.0);
            else if (key == QLatin1String("valign")) item.valign = qBound(0, value.toInt(), 2);
            else if (key == QLatin1String("anim")) item.anim = value.toString();
            else if (key == QLatin1String("arrow")) item.arrow = value.toBool();
            else if (key == QLatin1String("flipH")) item.flipH = value.toBool();
            else if (key == QLatin1String("flipV")) item.flipV = value.toBool();
            else if (key == QLatin1String("shape") && item.type == QLatin1String("shape") && item.shape != QLatin1String("line")
                     && value.toString() != QLatin1String("line")) item.shape = value.toString();
        }
    }, key);
}

void Deck::setFormat(const QString &key, const QVariant &value)
{
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        return;
    }
    const Look look = m_data.look();
    edit(QStringLiteral("Cambiar texto"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            const bool line = item.type == QLatin1String("shape") && item.shape == QLatin1String("line");
            if (!ids.contains(item.id) || line || item.type == QLatin1String("image") || item.type == QLatin1String("table")) {
                continue;
            }
            const TextDefaults d = Model::textDefaults(item, look);
            if (item.text.isEmpty()) {
                item.text << Paragraph();
            }
            for (Paragraph &paragraph : item.text) {
                if (paragraph.runs.isEmpty()) {
                    paragraph.runs << Run();
                }
                if (key == QLatin1String("align")) {
                    paragraph.align = value.toInt() == d.align ? -1 : qBound(0, value.toInt(), 3);
                } else if (key == QLatin1String("bullet")) {
                    paragraph.bullet = value.toBool();
                }
                for (Run &run : paragraph.runs) {
                    if (key == QLatin1String("bold")) run.bold = value.toBool() == d.bold ? -1 : (value.toBool() ? 1 : 0);
                    else if (key == QLatin1String("italic")) run.italic = value.toBool();
                    else if (key == QLatin1String("underline")) run.underline = value.toBool();
                    else if (key == QLatin1String("strike")) run.strike = value.toBool();
                    else if (key == QLatin1String("size")) {
                        const double size = qBound(4.0, value.toDouble(), 400.0);
                        run.size = qAbs(size - d.size) < 0.01 ? 0 : size;
                    }
                    else if (key == QLatin1String("color")) {
                        const QColor color = look.color(value.toString());
                        run.color = color.isValid() && color.rgb() == d.color.rgb() ? QString() : value.toString();
                    }
                    else if (key == QLatin1String("font")) run.font = value.toString() == d.font ? QString() : value.toString();
                }
            }
            item.text = RichText::tidied(item.text);
        }
    }, QStringLiteral("format-") + key);
}

QVariantMap Deck::textDefaults(const QString &id) const
{
    const Item *item = find(id);
    if (!item) {
        return {};
    }
    const TextDefaults d = Model::textDefaults(*item, m_data.look());
    return {
        {QStringLiteral("font"), d.font}, {QStringLiteral("size"), d.size}, {QStringLiteral("color"), d.color.name()},
        {QStringLiteral("align"), d.align}, {QStringLiteral("bold"), d.bold},
    };
}

QString Deck::html(const QString &id) const
{
    const Item *item = find(id);
    if (!item) {
        return {};
    }
    const Look look = m_data.look();
    QList<Paragraph> paragraphs = item->text;
    if (paragraphs.isEmpty()) {
        paragraphs << Paragraph();
    }
    return RichText::toHtml(paragraphs, Model::textDefaults(*item, look), look);
}

void Deck::setHtml(const QString &id, const QString &html)
{
    const Item *item = find(id);
    if (!item) {
        return;
    }
    const QList<Paragraph> paragraphs = RichText::fromHtml(html, Model::textDefaults(*item, m_data.look()));
    edit(QStringLiteral("Escribir"), [&](Slide &slide) {
        slide.items[slide.indexOf(id)].text = paragraphs;
    });
}

void Deck::setPlain(const QString &id, const QString &text)
{
    if (!find(id)) {
        return;
    }
    edit(QStringLiteral("Escribir"), [&](Slide &slide) {
        slide.items[slide.indexOf(id)].setPlainText(text);
    });
}

void Deck::setCell(const QString &id, int row, int col, const QString &text)
{
    const Item *item = find(id);
    if (!item || row < 0 || col < 0 || row >= item->rows || col >= item->cols) {
        return;
    }
    edit(QStringLiteral("Escribir en la tabla"), [&](Slide &slide) {
        Item &table = slide.items[slide.indexOf(id)];
        while (table.cells.size() < table.rows * table.cols) {
            table.cells << QString();
        }
        table.cells[row * table.cols + col] = text;
    });
}

void Deck::setTableSize(const QString &id, int rows, int cols)
{
    const Item *item = find(id);
    if (!item || item->type != QLatin1String("table")) {
        return;
    }
    rows = qBound(1, rows, 20);
    cols = qBound(1, cols, 12);
    edit(QStringLiteral("Cambiar tabla"), [&](Slide &slide) {
        Item &table = slide.items[slide.indexOf(id)];
        QStringList cells;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                cells << (row < table.rows && col < table.cols ? table.cell(row, col) : QString());
            }
        }
        // The rows keep their height: the table grows or shrinks by whole rows.
        table.h = table.h / table.rows * rows;
        table.rows = rows;
        table.cols = cols;
        table.cells = cells;
    });
}

// --- Selection -------------------------------------------------------------------

QStringList Deck::withGroups(const QStringList &ids) const
{
    const Slide *slide = now();
    if (!slide) {
        return {};
    }
    QStringList groups;
    for (const Item &item : slide->items) {
        if (ids.contains(item.id) && !item.group.isEmpty()) {
            groups << item.group;
        }
    }
    // In the order of the slide, bottom to top.
    QStringList out;
    for (const Item &item : slide->items) {
        if (ids.contains(item.id) || (!item.group.isEmpty() && groups.contains(item.group))) {
            out << item.id;
        }
    }
    return out;
}

void Deck::setSelection(const QStringList &ids)
{
    if (ids == m_selection) {
        return;
    }
    m_selection = ids;
    Q_EMIT selectionChanged();
}

void Deck::pruneSelection()
{
    setSelection(withGroups(m_selection));
}

QList<Item> Deck::selected() const
{
    QList<Item> out;
    if (const Slide *slide = now()) {
        for (const Item &item : slide->items) {
            if (m_selection.contains(item.id)) {
                out << item;
            }
        }
    }
    return out;
}

void Deck::select(const QString &id, bool extend)
{
    if (!find(id)) {
        if (!extend) {
            setSelection({});
        }
        return;
    }
    if (!extend) {
        setSelection(withGroups({id}));
        return;
    }
    const QStringList mates = withGroups({id});
    QStringList ids = m_selection;
    if (ids.contains(id)) {
        for (const QString &mate : mates) {
            ids.removeAll(mate);
        }
    } else {
        ids << mates;
    }
    setSelection(withGroups(ids));
}

void Deck::selectOnly(const QStringList &ids)
{
    setSelection(withGroups(ids));
}

void Deck::clearSelection()
{
    setSelection({});
}

void Deck::selectAll()
{
    QStringList ids;
    if (const Slide *slide = now()) {
        for (const Item &item : slide->items) {
            ids << item.id;
        }
    }
    setSelection(ids);
}

void Deck::selectRect(double x, double y, double w, double h)
{
    const QRectF rect = QRectF(x, y, w, h).normalized();
    QStringList ids;
    if (const Slide *slide = now()) {
        for (const Item &item : slide->items) {
            if (rect.intersects(item.bounds()) || rect.contains(item.center())) {
                ids << item.id;
            }
        }
    }
    setSelection(withGroups(ids));
}

QString Deck::hitTest(double x, double y, double slack) const
{
    const Slide *slide = now();
    if (!slide) {
        return {};
    }
    for (int i = int(slide->items.size()) - 1; i >= 0; --i) {
        if (slide->items.at(i).hit(QPointF(x, y), slack)) {
            return slide->items.at(i).id;
        }
    }
    return {};
}

QVariantMap Deck::selectionBounds() const
{
    const QRectF all = Model::bounds(selected());
    return {{QStringLiteral("x"), all.x()}, {QStringLiteral("y"), all.y()}, {QStringLiteral("w"), all.width()}, {QStringLiteral("h"), all.height()}};
}

// --- Dragging --------------------------------------------------------------------

void Deck::startDrag()
{
    if (m_dragging || !now()) {
        return;
    }
    m_dragStart = *now();
    m_dragging = true;
}

void Deck::moveBy(double dx, double dy, double snap)
{
    if (!m_dragging) {
        return;
    }
    QList<Item> moving;
    QList<QRectF> others;
    for (const Item &item : m_dragStart.items) {
        if (m_selection.contains(item.id)) {
            moving << item;
        } else {
            others << item.bounds();
        }
    }
    if (moving.isEmpty()) {
        return;
    }
    QVariantList guides;
    if (snap > 0) {
        const Snap pull = Model::snap(Model::bounds(moving).translated(dx, dy), others, m_data.size(), snap);
        dx += pull.dx;
        dy += pull.dy;
        for (const QLineF &line : pull.guides) {
            guides << QVariantMap{{QStringLiteral("x1"), line.x1()}, {QStringLiteral("y1"), line.y1()}, {QStringLiteral("x2"), line.x2()}, {QStringLiteral("y2"), line.y2()}};
        }
    }
    Slide *slide = now();
    for (const Item &start : moving) {
        Item &item = slide->items[slide->indexOf(start.id)];
        item.x = start.x + dx;
        item.y = start.y + dy;
    }
    if (guides != m_guides) {
        m_guides = guides;
        Q_EMIT guidesChanged();
    }
    bump();
}

void Deck::resizeBy(const QString &handle, double dx, double dy, bool keepAspect)
{
    if (!m_dragging) {
        return;
    }
    QList<Item> moving;
    for (const Item &item : m_dragStart.items) {
        if (m_selection.contains(item.id)) {
            moving << item;
        }
    }
    Slide *slide = now();
    if (moving.size() == 1) {
        const Item &start = moving.first();
        // A picture keeps its proportions unless told otherwise; the rest, the other way round.
        const bool keep = start.type == QLatin1String("image") && !start.image.isEmpty() ? !keepAspect : keepAspect;
        slide->items[slide->indexOf(start.id)].setRect(Model::resized(start.rect(), start.rotation, handle, QPointF(dx, dy), keep));
    } else if (moving.size() > 1) {
        // Several: the box around them is what is dragged, and each keeps its share of it.
        const QRectF from = Model::bounds(moving);
        const QRectF to = Model::resized(from, 0, handle, QPointF(dx, dy), keepAspect, 12);
        const double sx = from.width() > 0 ? to.width() / from.width() : 1;
        const double sy = from.height() > 0 ? to.height() / from.height() : 1;
        for (const Item &start : moving) {
            Item &item = slide->items[slide->indexOf(start.id)];
            const QPointF center(to.x() + (start.center().x() - from.x()) * sx, to.y() + (start.center().y() - from.y()) * sy);
            item.w = start.w * sx;
            item.h = start.h * sy;
            item.x = center.x() - item.w / 2;
            item.y = center.y() - item.h / 2;
        }
    }
    bump();
}

void Deck::moveEnd(int end, double dx, double dy)
{
    if (!m_dragging || m_selection.size() != 1) {
        return;
    }
    const int index = m_dragStart.indexOf(m_selection.first());
    if (index < 0) {
        return;
    }
    const Item &start = m_dragStart.items.at(index);
    QLineF line = start.line();
    if (end == 0) {
        line.setP1(line.p1() + QPointF(dx, dy));
    } else {
        line.setP2(line.p2() + QPointF(dx, dy));
    }
    Item &item = now()->items[index];
    item.rotation = 0;
    item.x = qMin(line.x1(), line.x2());
    item.y = qMin(line.y1(), line.y2());
    item.w = std::abs(line.dx());
    item.h = std::abs(line.dy());
    item.flipH = line.x2() < line.x1();
    item.flipV = line.y2() < line.y1();
    bump();
}

void Deck::rotateBy(double degrees)
{
    if (!m_dragging) {
        return;
    }
    QList<Item> moving;
    for (const Item &item : m_dragStart.items) {
        if (m_selection.contains(item.id)) {
            moving << item;
        }
    }
    if (moving.isEmpty()) {
        return;
    }
    const QPointF pivot = moving.size() == 1 ? moving.first().center() : Model::bounds(moving).center();
    QTransform turn;
    turn.translate(pivot.x(), pivot.y());
    turn.rotate(degrees);
    turn.translate(-pivot.x(), -pivot.y());
    Slide *slide = now();
    for (const Item &start : moving) {
        Item &item = slide->items[slide->indexOf(start.id)];
        const QPointF center = turn.map(start.center());
        item.x = center.x() - start.w / 2;
        item.y = center.y() - start.h / 2;
        item.rotation = normalAngle(start.rotation + degrees);
    }
    bump();
}

void Deck::finishDrag(const QString &text)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    if (!m_guides.isEmpty()) {
        m_guides.clear();
        Q_EMIT guidesChanged();
    }
    Slide *slide = now();
    if (!slide || *slide == m_dragStart) {
        return;
    }
    auto command = std::make_unique<SlideChange>(m_current, m_dragStart, *slide);
    command->text = text.isEmpty() ? QStringLiteral("Mover") : text;
    push(std::move(command));
}

// --- Arranging -------------------------------------------------------------------

void Deck::nudge(double dx, double dy)
{
    const QStringList ids = m_selection;
    edit(QStringLiteral("Mover"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            if (ids.contains(item.id)) {
                item.x += dx;
                item.y += dy;
            }
        }
    }, QStringLiteral("nudge"));
}

void Deck::align(const QString &mode)
{
    const QList<Item> items = selected();
    if (items.isEmpty()) {
        return;
    }
    // One thing (or one group) lines up with the slide; several, with each other.
    QStringList groups;
    bool single = true;
    for (const Item &item : items) {
        if (item.group.isEmpty() || item.group != items.first().group) {
            single = items.size() == 1;
        }
    }
    const QRectF frame = single ? QRectF(0, 0, m_data.width, m_data.height) : Model::bounds(items);
    const QRectF whole = Model::bounds(items);
    const QStringList ids = m_selection;
    edit(QStringLiteral("Alinear"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            if (!ids.contains(item.id)) {
                continue;
            }
            const QRectF box = single ? whole : item.bounds();
            if (mode == QLatin1String("left")) item.x += frame.left() - box.left();
            else if (mode == QLatin1String("center")) item.x += frame.center().x() - box.center().x();
            else if (mode == QLatin1String("right")) item.x += frame.right() - box.right();
            else if (mode == QLatin1String("top")) item.y += frame.top() - box.top();
            else if (mode == QLatin1String("middle")) item.y += frame.center().y() - box.center().y();
            else if (mode == QLatin1String("bottom")) item.y += frame.bottom() - box.bottom();
        }
    });
}

void Deck::distribute(const QString &axis)
{
    QList<Item> items = selected();
    if (items.size() < 3) {
        return;
    }
    const bool horizontal = axis != QLatin1String("v");
    std::sort(items.begin(), items.end(), [horizontal](const Item &a, const Item &b) {
        return horizontal ? a.bounds().center().x() < b.bounds().center().x() : a.bounds().center().y() < b.bounds().center().y();
    });
    // The first and the last stay; the gaps between the boxes become equal.
    double used = 0;
    for (const Item &item : items) {
        used += horizontal ? item.bounds().width() : item.bounds().height();
    }
    const QRectF all = Model::bounds(items);
    const double gap = ((horizontal ? all.width() : all.height()) - used) / (items.size() - 1);
    QHash<QString, double> shift;
    double at = horizontal ? all.left() : all.top();
    for (const Item &item : items) {
        const QRectF box = item.bounds();
        shift.insert(item.id, at - (horizontal ? box.left() : box.top()));
        at += (horizontal ? box.width() : box.height()) + gap;
    }
    edit(QStringLiteral("Distribuir"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            if (shift.contains(item.id)) {
                (horizontal ? item.x : item.y) += shift.value(item.id);
            }
        }
    });
}

void Deck::group()
{
    const QStringList ids = m_selection;
    if (ids.size() < 2) {
        return;
    }
    const QString name = newId();
    edit(QStringLiteral("Agrupar"), [&](Slide &slide) {
        // The group sits where its topmost member was, the members together.
        QList<Item> members;
        int top = -1;
        for (int i = int(slide.items.size()) - 1; i >= 0; --i) {
            if (ids.contains(slide.items.at(i).id)) {
                if (top < 0) {
                    top = i;
                }
                Item item = slide.items.takeAt(i);
                item.group = name;
                members.prepend(item);
            }
        }
        int at = top - int(members.size()) + 1;
        for (const Item &item : members) {
            slide.items.insert(at++, item);
        }
    });
}

void Deck::ungroup()
{
    const QStringList ids = m_selection;
    edit(QStringLiteral("Desagrupar"), [&](Slide &slide) {
        for (Item &item : slide.items) {
            if (ids.contains(item.id)) {
                item.group.clear();
            }
        }
    });
}

void Deck::order(const QString &mode)
{
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        return;
    }
    edit(QStringLiteral("Ordenar"), [&](Slide &slide) {
        QList<Item> picked;
        QList<Item> rest;
        int lowest = -1;
        int highest = -1;
        for (int i = 0; i < slide.items.size(); ++i) {
            if (ids.contains(slide.items.at(i).id)) {
                picked << slide.items.at(i);
                if (lowest < 0) {
                    lowest = i;
                }
                highest = i;
            } else {
                rest << slide.items.at(i);
            }
        }
        int at = 0;
        if (mode == QLatin1String("front")) {
            at = int(rest.size());
        } else if (mode == QLatin1String("back")) {
            at = 0;
        } else if (mode == QLatin1String("forward")) {
            // Past the first thing that was above the topmost of them.
            at = qMin(int(rest.size()), highest - int(picked.size()) + 2);
        } else {
            at = qMax(0, lowest - 1);
        }
        for (const Item &item : picked) {
            rest.insert(at++, item);
        }
        slide.items = rest;
    });
}

void Deck::copy()
{
    const QList<Item> items = selected();
    QJsonObject root{{QStringLiteral("wonderland-diapositivas"), 1}};
    QJsonObject media;
    QStringList words;
    const auto keep = [&](const Item &item) {
        if (!item.image.isEmpty() && m_data.media.contains(item.image)) {
            media.insert(item.image, QString::fromLatin1(m_data.media.value(item.image).toBase64()));
        }
    };
    if (!items.isEmpty()) {
        QJsonArray array;
        for (const Item &item : items) {
            array.append(item.toJson());
            keep(item);
            if (item.hasText()) {
                words << item.plainText();
            }
        }
        root.insert(QStringLiteral("items"), array);
        root.insert(QStringLiteral("from"), now()->id);
    } else if (const Slide *slide = now()) {
        // Nothing selected: the slide itself.
        root.insert(QStringLiteral("slide"), slide->toJson());
        for (const Item &item : slide->items) {
            keep(item);
            if (item.hasText()) {
                words << item.plainText();
            }
        }
    }
    root.insert(QStringLiteral("media"), media);
    auto *mime = new QMimeData;
    mime->setData(clipboardType, QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!words.isEmpty()) {
        mime->setText(words.join(QLatin1Char('\n')));
    }
    QGuiApplication::clipboard()->setMimeData(mime);
}

void Deck::cut()
{
    if (m_selection.isEmpty()) {
        return;
    }
    copy();
    remove();
    if (!m_undo.empty()) {
        m_undo.back()->text = QStringLiteral("Cortar");
        touchStack();
    }
}

void Deck::paste()
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !now()) {
        return;
    }
    if (mime->hasFormat(clipboardType)) {
        const QJsonObject root = QJsonDocument::fromJson(mime->data(clipboardType)).object();
        const QJsonObject media = root.value(QLatin1String("media")).toObject();
        for (auto it = media.constBegin(); it != media.constEnd(); ++it) {
            if (!m_data.media.contains(it.key())) {
                m_data.media.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
            }
        }
        if (root.contains(QLatin1String("slide"))) {
            Slide slide = Slide::fromJson(root.value(QLatin1String("slide")).toObject());
            slide.id = newId();
            for (Item &item : slide.items) {
                item.id = newId();
            }
            auto command = std::make_unique<SlideInsert>(m_current + 1, slide);
            command->text = QStringLiteral("Pegar diapositiva");
            command->redo(m_data);
            m_current = m_current + 1;
            setSelection({});
            push(std::move(command));
            Q_EMIT currentChanged();
            return;
        }
        QList<Item> items;
        QHash<QString, QString> groups;
        // Pasted where it was copied, it would hide what it came from.
        const double shift = root.value(QLatin1String("from")).toString() == now()->id ? 16 : 0;
        QStringList ids;
        for (const QJsonValue &value : root.value(QLatin1String("items")).toArray()) {
            Item item = Item::fromJson(value.toObject());
            item.id = newId();
            item.role.clear();
            item.x += shift;
            item.y += shift;
            if (!item.group.isEmpty()) {
                if (!groups.contains(item.group)) {
                    groups.insert(item.group, newId());
                }
                item.group = groups.value(item.group);
            }
            ids << item.id;
            items << item;
        }
        if (items.isEmpty()) {
            return;
        }
        edit(QStringLiteral("Pegar"), [&](Slide &slide) {
            slide.items << items;
        });
        setSelection(ids);
        return;
    }
    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        QByteArray png;
        QBuffer out(&png);
        out.open(QIODevice::WriteOnly);
        if (image.isNull() || !image.save(&out, "PNG")) {
            return;
        }
        Item item;
        item.type = QStringLiteral("image");
        item.image = m_data.addMedia(png, QStringLiteral("png"));
        const QSizeF fitted = QSizeF(image.size()).scaled(m_data.width * 0.6, m_data.height * 0.6, Qt::KeepAspectRatio);
        item.w = std::round(fitted.width());
        item.h = std::round(fitted.height());
        item.x = std::round((m_data.width - item.w) / 2);
        item.y = std::round((m_data.height - item.h) / 2);
        place(item, QStringLiteral("Pegar"));
        return;
    }
    if (mime->hasText() && !mime->text().trimmed().isEmpty()) {
        Item item;
        item.type = QStringLiteral("text");
        item.setPlainText(mime->text().trimmed());
        item.w = std::round(m_data.width * 0.6);
        item.h = qMin(m_data.height * 0.7, 36.0 * item.text.size() + 8);
        item.x = std::round((m_data.width - item.w) / 2);
        item.y = std::round((m_data.height - item.h) / 2);
        place(item, QStringLiteral("Pegar"));
    }
}

void Deck::duplicate()
{
    const QList<Item> items = selected();
    if (items.isEmpty()) {
        return;
    }
    QHash<QString, QString> groups;
    QList<Item> copies;
    QStringList ids;
    for (Item item : items) {
        item.id = newId();
        item.role.clear();
        item.x += 16;
        item.y += 16;
        if (!item.group.isEmpty()) {
            if (!groups.contains(item.group)) {
                groups.insert(item.group, newId());
            }
            item.group = groups.value(item.group);
        }
        ids << item.id;
        copies << item;
    }
    edit(QStringLiteral("Duplicar"), [&](Slide &slide) {
        slide.items << copies;
    });
    setSelection(ids);
}

void Deck::remove()
{
    const QStringList ids = m_selection;
    if (ids.isEmpty()) {
        return;
    }
    setSelection({});
    edit(QStringLiteral("Eliminar"), [&](Slide &slide) {
        slide.items.removeIf([&](const Item &item) {
            return ids.contains(item.id);
        });
    });
}

// --- The look --------------------------------------------------------------------

QVariantList Deck::themes() const
{
    QVariantList out;
    for (const Theme &theme : Model::themes()) {
        out << QVariantMap{
            {QStringLiteral("id"), theme.id}, {QStringLiteral("name"), theme.name},
            {QStringLiteral("bg1"), theme.bg1}, {QStringLiteral("bg2"), theme.bg2},
            {QStringLiteral("title"), theme.title}, {QStringLiteral("text"), theme.text},
            {QStringLiteral("accents"), theme.accents},
            {QStringLiteral("titleFont"), theme.titleFont}, {QStringLiteral("bodyFont"), theme.bodyFont},
        };
    }
    return out;
}

void Deck::setTheme(const QString &id)
{
    if (id == m_data.theme || Model::theme(id).id != id) {
        return;
    }
    const Presentation before = m_data;
    m_data.theme = id;
    // A theme brings its own background, fonts and inks: the master starts over, bar the footer.
    Master master;
    master.footer = before.master.footer;
    master.numbers = before.master.numbers;
    m_data.master = master;
    auto command = std::make_unique<DeckChange>(before, m_data, m_current);
    command->text = QStringLiteral("Cambiar tema");
    push(std::move(command));
}

QVariantMap Deck::master() const
{
    QVariantMap map = m_data.master.toJson().toVariantMap();
    const Look look = m_data.look();
    // What is in force, for the dialog to show beside what is set.
    map.insert(QStringLiteral("effectiveBackground"), look.bg1.name());
    map.insert(QStringLiteral("effectiveTitleColor"), look.title.name());
    map.insert(QStringLiteral("effectiveTextColor"), look.text.name());
    map.insert(QStringLiteral("effectiveTitleFont"), look.titleFont);
    map.insert(QStringLiteral("effectiveBodyFont"), look.bodyFont);
    return map;
}

void Deck::setMaster(const QVariantMap &master)
{
    const Master next = Master::fromJson(QJsonObject::fromVariantMap(master));
    if (next == m_data.master) {
        return;
    }
    const Presentation before = m_data;
    m_data.master = next;
    auto command = std::make_unique<DeckChange>(before, m_data, m_current);
    command->text = QStringLiteral("Cambiar patrón");
    push(std::move(command));
}

void Deck::setAspect(const QString &aspect)
{
    const double width = aspect == QLatin1String("4:3") ? 720 : 960;
    if (qFuzzyCompare(width, m_data.width)) {
        return;
    }
    const Presentation before = m_data;
    const double scale = width / m_data.width;
    for (Slide &slide : m_data.slides) {
        for (Item &item : slide.items) {
            if (!item.role.isEmpty() || item.type == QLatin1String("text") || item.type == QLatin1String("table")) {
                // What holds text is reflowed to the new width.
                item.x *= scale;
                item.w *= scale;
            } else {
                // Shapes and pictures keep their proportions and their place across the slide.
                const double center = item.center().x() * scale;
                if (item.w > width) {
                    const double shrink = width / item.w;
                    item.y += item.h * (1 - shrink) / 2;
                    item.w *= shrink;
                    item.h *= shrink;
                }
                item.x = center - item.w / 2;
            }
        }
    }
    m_data.width = width;
    m_data.height = 540;
    auto command = std::make_unique<DeckChange>(before, m_data, m_current);
    command->text = QStringLiteral("Cambiar tamaño de diapositiva");
    push(std::move(command));
}

QVariantList Deck::swatches() const
{
    const Look look = m_data.look();
    QVariantList out;
    const auto add = [&](const QString &token, const QColor &color, const QString &name) {
        out << QVariantMap{{QStringLiteral("token"), token}, {QStringLiteral("color"), color.name()}, {QStringLiteral("name"), name}};
    };
    for (int i = 0; i < look.accents.size(); ++i) {
        add(QStringLiteral("@accent%1").arg(i + 1), look.accents.at(i), QStringLiteral("Énfasis %1").arg(i + 1));
    }
    add(QStringLiteral("@title"), look.title, QStringLiteral("Títulos"));
    add(QStringLiteral("@text"), look.text, QStringLiteral("Texto"));
    add(QStringLiteral("@bg"), look.bg1, QStringLiteral("Fondo"));
    return out;
}

QStringList Deck::fonts() const
{
    QStringList out;
    for (const QString &family : QFontDatabase::families()) {
        // Scalable text faces only: no bitmap, symbol or private ones.
        if (!family.startsWith(QLatin1Char('.')) && QFontDatabase::isSmoothlyScalable(family) && !QFontDatabase::isPrivateFamily(family)) {
            out << family;
        }
    }
    return out;
}
