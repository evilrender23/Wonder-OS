#include "model.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QTransform>

#include <cmath>

namespace Model {

QString newId()
{
    return QString::number(QRandomGenerator::global()->generate64() & 0xffffffffffULL, 36);
}

// --- JSON ------------------------------------------------------------------------

bool Run::sameFormat(const Run &other) const
{
    return bold == other.bold && italic == other.italic && underline == other.underline && strike == other.strike
        && qFuzzyCompare(size + 1, other.size + 1) && color == other.color && font == other.font;
}

QJsonObject Run::toJson() const
{
    QJsonObject o{{QStringLiteral("text"), text}};
    if (bold >= 0) o.insert(QStringLiteral("bold"), bold);
    if (italic) o.insert(QStringLiteral("italic"), true);
    if (underline) o.insert(QStringLiteral("underline"), true);
    if (strike) o.insert(QStringLiteral("strike"), true);
    if (size > 0) o.insert(QStringLiteral("size"), size);
    if (!color.isEmpty()) o.insert(QStringLiteral("color"), color);
    if (!font.isEmpty()) o.insert(QStringLiteral("font"), font);
    return o;
}

Run Run::fromJson(const QJsonObject &o)
{
    Run run;
    run.text = o.value(QLatin1String("text")).toString();
    run.bold = o.value(QLatin1String("bold")).toInt(-1);
    run.italic = o.value(QLatin1String("italic")).toBool();
    run.underline = o.value(QLatin1String("underline")).toBool();
    run.strike = o.value(QLatin1String("strike")).toBool();
    run.size = o.value(QLatin1String("size")).toDouble();
    run.color = o.value(QLatin1String("color")).toString();
    run.font = o.value(QLatin1String("font")).toString();
    return run;
}

QString Paragraph::plain() const
{
    QString out;
    for (const Run &run : runs) {
        out += run.text;
    }
    return out;
}

QJsonObject Paragraph::toJson() const
{
    QJsonArray array;
    for (const Run &run : runs) {
        array.append(run.toJson());
    }
    QJsonObject o{{QStringLiteral("runs"), array}};
    if (align >= 0) o.insert(QStringLiteral("align"), align);
    if (bullet) o.insert(QStringLiteral("bullet"), true);
    if (level > 0) o.insert(QStringLiteral("level"), level);
    return o;
}

Paragraph Paragraph::fromJson(const QJsonObject &o)
{
    Paragraph paragraph;
    for (const QJsonValue &value : o.value(QLatin1String("runs")).toArray()) {
        paragraph.runs << Run::fromJson(value.toObject());
    }
    paragraph.align = o.value(QLatin1String("align")).toInt(-1);
    paragraph.bullet = o.value(QLatin1String("bullet")).toBool();
    paragraph.level = o.value(QLatin1String("level")).toInt();
    return paragraph;
}

QJsonObject Item::toJson() const
{
    QJsonObject o{
        {QStringLiteral("id"), id}, {QStringLiteral("type"), type},
        {QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("w"), w}, {QStringLiteral("h"), h},
        {QStringLiteral("rotation"), rotation}, {QStringLiteral("fill"), fill}, {QStringLiteral("stroke"), stroke},
        {QStringLiteral("strokeWidth"), strokeWidth}, {QStringLiteral("shadow"), shadow}, {QStringLiteral("opacity"), opacity},
        {QStringLiteral("valign"), valign}, {QStringLiteral("role"), role}, {QStringLiteral("group"), group},
        {QStringLiteral("anim"), anim}, {QStringLiteral("shape"), shape}, {QStringLiteral("flipH"), flipH},
        {QStringLiteral("flipV"), flipV}, {QStringLiteral("arrow"), arrow}, {QStringLiteral("image"), image},
        {QStringLiteral("rows"), rows}, {QStringLiteral("cols"), cols},
    };
    QJsonArray paragraphs;
    for (const Paragraph &paragraph : text) {
        paragraphs.append(paragraph.toJson());
    }
    o.insert(QStringLiteral("text"), paragraphs);
    o.insert(QStringLiteral("cells"), QJsonArray::fromStringList(cells));
    return o;
}

Item Item::fromJson(const QJsonObject &o)
{
    Item item;
    item.id = o.value(QLatin1String("id")).toString();
    item.type = o.value(QLatin1String("type")).toString();
    item.shape = o.value(QLatin1String("shape")).toString();
    item.x = o.value(QLatin1String("x")).toDouble();
    item.y = o.value(QLatin1String("y")).toDouble();
    item.w = o.value(QLatin1String("w")).toDouble();
    item.h = o.value(QLatin1String("h")).toDouble();
    item.rotation = o.value(QLatin1String("rotation")).toDouble();
    item.flipH = o.value(QLatin1String("flipH")).toBool();
    item.flipV = o.value(QLatin1String("flipV")).toBool();
    item.fill = o.value(QLatin1String("fill")).toString();
    item.stroke = o.value(QLatin1String("stroke")).toString();
    item.strokeWidth = o.value(QLatin1String("strokeWidth")).toDouble();
    item.shadow = o.value(QLatin1String("shadow")).toBool();
    item.opacity = o.value(QLatin1String("opacity")).toDouble(1);
    item.arrow = o.value(QLatin1String("arrow")).toBool();
    for (const QJsonValue &value : o.value(QLatin1String("text")).toArray()) {
        item.text << Paragraph::fromJson(value.toObject());
    }
    item.valign = o.value(QLatin1String("valign")).toInt();
    item.role = o.value(QLatin1String("role")).toString();
    item.image = o.value(QLatin1String("image")).toString();
    item.rows = o.value(QLatin1String("rows")).toInt();
    item.cols = o.value(QLatin1String("cols")).toInt();
    for (const QJsonValue &value : o.value(QLatin1String("cells")).toArray()) {
        item.cells << value.toString();
    }
    item.group = o.value(QLatin1String("group")).toString();
    item.anim = o.value(QLatin1String("anim")).toString();
    return item;
}

QJsonObject Slide::toJson() const
{
    QJsonArray array;
    for (const Item &item : items) {
        array.append(item.toJson());
    }
    return {
        {QStringLiteral("id"), id}, {QStringLiteral("layout"), layout}, {QStringLiteral("items"), array},
        {QStringLiteral("notes"), notes}, {QStringLiteral("hidden"), hidden},
        {QStringLiteral("background"), background}, {QStringLiteral("transition"), transition},
    };
}

Slide Slide::fromJson(const QJsonObject &o)
{
    Slide slide;
    slide.id = o.value(QLatin1String("id")).toString();
    slide.layout = o.value(QLatin1String("layout")).toString(QStringLiteral("blank"));
    for (const QJsonValue &value : o.value(QLatin1String("items")).toArray()) {
        slide.items << Item::fromJson(value.toObject());
    }
    slide.notes = o.value(QLatin1String("notes")).toString();
    slide.hidden = o.value(QLatin1String("hidden")).toBool();
    slide.background = o.value(QLatin1String("background")).toString();
    slide.transition = o.value(QLatin1String("transition")).toString();
    return slide;
}

QJsonObject Master::toJson() const
{
    return {
        {QStringLiteral("background"), background}, {QStringLiteral("titleFont"), titleFont},
        {QStringLiteral("bodyFont"), bodyFont}, {QStringLiteral("titleColor"), titleColor},
        {QStringLiteral("textColor"), textColor}, {QStringLiteral("footer"), footer},
        {QStringLiteral("numbers"), numbers},
    };
}

Master Master::fromJson(const QJsonObject &o)
{
    Master master;
    master.background = o.value(QLatin1String("background")).toString();
    master.titleFont = o.value(QLatin1String("titleFont")).toString();
    master.bodyFont = o.value(QLatin1String("bodyFont")).toString();
    master.titleColor = o.value(QLatin1String("titleColor")).toString();
    master.textColor = o.value(QLatin1String("textColor")).toString();
    master.footer = o.value(QLatin1String("footer")).toString();
    master.numbers = o.value(QLatin1String("numbers")).toBool();
    return master;
}

// --- Items -----------------------------------------------------------------------

void Item::setRect(const QRectF &r)
{
    x = r.x();
    y = r.y();
    w = r.width();
    h = r.height();
}

QRectF Item::bounds() const
{
    if (qFuzzyIsNull(rotation)) {
        return rect();
    }
    QTransform turn;
    turn.translate(center().x(), center().y());
    turn.rotate(rotation);
    turn.translate(-center().x(), -center().y());
    return turn.mapRect(rect());
}

QLineF Item::line() const
{
    const QPointF a(flipH ? x + w : x, flipV ? y + h : y);
    const QPointF b(flipH ? x : x + w, flipV ? y : y + h);
    return QLineF(a, b);
}

bool Item::hit(const QPointF &point, double slack) const
{
    QTransform back;
    back.translate(center().x(), center().y());
    back.rotate(-rotation);
    back.translate(-center().x(), -center().y());
    const QPointF local = back.map(point);
    if (type == QLatin1String("shape") && shape == QLatin1String("line")) {
        // The distance to the segment, not to the box around it.
        const QLineF l = line();
        const QPointF d = l.p2() - l.p1();
        const double length = d.x() * d.x() + d.y() * d.y();
        double t = length > 0 ? ((local.x() - l.p1().x()) * d.x() + (local.y() - l.p1().y()) * d.y()) / length : 0;
        t = qBound(0.0, t, 1.0);
        const QPointF nearest = l.p1() + t * d;
        return QLineF(nearest, local).length() <= qMax(slack, 0.0) + qMax(strokeWidth / 2, 5.0);
    }
    return rect().adjusted(-slack, -slack, slack, slack).contains(local);
}

bool Item::hasText() const
{
    for (const Paragraph &paragraph : text) {
        for (const Run &run : paragraph.runs) {
            if (!run.text.isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

QString Item::plainText() const
{
    QStringList lines;
    for (const Paragraph &paragraph : text) {
        lines << paragraph.plain();
    }
    return lines.join(QLatin1Char('\n'));
}

void Item::setPlainText(const QString &plain)
{
    // The format of what was there stays: of the paragraph and of its first run.
    Paragraph pattern = text.isEmpty() ? Paragraph() : text.first();
    Run run = pattern.runs.isEmpty() ? Run() : pattern.runs.first();
    text.clear();
    const QStringList lines = plain.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        Paragraph paragraph = pattern;
        run.text = line;
        paragraph.runs = {run};
        text << paragraph;
    }
}

int Slide::indexOf(const QString &itemId) const
{
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).id == itemId) {
            return i;
        }
    }
    return -1;
}

QStringList Slide::builds() const
{
    QStringList out;
    for (const Item &item : items) {
        if (!item.anim.isEmpty()) {
            out << item.id;
        }
    }
    return out;
}

QRectF bounds(const QList<Item> &items)
{
    QRectF all;
    for (const Item &item : items) {
        all = all.isNull() ? item.bounds() : all.united(item.bounds());
    }
    return all;
}

// --- Themes ----------------------------------------------------------------------

const QList<Theme> &themes()
{
    static const QList<Theme> all = {
        {QStringLiteral("blanco"), QStringLiteral("Blanco"), QStringLiteral("#ffffff"), QStringLiteral("#ffffff"),
         QStringLiteral("#1d1d1f"), QStringLiteral("#3a3a3c"),
         {QStringLiteral("#0a7aff"), QStringLiteral("#34c759"), QStringLiteral("#ff9500"), QStringLiteral("#ff3b30"), QStringLiteral("#af52de"), QStringLiteral("#5ac8fa")},
         QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans"), true},
        {QStringLiteral("negro"), QStringLiteral("Negro"), QStringLiteral("#0b0b0d"), QStringLiteral("#1c1c1e"),
         QStringLiteral("#ffffff"), QStringLiteral("#d1d1d6"),
         {QStringLiteral("#0a84ff"), QStringLiteral("#30d158"), QStringLiteral("#ff9f0a"), QStringLiteral("#ff453a"), QStringLiteral("#bf5af2"), QStringLiteral("#64d2ff")},
         QStringLiteral("Noto Sans"), QStringLiteral("Noto Sans"), true},
        {QStringLiteral("aurora"), QStringLiteral("Aurora"), QStringLiteral("#1b1f4b"), QStringLiteral("#5b2a86"),
         QStringLiteral("#ffffff"), QStringLiteral("#e4dcf5"),
         {QStringLiteral("#ff7eb6"), QStringLiteral("#7ee8fa"), QStringLiteral("#ffd166"), QStringLiteral("#9b8cff"), QStringLiteral("#5ef0b0"), QStringLiteral("#ff9f6e")},
         QStringLiteral("Fira Sans"), QStringLiteral("Fira Sans"), true},
        {QStringLiteral("papel"), QStringLiteral("Papel"), QStringLiteral("#f6f1e7"), QStringLiteral("#efe8da"),
         QStringLiteral("#2b2622"), QStringLiteral("#4a433c"),
         {QStringLiteral("#b5532a"), QStringLiteral("#3d6b5c"), QStringLiteral("#c9973a"), QStringLiteral("#7a4b6e"), QStringLiteral("#4d6a8f"), QStringLiteral("#8a8f3c")},
         QStringLiteral("Noto Serif"), QStringLiteral("Noto Sans"), false},
        {QStringLiteral("pizarra"), QStringLiteral("Pizarra"), QStringLiteral("#2f3e46"), QStringLiteral("#22303a"),
         QStringLiteral("#f4f1de"), QStringLiteral("#d6dde0"),
         {QStringLiteral("#f2cc8f"), QStringLiteral("#81b29a"), QStringLiteral("#e07a5f"), QStringLiteral("#a8c6df"), QStringLiteral("#d4a5c9"), QStringLiteral("#bfd96c")},
         QStringLiteral("Ubuntu"), QStringLiteral("Ubuntu"), true},
        {QStringLiteral("oceano"), QStringLiteral("Océano"), QStringLiteral("#eaf6fc"), QStringLiteral("#bfe1f2"),
         QStringLiteral("#0b3c5d"), QStringLiteral("#1f5673"),
         {QStringLiteral("#0e7ac4"), QStringLiteral("#17a398"), QStringLiteral("#f4a259"), QStringLiteral("#e4572e"), QStringLiteral("#5c6bc0"), QStringLiteral("#2e86ab")},
         QStringLiteral("Open Sans"), QStringLiteral("Open Sans"), true},
        {QStringLiteral("atardecer"), QStringLiteral("Atardecer"), QStringLiteral("#ff9a5a"), QStringLiteral("#e8476a"),
         QStringLiteral("#ffffff"), QStringLiteral("#fff1e6"),
         {QStringLiteral("#3d1c56"), QStringLiteral("#ffe29a"), QStringLiteral("#7b2d5b"), QStringLiteral("#2b2d6e"), QStringLiteral("#ffffff"), QStringLiteral("#ffb38a")},
         QStringLiteral("Fira Sans"), QStringLiteral("Fira Sans"), true},
        {QStringLiteral("editorial"), QStringLiteral("Editorial"), QStringLiteral("#ffffff"), QStringLiteral("#ffffff"),
         QStringLiteral("#111111"), QStringLiteral("#333333"),
         {QStringLiteral("#d0021b"), QStringLiteral("#111111"), QStringLiteral("#8e8e93"), QStringLiteral("#f5a623"), QStringLiteral("#4a90e2"), QStringLiteral("#417505")},
         QStringLiteral("Liberation Serif"), QStringLiteral("Liberation Sans"), true},
    };
    return all;
}

const Theme &theme(const QString &id)
{
    for (const Theme &theme : themes()) {
        if (theme.id == id) {
            return theme;
        }
    }
    return themes().first();
}

QColor Look::color(const QString &token) const
{
    if (token.isEmpty()) {
        return QColor();
    }
    if (token.startsWith(QLatin1Char('@'))) {
        if (token == QLatin1String("@text")) return text;
        if (token == QLatin1String("@title")) return title;
        if (token == QLatin1String("@bg")) return bg1;
        if (token.startsWith(QLatin1String("@accent"))) {
            const int n = token.mid(7).toInt();
            if (n >= 1 && n <= accents.size()) {
                return accents.at(n - 1);
            }
        }
        return QColor();
    }
    return QColor::fromString(token);
}

Look Presentation::look(const QString &themeOverride) const
{
    const Theme &t = Model::theme(themeOverride.isEmpty() ? theme : themeOverride);
    Look look;
    look.bg1 = QColor::fromString(t.bg1);
    look.bg2 = QColor::fromString(t.bg2);
    look.title = QColor::fromString(t.title);
    look.text = QColor::fromString(t.text);
    for (const QString &accent : t.accents) {
        look.accents << QColor::fromString(accent);
    }
    look.titleFont = t.titleFont;
    look.bodyFont = t.bodyFont;
    look.titleBold = t.titleBold;
    if (themeOverride.isEmpty()) {
        // The master's changes belong to the deck's own theme.
        const auto set = [](QColor &into, const QString &value) {
            const QColor color = QColor::fromString(value);
            if (!value.isEmpty() && color.isValid()) {
                into = color;
            }
        };
        if (!master.background.isEmpty() && QColor::fromString(master.background).isValid()) {
            look.bg1 = look.bg2 = QColor::fromString(master.background);
        }
        set(look.title, master.titleColor);
        set(look.text, master.textColor);
        if (!master.titleFont.isEmpty()) look.titleFont = master.titleFont;
        if (!master.bodyFont.isEmpty()) look.bodyFont = master.bodyFont;
    }
    return look;
}

QString Presentation::addMedia(const QByteArray &bytes, const QString &suffix)
{
    const QString key = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex().left(16))
        + QLatin1Char('.') + (suffix.isEmpty() ? QStringLiteral("png") : suffix.toLower());
    media.insert(key, bytes);
    return key;
}

// --- Layouts ---------------------------------------------------------------------

QStringList layouts()
{
    return {QStringLiteral("title"), QStringLiteral("content"), QStringLiteral("columns"),
            QStringLiteral("titleOnly"), QStringLiteral("blank"), QStringLiteral("picture")};
}

QString layoutName(const QString &layout)
{
    if (layout == QLatin1String("title")) return QStringLiteral("Título");
    if (layout == QLatin1String("content")) return QStringLiteral("Título y contenido");
    if (layout == QLatin1String("columns")) return QStringLiteral("Dos columnas");
    if (layout == QLatin1String("titleOnly")) return QStringLiteral("Solo título");
    if (layout == QLatin1String("picture")) return QStringLiteral("Imagen con pie");
    return QStringLiteral("En blanco");
}

static Item placeholder(const QString &role, const QSizeF &size, double x, double y, double w, double h)
{
    Item item;
    item.id = newId();
    item.type = role == QLatin1String("picture") ? QStringLiteral("image") : QStringLiteral("text");
    item.role = role;
    item.x = std::round(size.width() * x);
    item.y = std::round(size.height() * y);
    item.w = std::round(size.width() * w);
    item.h = std::round(size.height() * h);
    if (role == QLatin1String("ctitle")) {
        item.valign = 2;
    } else if (role == QLatin1String("title") || role == QLatin1String("caption")) {
        item.valign = 1;
    }
    if (item.type == QLatin1String("text")) {
        Paragraph paragraph;
        paragraph.bullet = role == QLatin1String("body");
        item.text << paragraph;
    }
    return item;
}

QList<Item> layoutItems(const QString &layout, const QSizeF &size)
{
    if (layout == QLatin1String("title")) {
        return {placeholder(QStringLiteral("ctitle"), size, 0.1, 0.24, 0.8, 0.28),
                placeholder(QStringLiteral("subtitle"), size, 0.1, 0.55, 0.8, 0.14)};
    }
    if (layout == QLatin1String("content")) {
        return {placeholder(QStringLiteral("title"), size, 0.06, 0.07, 0.88, 0.17),
                placeholder(QStringLiteral("body"), size, 0.06, 0.27, 0.88, 0.63)};
    }
    if (layout == QLatin1String("columns")) {
        return {placeholder(QStringLiteral("title"), size, 0.06, 0.07, 0.88, 0.17),
                placeholder(QStringLiteral("body"), size, 0.06, 0.27, 0.425, 0.63),
                placeholder(QStringLiteral("body"), size, 0.515, 0.27, 0.425, 0.63)};
    }
    if (layout == QLatin1String("titleOnly")) {
        return {placeholder(QStringLiteral("title"), size, 0.06, 0.07, 0.88, 0.17)};
    }
    if (layout == QLatin1String("picture")) {
        return {placeholder(QStringLiteral("picture"), size, 0.1, 0.08, 0.8, 0.66),
                placeholder(QStringLiteral("caption"), size, 0.1, 0.77, 0.8, 0.14)};
    }
    return {};
}

static int family(const QString &role)
{
    if (role == QLatin1String("title") || role == QLatin1String("ctitle")) return 1;
    if (role == QLatin1String("subtitle") || role == QLatin1String("body") || role == QLatin1String("caption")) return 2;
    if (role == QLatin1String("picture")) return 3;
    return 0;
}

void applyLayout(Slide &slide, const QString &layout, const QSizeF &size)
{
    const QList<Item> wanted = layoutItems(layout, size);
    QList<int> spare; // Placeholders of the slide not yet given a place.
    for (int i = 0; i < slide.items.size(); ++i) {
        if (family(slide.items.at(i).role) > 0) {
            spare << i;
        }
    }
    QList<Item> added;
    for (const Item &place : wanted) {
        int found = -1;
        for (int n = 0; n < spare.size(); ++n) {
            if (family(slide.items.at(spare.at(n)).role) == family(place.role)) {
                found = n;
                break;
            }
        }
        if (found < 0) {
            added << place;
            continue;
        }
        Item &item = slide.items[spare.takeAt(found)];
        const bool wasBody = item.role == QLatin1String("body");
        item.setRect(place.rect());
        item.rotation = 0;
        item.role = place.role;
        item.valign = place.valign;
        if (!item.hasText() && item.type == QLatin1String("text")) {
            item.text = place.text;
        } else if (wasBody != (place.role == QLatin1String("body"))) {
            for (Paragraph &paragraph : item.text) {
                paragraph.bullet = place.role == QLatin1String("body");
            }
        }
    }
    // What the new layout has no place for goes, unless it holds something.
    std::sort(spare.begin(), spare.end(), std::greater<int>());
    for (int index : spare) {
        const Item &item = slide.items.at(index);
        if (!item.hasText() && item.image.isEmpty()) {
            slide.items.removeAt(index);
        }
    }
    // Placeholders sit under what the user added.
    int at = 0;
    while (at < slide.items.size() && family(slide.items.at(at).role) > 0) {
        ++at;
    }
    for (const Item &item : added) {
        slide.items.insert(at++, item);
    }
    slide.layout = layout;
}

Slide newSlide(const QString &layout, const QSizeF &size)
{
    Slide slide;
    slide.id = newId();
    slide.layout = layouts().contains(layout) ? layout : QStringLiteral("blank");
    slide.items = layoutItems(slide.layout, size);
    return slide;
}

TextDefaults textDefaults(const Item &item, const Look &look)
{
    TextDefaults d;
    d.font = look.bodyFont;
    d.color = look.text;
    if (item.role == QLatin1String("ctitle")) {
        d = {look.titleFont, 54, look.title, 1, look.titleBold};
    } else if (item.role == QLatin1String("title")) {
        d = {look.titleFont, 38, look.title, 0, look.titleBold};
    } else if (item.role == QLatin1String("subtitle")) {
        d.size = 24;
        d.align = 1;
    } else if (item.role == QLatin1String("body")) {
        d.size = 24;
        d.spacing = 8;
    } else if (item.role == QLatin1String("caption")) {
        d.size = 18;
        d.align = 1;
    } else if (item.type == QLatin1String("shape")) {
        d.size = 18;
        d.align = 1;
        // Ink that reads on the fill.
        const QColor fill = look.color(item.fill);
        if (fill.isValid() && fill.alpha() > 0) {
            const double luma = 0.299 * fill.redF() + 0.587 * fill.greenF() + 0.114 * fill.blueF();
            d.color = luma < 0.6 ? QColor(Qt::white) : QColor(0x1d, 0x1d, 0x1f);
        }
    } else if (item.type == QLatin1String("table")) {
        d.size = 16;
    }
    return d;
}

QString prompt(const Item &item)
{
    if (item.role == QLatin1String("ctitle") || item.role == QLatin1String("title")) return QStringLiteral("Título");
    if (item.role == QLatin1String("subtitle")) return QStringLiteral("Subtítulo");
    if (item.role == QLatin1String("body")) return QStringLiteral("Texto");
    if (item.role == QLatin1String("caption")) return QStringLiteral("Pie de imagen");
    if (item.role == QLatin1String("picture")) return QStringLiteral("Imagen");
    return QString();
}

// --- Geometry --------------------------------------------------------------------

Snap snap(const QRectF &moving, const QList<QRectF> &others, const QSizeF &slide, double threshold)
{
    Snap result;
    QList<double> xs = {0, slide.width() / 2, slide.width()};
    QList<double> ys = {0, slide.height() / 2, slide.height()};
    for (const QRectF &other : others) {
        xs << other.left() << other.center().x() << other.right();
        ys << other.top() << other.center().y() << other.bottom();
    }
    const auto nearest = [threshold](const QList<double> &mine, const QList<double> &theirs, double &shift, double &at) {
        double best = threshold;
        bool found = false;
        for (double m : mine) {
            for (double t : theirs) {
                if (std::abs(t - m) <= best) {
                    // An exact tie keeps the first: the slide's own lines come first.
                    if (std::abs(t - m) < best || !found) {
                        best = std::abs(t - m);
                        shift = t - m;
                        at = t;
                        found = true;
                    }
                }
            }
        }
        return found;
    };
    double at = 0;
    if (nearest({moving.left(), moving.center().x(), moving.right()}, xs, result.dx, at)) {
        result.guides << QLineF(at, 0, at, slide.height());
    }
    if (nearest({moving.top(), moving.center().y(), moving.bottom()}, ys, result.dy, at)) {
        result.guides << QLineF(0, at, slide.width(), at);
    }
    return result;
}

QRectF resized(const QRectF &rect, double rotation, const QString &handle, const QPointF &delta, bool keepAspect, double minimum)
{
    QTransform turn;
    turn.rotate(rotation);
    const QPointF local = turn.inverted().map(delta);
    double left = -rect.width() / 2;
    double right = rect.width() / 2;
    double top = -rect.height() / 2;
    double bottom = rect.height() / 2;
    const bool west = handle.contains(QLatin1Char('w'));
    const bool east = handle.contains(QLatin1Char('e'));
    const bool north = handle.contains(QLatin1Char('n'));
    const bool south = handle.contains(QLatin1Char('s'));
    if (west) left = qMin(left + local.x(), right - minimum);
    if (east) right = qMax(right + local.x(), left + minimum);
    if (north) top = qMin(top + local.y(), bottom - minimum);
    if (south) bottom = qMax(bottom + local.y(), top + minimum);
    if (keepAspect && (west || east) && (north || south) && rect.width() > 0 && rect.height() > 0) {
        // The side that grew the most decides; the other follows.
        const double scale = qMax((right - left) / rect.width(), (bottom - top) / rect.height());
        const double width = rect.width() * scale;
        const double height = rect.height() * scale;
        if (west) left = right - width; else right = left + width;
        if (north) top = bottom - height; else bottom = top + height;
    }
    // The box grew off-centre in its own frame: move the centre to match, as seen on the slide.
    const QPointF shift = turn.map(QPointF((left + right) / 2, (top + bottom) / 2));
    const QPointF center = rect.center() + shift;
    const double width = right - left;
    const double height = bottom - top;
    return QRectF(center.x() - width / 2, center.y() - height / 2, width, height);
}

}
