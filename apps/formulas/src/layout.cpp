#include "layout.h"

#include "mathfont.h"

#include <QHash>
#include <QPainter>
#include <climits>
#include <QtMath>

namespace {

using F = MathFont;

enum { Display = 0, TextStyle = 1, ScriptStyle = 2, ScriptScript = 3 };

struct Ctx {
    const MathFont *font = nullptr;
    qreal base = 32;
    int style = Display;
    bool cramped = false;
    QString variant;
    QColor color;
    /// The height `\middle` delimiters grow to; 0 on the measuring pass.
    qreal middle = 0;

    qreal em() const
    {
        return style == ScriptStyle ? base * font->constant(F::ScriptPercentScaleDown)
            : style == ScriptScript ? base * font->constant(F::ScriptScriptPercentScaleDown) : base;
    }
    qreal k(F::Constant which) const { return font->constant(which) * em(); }
    Ctx with(int newStyle, bool cramp) const
    {
        Ctx c = *this;
        c.style = newStyle;
        c.cramped = cramp;
        c.middle = 0;
        return c;
    }
    Ctx sup() const { return with(style <= TextStyle ? ScriptStyle : ScriptScript, cramped); }
    Ctx sub() const { return with(style <= TextStyle ? ScriptStyle : ScriptScript, true); }
    Ctx numerator() const { return with(qMin(style + 1, int(ScriptScript)), cramped); }
    Ctx denominator() const { return with(qMin(style + 1, int(ScriptScript)), true); }
    Ctx cramp() const { return with(style, true); }
};

struct Piece {
    Box box;
    Atom atom = Atom::Ord;
    /// Glue: it takes no part in the spacing between atoms.
    bool space = false;
};

/// The character of a math alphabet, or the same one when the alphabet has none for it.
char32_t styled(char32_t c, const QString &variant, bool upright)
{
    const bool upper = c >= 'A' && c <= 'Z';
    const bool lower = c >= 'a' && c <= 'z';
    const bool digit = c >= '0' && c <= '9';
    const bool greekUpper = c >= 0x391 && c <= 0x3A9;
    const bool greekLower = c >= 0x3B1 && c <= 0x3C9;
    const auto greekVariant = [c](char32_t base) -> char32_t {
        switch (c) {
        case 0x2202: return base + 25;
        case 0x3F5: return base + 26;
        case 0x3D1: return base + 27;
        case 0x3F0: return base + 28;
        case 0x3D5: return base + 29;
        case 0x3F1: return base + 30;
        case 0x3D6: return base + 31;
        default: return 0;
        }
    };
    if (variant == QLatin1String("rm") || (upright && variant.isEmpty())) {
        return c;
    }
    if (variant.isEmpty() || variant == QLatin1String("it")) {
        if (c == 'h') return 0x210E;
        if (upper) return 0x1D434 + (c - 'A');
        if (lower) return 0x1D44E + (c - 'a');
        if (greekLower) return 0x1D6FC + (c - 0x3B1);
        if (greekUpper && !variant.isEmpty()) return 0x1D6E2 + (c - 0x391);
        if (c != 0x2202) {
            if (const char32_t v = greekVariant(0x1D6FC)) return v;
        }
        return c;
    }
    if (variant == QLatin1String("bf")) {
        if (upper) return 0x1D400 + (c - 'A');
        if (lower) return 0x1D41A + (c - 'a');
        if (digit) return 0x1D7CE + (c - '0');
        if (greekUpper) return 0x1D6A8 + (c - 0x391);
        if (greekLower) return 0x1D6C2 + (c - 0x3B1);
        return c;
    }
    if (variant == QLatin1String("bs")) {
        if (upper) return 0x1D468 + (c - 'A');
        if (lower) return 0x1D482 + (c - 'a');
        if (digit) return 0x1D7CE + (c - '0');
        if (greekUpper) return 0x1D71C + (c - 0x391);
        if (greekLower) return 0x1D736 + (c - 0x3B1);
        return c;
    }
    if (variant == QLatin1String("bb")) {
        switch (c) {
        case 'C': return 0x2102;
        case 'H': return 0x210D;
        case 'N': return 0x2115;
        case 'P': return 0x2119;
        case 'Q': return 0x211A;
        case 'R': return 0x211D;
        case 'Z': return 0x2124;
        default: break;
        }
        if (upper) return 0x1D538 + (c - 'A');
        if (lower) return 0x1D552 + (c - 'a');
        if (digit) return 0x1D7D8 + (c - '0');
        return c;
    }
    if (variant == QLatin1String("cal")) {
        switch (c) {
        case 'B': return 0x212C;
        case 'E': return 0x2130;
        case 'F': return 0x2131;
        case 'H': return 0x210B;
        case 'I': return 0x2110;
        case 'L': return 0x2112;
        case 'M': return 0x2133;
        case 'R': return 0x211B;
        case 'e': return 0x212F;
        case 'g': return 0x210A;
        case 'o': return 0x2134;
        default: break;
        }
        if (upper) return 0x1D49C + (c - 'A');
        if (lower) return 0x1D4B6 + (c - 'a');
        return c;
    }
    if (variant == QLatin1String("frak")) {
        switch (c) {
        case 'C': return 0x212D;
        case 'H': return 0x210C;
        case 'I': return 0x2111;
        case 'R': return 0x211C;
        case 'Z': return 0x2128;
        default: break;
        }
        if (upper) return 0x1D504 + (c - 'A');
        if (lower) return 0x1D51E + (c - 'a');
        return c;
    }
    if (variant == QLatin1String("sf")) {
        if (upper) return 0x1D5A0 + (c - 'A');
        if (lower) return 0x1D5BA + (c - 'a');
        if (digit) return 0x1D7E2 + (c - '0');
        return c;
    }
    if (variant == QLatin1String("tt")) {
        if (upper) return 0x1D670 + (c - 'A');
        if (lower) return 0x1D68A + (c - 'a');
        if (digit) return 0x1D7F6 + (c - '0');
        return c;
    }
    return c;
}

class Engine
{
public:
    Engine(const LayoutOptions &options)
    {
        root.font = MathFont::get(options.family);
        root.base = options.size;
        root.style = options.display ? Display : TextStyle;
    }

    Ctx root;

    Box run(const NodePtr &node)
    {
        if (!node) {
            return Box();
        }
        return row(node->kids, root);
    }

private:
    // ---- Leaves ----------------------------------------------------------

    static Item item(const QPainterPath &path, const Ctx &c, const NodePtr &node)
    {
        Item it;
        it.path = path;
        it.color = c.color;
        if (node) {
            it.start = node->start;
            it.end = node->end;
        }
        return it;
    }

    /// A glyph of `font` at the size of the context, on the baseline.
    Box glyphBox(const MathFont *font, quint32 glyph, const Ctx &c, const NodePtr &node, qreal slant = 0) const
    {
        const qreal s = c.em();
        Box box;
        QTransform t = QTransform::fromScale(s, s);
        if (slant != 0) {
            t = QTransform(1, 0, -slant, 1, 0, 0) * t;
        }
        const QPainterPath path = t.map(font->path(glyph));
        const QRectF r = path.boundingRect();
        box.width = font->advance(glyph) * s;
        box.height = qMax(0.0, -r.top());
        box.depth = qMax(0.0, r.bottom());
        box.italic = font->italic(glyph) * s;
        const qreal accent = font->topAccent(glyph);
        box.accent = accent < 0 ? -1 : accent * s;
        box.single = true;
        if (!path.isEmpty()) {
            box.items.append(item(path, c, node));
        }
        return box;
    }

    /// One character, in the alphabet of the context.
    Box charBox(char32_t code, const Ctx &c, const NodePtr &node, bool upright) const
    {
        const char32_t mapped = styled(code, c.variant, upright);
        if (quint32 g = c.font->glyph(mapped)) {
            // In a script, the drawing the font made for scripts: sturdier, and a prime of the right size.
            return glyphBox(c.font, c.font->scriptForm(g, c.style == ScriptStyle ? 1 : c.style == ScriptScript ? 2 : 0), c, node);
        }
        // The font has no such alphabet: the plain letter, slanted when it should be italic.
        const bool italic = mapped != code && (c.variant.isEmpty() || c.variant == QLatin1String("it") || c.variant == QLatin1String("bs"));
        for (const MathFont *font : {c.font, c.font->fallback()}) {
            if (quint32 g = font->glyph(mapped)) {
                return glyphBox(font, g, c, node);
            }
            if (quint32 g = font->glyph(code)) {
                return glyphBox(font, g, c, node, italic ? 0.2 : 0);
            }
        }
        Box box;
        box.width = 0.5 * c.em();
        return box;
    }

    Box textBox(const QString &text, const Ctx &c, const NodePtr &node, bool upright) const
    {
        Box out;
        const QList<uint> codes = text.toUcs4();
        qreal lastX = 0;
        for (int i = 0; i < codes.size(); ++i) {
            const char32_t code = codes.at(i);
            if (code == ' ' || code == 0xA0) {
                out.width += 0.28 * c.em();
                continue;
            }
            if (code == 0x338 && !out.isEmpty()) {
                // A negating slash over the glyph before it.
                const Box slash = charBox('/', c, node, true);
                out.add(slash, lastX + (out.width - lastX - slash.width) / 2, 0);
                out.height = qMax(out.height, slash.height);
                out.depth = qMax(out.depth, slash.depth);
                continue;
            }
            Box g = charBox(code, c, node, upright);
            lastX = out.width;
            out.add(g, out.width, 0);
            out.height = qMax(out.height, g.height);
            out.depth = qMax(out.depth, g.depth);
            out.width += g.width;
            out.italic = g.italic;
            out.accent = codes.size() == 1 ? g.accent : -1;
        }
        out.single = codes.size() == 1;
        return out;
    }

    static Box ruleBox(qreal x, qreal y, qreal w, qreal h, const Ctx &c, const NodePtr &node)
    {
        Box box;
        QPainterPath path;
        path.addRect(QRectF(x, y, w, h));
        box.items.append(item(path, c, node));
        return box;
    }

    static void centreOnAxis(Box &box, const Ctx &c)
    {
        const qreal axis = c.font->axis() * c.em();
        const qreal dy = (box.height - box.depth) / 2 - axis;
        box.shift(0, dy);
        box.height -= dy;
        box.depth += dy;
    }

    // ---- Stretchy glyphs -------------------------------------------------

    /// The parts of an assembly, repeated and overlapped to reach `target` (in em).
    struct Placed {
        quint32 glyph;
        qreal at;
    };
    QList<Placed> assemble(const QList<F::Part> &parts, qreal target, const MathFont *font, qreal *length) const
    {
        const qreal minOverlap = font->minConnectorOverlap();
        QList<F::Part> list;
        for (int repeats = 0; repeats < 200; ++repeats) {
            list.clear();
            for (const F::Part &part : parts) {
                for (int i = 0; i < (part.extender ? repeats : 1); ++i) {
                    list.append(part);
                }
            }
            qreal sum = 0;
            for (const F::Part &part : list) {
                sum += part.advance;
            }
            if (sum - minOverlap * qMax(0, list.size() - 1) >= target) {
                break;
            }
        }
        qreal sum = 0;
        qreal maxOverlap = 1e9;
        for (int i = 0; i < list.size(); ++i) {
            sum += list.at(i).advance;
            if (i > 0) {
                maxOverlap = qMin(maxOverlap, qMin(list.at(i - 1).endConnector, list.at(i).startConnector));
            }
        }
        qreal overlap = list.size() > 1 ? (sum - target) / (list.size() - 1) : 0;
        overlap = qBound(qMin(minOverlap, maxOverlap), overlap, qMax(minOverlap, maxOverlap));
        QList<Placed> out;
        qreal at = 0;
        for (int i = 0; i < list.size(); ++i) {
            out.append({list.at(i).glyph, at});
            at += list.at(i).advance - (i + 1 < list.size() ? overlap : 0);
        }
        *length = at;
        return out;
    }

    /// A delimiter (or radical) at least `target` pixels tall, centred on the axis.
    Box tall(char32_t code, qreal target, const Ctx &c, const NodePtr &node, bool centre = true) const
    {
        const qreal s = c.em();
        const MathFont *font = c.font;
        quint32 g = font->glyph(code);
        if (!g) {
            font = c.font->fallback();
            g = font->glyph(code);
        }
        if (!g) {
            Box none;
            none.width = 0.3 * s;
            return none;
        }
        Box box;
        const QRectF ink = font->bounds(g);
        if (ink.height() * s >= target) {
            box = glyphBox(font, g, c, node);
        } else {
            quint32 chosen = 0;
            const QList<F::Variant> variants = font->variants(g, true);
            for (const F::Variant &v : variants) {
                if (font->bounds(v.glyph).height() * s >= target) {
                    chosen = v.glyph;
                    break;
                }
            }
            const QList<F::Part> parts = font->assembly(g, true);
            if (chosen) {
                box = glyphBox(font, chosen, c, node);
            } else if (!parts.isEmpty()) {
                qreal length = 0;
                const QList<Placed> placed = assemble(parts, target / s, font, &length);
                // Bottom to top: each part sits with its ink on what is below it.
                qreal width = 0;
                QPainterPath whole;
                for (const Placed &part : placed) {
                    const QRectF r = font->bounds(part.glyph);
                    QPainterPath path = font->path(part.glyph);
                    path.translate(0, -part.at - r.bottom());
                    // One outline, so that no seam shows where the parts meet.
                    whole = whole.isEmpty() ? path : whole.united(path);
                    width = qMax(width, font->advance(part.glyph) * s);
                }
                whole.setFillRule(Qt::WindingFill);
                box.items.append(item(QTransform::fromScale(s, s).map(whole), c, node));
                box.width = width;
                box.height = length * s;
                box.depth = 0;
            } else if (!variants.isEmpty()) {
                box = glyphBox(font, variants.last().glyph, c, node);
            } else {
                // No help from the font: the glyph itself, pulled taller.
                const qreal factor = target / (ink.height() * s);
                const qreal wide = qMin(1.5, 1 + 0.12 * (factor - 1));
                QPainterPath path = font->path(g);
                path.translate(0, -ink.bottom());
                path = QTransform::fromScale(s * wide, s * factor).map(path);
                box.items.append(item(path, c, node));
                box.width = font->advance(g) * s * wide;
                box.height = target;
                box.depth = 0;
            }
        }
        box.single = false;
        box.italic = 0;
        if (centre) {
            // By its ink, whatever the baseline of the glyph.
            centreOnAxis(box, c);
        }
        return box;
    }

    /// A horizontal brace, arrow or wide accent `target` pixels wide; its ink starts at x = 0, bottom at y = 0.
    Box wide(char32_t code, qreal target, const Ctx &c, const NodePtr &node, bool stretch) const
    {
        const qreal s = c.em();
        const MathFont *font = c.font;
        quint32 g = font->glyph(code);
        if (!g) {
            font = c.font->fallback();
            g = font->glyph(code);
        }
        Box box;
        if (!g) {
            return box;
        }
        QPainterPath path;
        quint32 chosen = g;
        const QList<F::Variant> variants = font->variants(g, false);
        const QList<F::Part> parts = font->assembly(g, false);
        bool built = false;
        if (font->bounds(g).width() * s < target) {
            if (stretch) {
                // The first one wide enough, or the assembly.
                bool found = false;
                for (const F::Variant &v : variants) {
                    if (font->bounds(v.glyph).width() * s >= target) {
                        chosen = v.glyph;
                        found = true;
                        break;
                    }
                }
                if (!found && !parts.isEmpty()) {
                    qreal length = 0;
                    const QList<Placed> placed = assemble(parts, target / s, font, &length);
                    for (const Placed &part : placed) {
                        QPainterPath piece = font->path(part.glyph);
                        piece.translate(part.at - font->bounds(part.glyph).left(), 0);
                        path = path.isEmpty() ? piece : path.united(piece);
                    }
                    built = true;
                } else if (!found && !variants.isEmpty()) {
                    chosen = variants.last().glyph;
                }
            } else {
                // A wide accent: the widest that still fits.
                for (const F::Variant &v : variants) {
                    if (font->bounds(v.glyph).width() * s <= target) {
                        chosen = v.glyph;
                    }
                }
            }
        }
        if (!built) {
            path = font->path(chosen);
        }
        QRectF r = path.boundingRect();
        qreal sx = s;
        if (stretch && r.width() > 0 && qAbs(r.width() * s - target) > target * 0.02) {
            sx = target / r.width();
        }
        path.translate(-r.left(), -r.bottom());
        path = QTransform::fromScale(sx, s).map(path);
        r = path.boundingRect();
        box.items.append(item(path, c, node));
        box.width = r.width();
        box.height = r.height();
        box.accent = font->topAccent(chosen) < 0 ? -1 : (font->topAccent(chosen) - font->bounds(chosen).left()) * s;
        return box;
    }

    Box delimiter(const QString &text, qreal target, const Ctx &c, const NodePtr &node) const
    {
        if (text.isEmpty()) {
            Box none;
            none.width = 0.12 * c.em();
            return none;
        }
        return tall(text.toUcs4().first(), target, c, node);
    }

    /// TeX's rule for `\left … \right`: nearly as tall as what is inside, measured from the axis.
    static qreal delimiterTarget(const Box &body, const Ctx &c)
    {
        const qreal axis = c.font->axis() * c.em();
        const qreal delta = qMax(body.height - axis, body.depth + axis);
        return qMax(delta * 2 * 0.901, 2 * delta - 0.5 * c.em());
    }

    // ---- Rows ------------------------------------------------------------

    struct Flat {
        NodePtr node;
        Ctx ctx;
    };

    void flatten(const QList<NodePtr> &nodes, const Ctx &c, QList<Flat> &out) const
    {
        for (const NodePtr &node : nodes) {
            if (!node) {
                continue;
            }
            if (node->flows && node->type == Node::Row) {
                flatten(node->kids, c, out);
            } else if (node->flows && node->type == Node::Style) {
                Ctx inner = c;
                apply(node, inner);
                const NodePtr body = node->kids.value(0);
                if (body && body->type == Node::Row) {
                    flatten(body->kids, inner, out);
                } else if (body) {
                    flatten({body}, inner, out);
                }
            } else {
                out.append({node, c});
            }
        }
    }

    static void apply(const NodePtr &style, Ctx &c)
    {
        if (style->text == QLatin1String("font")) {
            c.variant = style->arg;
        } else if (style->text == QLatin1String("color")) {
            const QString name = Latex::colorName(style->arg);
            if (!name.isEmpty()) {
                c.color = QColor(name);
            }
        } else if (style->text == QLatin1String("math")) {
            c.style = style->arg.toInt();
        }
    }

    static qreal spacing(Atom left, Atom right, const Ctx &c)
    {
        // 0 none, 1 thin, 2 medium, 3 thick; negative: not in script styles.
        static const int table[8][8] = {
            /* Ord   */ {0, 1, -2, -3, 0, 0, 0, -1},
            /* Op    */ {1, 1, 0, -3, 0, 0, 0, -1},
            /* Bin   */ {-2, -2, 0, 0, -2, 0, 0, -2},
            /* Rel   */ {-3, -3, 0, 0, -3, 0, 0, -3},
            /* Open  */ {0, 0, 0, 0, 0, 0, 0, 0},
            /* Close */ {0, 1, -2, -3, 0, 0, 0, -1},
            /* Punct */ {-1, -1, 0, -1, -1, -1, -1, -1},
            /* Inner */ {-1, 1, -2, -3, -1, 0, -1, -1},
        };
        int kind = table[int(left)][int(right)];
        if (kind < 0) {
            if (c.style >= ScriptStyle) {
                return 0;
            }
            kind = -kind;
        }
        static const qreal mu[4] = {0, 3, 4, 5};
        return mu[kind] * c.em() / 18;
    }

    Box row(const QList<NodePtr> &nodes, const Ctx &c, bool leadingOrd = false) const
    {
        QList<Flat> flat;
        flatten(nodes, c, flat);
        QList<Piece> pieces;
        if (leadingOrd) {
            pieces.append(Piece());
        }
        for (const Flat &f : flat) {
            pieces.append(piece(f.node, f.ctx));
        }
        // A binary operator with nothing to operate on is an ordinary symbol.
        Atom previous = Atom::Open;
        bool any = false;
        for (int i = 0; i < pieces.size(); ++i) {
            Piece &p = pieces[i];
            if (p.space) {
                continue;
            }
            if (p.atom == Atom::Bin) {
                if (!any || previous == Atom::Bin || previous == Atom::Op || previous == Atom::Rel || previous == Atom::Open || previous == Atom::Punct) {
                    p.atom = Atom::Ord;
                }
            } else if (any && previous == Atom::Bin && (p.atom == Atom::Rel || p.atom == Atom::Close || p.atom == Atom::Punct)) {
                for (int j = i - 1; j >= 0; --j) {
                    if (!pieces.at(j).space) {
                        pieces[j].atom = Atom::Ord;
                        break;
                    }
                }
            }
            previous = p.atom;
            any = true;
        }
        for (int j = pieces.size() - 1; j >= 0; --j) {
            if (!pieces.at(j).space) {
                if (pieces.at(j).atom == Atom::Bin) {
                    pieces[j].atom = Atom::Ord;
                }
                break;
            }
        }
        Box out;
        any = false;
        for (int i = 0; i < pieces.size(); ++i) {
            const Piece &p = pieces.at(i);
            if (!p.space) {
                if (any) {
                    out.width += spacing(previous, p.atom, c);
                }
                previous = p.atom;
                any = true;
            }
            out.add(p.box, out.width, 0);
            out.height = qMax(out.height, p.box.height);
            out.depth = qMax(out.depth, p.box.depth);
            out.width += p.box.width;
        }
        if (pieces.size() == 1 && !leadingOrd) {
            out.italic = pieces.first().box.italic;
            out.accent = pieces.first().box.accent;
            out.single = pieces.first().box.single;
        }
        return out;
    }

    Box boxOf(const NodePtr &node, const Ctx &c) const
    {
        if (!node) {
            return Box();
        }
        if (node->type == Node::Row && !node->hole) {
            return row(node->kids, c);
        }
        return row({node}, c);
    }

    // ---- Nodes -----------------------------------------------------------

    Piece piece(const NodePtr &node, const Ctx &c) const
    {
        Piece p;
        switch (node->type) {
        case Node::Row:
            if (node->hole) {
                p.box = hole(c, node);
            } else {
                p.box = row(node->kids, c);
            }
            break;
        case Node::Symbol:
            p.box = symbol(node, c);
            p.atom = node->atom;
            break;
        case Node::Text: {
            Ctx inner = c;
            inner.variant = node->arg.isEmpty() ? QStringLiteral("rm") : node->arg;
            p.box = textBox(node->text, inner, node, true);
            p.box.single = false;
            break;
        }
        case Node::Frac:
            p.box = fraction(node, c);
            p.atom = Atom::Inner;
            break;
        case Node::Sqrt:
            p.box = radical(node, c);
            break;
        case Node::Script:
            p = script(node, c);
            break;
        case Node::Delim: {
            const NodePtr body = node->kids.value(0);
            const QList<NodePtr> kids = body ? body->kids : QList<NodePtr>();
            Box inner = row(kids, c);
            qreal target = delimiterTarget(inner, c);
            bool middles = false;
            for (const NodePtr &kid : kids) {
                middles = middles || (kid && kid->middle);
            }
            if (middles) {
                Ctx again = c;
                again.middle = target;
                inner = row(kids, again);
            }
            const Box left = delimiter(node->left, target, c, node);
            const Box right = delimiter(node->right, target, c, node);
            p.box.add(left, 0, 0);
            p.box.add(inner, left.width, 0);
            p.box.add(right, left.width + inner.width, 0);
            p.box.width = left.width + inner.width + right.width;
            p.box.height = qMax(inner.height, qMax(left.height, right.height));
            p.box.depth = qMax(inner.depth, qMax(left.depth, right.depth));
            p.atom = Atom::Inner;
            break;
        }
        case Node::Matrix:
            p.box = matrix(node, c);
            p.atom = Atom::Inner;
            break;
        case Node::Accent:
            p.box = accent(node, c);
            break;
        case Node::Style: {
            Ctx inner = c;
            apply(node, inner);
            p.box = boxOf(node->kids.value(0), inner);
            if (node->text == QLatin1String("class")) {
                p.atom = node->atom;
                p.box.single = false;
            } else if (node->text == QLatin1String("phantom")) {
                p.box.items.clear();
            } else if (node->text == QLatin1String("box")) {
                const qreal pad = 0.3 * c.em();
                const qreal t = c.font->ruleThickness() * c.em();
                Box framed;
                const qreal w = p.box.width + 2 * pad + 2 * t;
                const qreal top = -(p.box.height + pad + t);
                const qreal h = p.box.height + p.box.depth + 2 * pad + 2 * t;
                framed.add(ruleBox(0, top, w, t, c, node));
                framed.add(ruleBox(0, top + h - t, w, t, c, node));
                framed.add(ruleBox(0, top, t, h, c, node));
                framed.add(ruleBox(w - t, top, t, h, c, node));
                framed.add(p.box, pad + t, 0);
                framed.width = w;
                framed.height = -top;
                framed.depth = top + h;
                p.box = framed;
            }
            break;
        }
        case Node::Space:
            p.box.width = node->width * c.em() / 18;
            p.space = true;
            break;
        case Node::Error: {
            Ctx inner = c;
            inner.variant = QStringLiteral("tt");
            inner.color = QColor(0xe5, 0x48, 0x4d);
            p.box = textBox(node->text, inner, node, true);
            break;
        }
        }
        return p;
    }

    /// A box to fill in: where an argument is missing.
    static Box hole(const Ctx &c, const NodePtr &node)
    {
        const qreal s = c.em();
        const qreal w = 0.5 * s;
        const qreal h = 0.62 * s;
        const qreal t = qMax(1.0, 0.035 * s);
        Box box;
        box.add(ruleBox(0.05 * s, -h, w, t, c, node));
        box.add(ruleBox(0.05 * s, -t, w, t, c, node));
        box.add(ruleBox(0.05 * s, -h, t, h, c, node));
        box.add(ruleBox(0.05 * s + w - t, -h, t, h, c, node));
        for (Item &it : box.items) {
            it.ghost = true;
        }
        box.width = w + 0.1 * s;
        box.height = h;
        return box;
    }

    Box symbol(const NodePtr &node, const Ctx &c) const
    {
        const QList<uint> codes = node->text.toUcs4();
        if (node->middle) {
            Box box = delimiter(node->text, c.middle > 0 ? c.middle : c.em(), c, node);
            const qreal pad = codes.isEmpty() ? 0 : 0.17 * c.em();
            box.shift(pad, 0);
            box.width += 2 * pad;
            return box;
        }
        if (node->sized > 0) {
            static const qreal steps[] = {1.2, 1.8, 2.4, 3.0};
            return delimiter(node->text, steps[qBound(1, node->sized, 4) - 1] * c.em(), c, node);
        }
        if (node->big && codes.size() == 1) {
            const MathFont *font = c.font;
            quint32 g = font->glyph(codes.first());
            if (!g) {
                font = font->fallback();
                g = font->glyph(codes.first());
            }
            if (!g) {
                return Box();
            }
            Box box;
            if (c.style == Display) {
                // Integrals stand taller than the other operators, as in TeX's own fonts.
                const bool integral = codes.first() >= 0x222B && codes.first() <= 0x2233;
                const qreal wanted = qMax(c.font->constant(F::DisplayOperatorMinHeight), integral ? 2.0 : 0.0);
                quint32 chosen = 0;
                const qreal plain = font->bounds(g).height();
                for (const F::Variant &v : font->variants(g, true)) {
                    // The list starts with the glyph itself: display wants a larger cut.
                    if (font->bounds(v.glyph).height() <= plain * 1.05) {
                        continue;
                    }
                    chosen = v.glyph;
                    if (font->bounds(v.glyph).height() >= wanted * 0.98) {
                        break;
                    }
                }
                if (chosen) {
                    box = glyphBox(font, chosen, c, node);
                } else {
                    // No larger cut: the same glyph, enlarged.
                    Ctx bigger = c;
                    bigger.base *= 1.45;
                    box = glyphBox(font, g, bigger, node);
                }
            } else {
                box = glyphBox(font, g, c, node);
            }
            const qreal italic = box.italic;
            centreOnAxis(box, c);
            box.italic = italic;
            return box;
        }
        return textBox(node->text, c, node, node->upright);
    }

    Box fraction(const NodePtr &node, const Ctx &outer) const
    {
        Ctx c = outer;
        if (node->mathStyle >= 0) {
            c.style = node->mathStyle;
        }
        const bool display = c.style == Display;
        const Box num = boxOf(node->kids.value(0), c.numerator());
        const Box den = boxOf(node->kids.value(1), c.denominator());
        const qreal axis = c.font->axis() * c.em();
        qreal up;
        qreal down;
        qreal t = 0;
        if (node->rule) {
            t = c.k(F::FractionRuleThickness);
            up = c.k(display ? F::FractionNumeratorDisplayStyleShiftUp : F::FractionNumeratorShiftUp);
            down = c.k(display ? F::FractionDenominatorDisplayStyleShiftDown : F::FractionDenominatorShiftDown);
            const qreal gapUp = c.k(display ? F::FractionNumDisplayStyleGapMin : F::FractionNumeratorGapMin);
            const qreal gapDown = c.k(display ? F::FractionDenomDisplayStyleGapMin : F::FractionDenominatorGapMin);
            up = qMax(up, axis + t / 2 + gapUp + num.depth);
            down = qMax(down, gapDown + den.height - (axis - t / 2));
        } else {
            up = c.k(display ? F::StackTopDisplayStyleShiftUp : F::StackTopShiftUp);
            down = c.k(display ? F::StackBottomDisplayStyleShiftDown : F::StackBottomShiftDown);
            const qreal gap = c.k(display ? F::StackDisplayStyleGapMin : F::StackGapMin);
            const qreal have = (up - num.depth) - (den.height - down);
            if (have < gap) {
                up += (gap - have) / 2;
                down += (gap - have) / 2;
            }
        }
        const qreal side = node->rule ? 0.12 * c.em() : 0;
        const qreal pad = node->rule ? 0.1 * c.em() : 0;
        const qreal inner = qMax(num.width, den.width) + 2 * pad;
        Box stack;
        stack.add(num, side + (inner - num.width) / 2, -up);
        stack.add(den, side + (inner - den.width) / 2, down);
        if (node->rule) {
            stack.add(ruleBox(side, -axis - t / 2, inner, t, c, node));
        }
        stack.width = inner + 2 * side;
        stack.height = up + num.height;
        stack.depth = down + den.depth;
        if (node->left.isEmpty() && node->right.isEmpty()) {
            return stack;
        }
        const qreal target = qMax(delimiterTarget(stack, c), (display ? 2.39 : 1.01) * c.em());
        const Box left = delimiter(node->left, target, c, node);
        const Box right = delimiter(node->right, target, c, node);
        Box out;
        out.add(left);
        out.add(stack, left.width, 0);
        out.add(right, left.width + stack.width, 0);
        out.width = left.width + stack.width + right.width;
        out.height = qMax(stack.height, qMax(left.height, right.height));
        out.depth = qMax(stack.depth, qMax(left.depth, right.depth));
        return out;
    }

    Box radical(const NodePtr &node, const Ctx &c) const
    {
        const Box body = boxOf(node->kids.value(0), c.cramp());
        const qreal t = c.k(F::RadicalRuleThickness);
        qreal gap = c.k(c.style == Display ? F::RadicalDisplayStyleVerticalGap : F::RadicalVerticalGap);
        const qreal wanted = body.height + body.depth + gap + t;
        Box surd = tall(0x221A, wanted, c, node, false);
        const qreal total = surd.height + surd.depth;
        if (total > wanted) {
            gap += (total - wanted) / 2;
        }
        // The top of the sign is where the bar starts.
        const qreal top = body.height + gap + t;
        const qreal dy = surd.height - top;
        Box out;
        qreal x = 0;
        const NodePtr indexNode = node->kids.value(1);
        if (indexNode) {
            const Box index = boxOf(indexNode, c.with(ScriptScript, c.cramped));
            const qreal raise = c.font->constant(F::RadicalDegreeBottomRaisePercent) * total;
            const qreal bottom = (total - top) - raise;
            x = qMax(0.0, c.k(F::RadicalKernBeforeDegree));
            out.add(index, x, bottom - index.depth);
            out.height = index.height - (bottom - index.depth);
            x = qMax(0.0, x + index.width + c.k(F::RadicalKernAfterDegree));
        }
        out.add(surd, x, dy);
        x += surd.width;
        out.add(ruleBox(x - 0.01 * c.em(), -top, body.width + 0.01 * c.em(), t, c, node));
        out.add(body, x, 0);
        out.width = x + body.width;
        out.height = qMax(out.height, top + c.k(F::RadicalExtraAscender));
        out.depth = qMax(body.depth, total - top);
        return out;
    }

    static bool wantsLimits(const NodePtr &base, const Ctx &c)
    {
        if (!base) {
            return false;
        }
        if (base->limits == 1) {
            return true;
        }
        if (base->limits == 2) {
            return false;
        }
        return base->movable && c.style == Display;
    }

    Piece script(const NodePtr &node, const Ctx &c) const
    {
        const NodePtr baseNode = node->kids.value(0);
        const NodePtr subNode = node->kids.value(1);
        const NodePtr supNode = node->kids.value(2);
        Piece p;
        Box base;
        if (baseNode) {
            const Piece b = piece(baseNode, c);
            base = b.box;
            p.atom = b.atom;
        }
        if (node->arg == QLatin1String("rel")) {
            p.atom = Atom::Rel;
        }
        const Box sup = supNode ? boxOf(supNode, c.sup()) : Box();
        const Box sub = subNode ? boxOf(subNode, c.sub()) : Box();
        const bool bigOp = baseNode && baseNode->type == Node::Symbol && baseNode->big;

        if (node->stacked || wantsLimits(baseNode, c)) {
            const qreal lean = bigOp ? base.italic / 2 : 0;
            const qreal width = qMax(base.width, qMax(sup.width, sub.width));
            Box out;
            out.add(base, (width - base.width) / 2, 0);
            out.height = base.height;
            out.depth = base.depth;
            if (supNode) {
                const qreal gap = qMax(c.k(F::UpperLimitGapMin), c.k(F::UpperLimitBaselineRiseMin) - sup.depth);
                const qreal y = -(base.height + gap + sup.depth);
                out.add(sup, (width - sup.width) / 2 + lean, y);
                out.height = -y + sup.height;
            }
            if (subNode) {
                const qreal gap = qMax(c.k(F::LowerLimitGapMin), c.k(F::LowerLimitBaselineDropMin) - sub.height);
                const qreal y = base.depth + gap + sub.height;
                out.add(sub, (width - sub.width) / 2 - lean, y);
                out.depth = y + sub.depth;
            }
            out.width = width;
            p.box = out;
            return p;
        }

        // TeX's rule 18, with the font's constants.
        const Ctx sc = c.sup();
        qreal u = 0;
        qreal v = 0;
        // A lone letter takes its scripts where the font says; anything taller, by its own height. So does a big operator.
        if (!base.single || bigOp) {
            u = base.height - sc.k(F::SuperscriptBaselineDropMax);
            v = base.depth + sc.k(F::SubscriptBaselineDropMin);
        }
        if (supNode) {
            u = qMax(u, c.k(c.cramped ? F::SuperscriptShiftUpCramped : F::SuperscriptShiftUp));
            u = qMax(u, sup.depth + c.k(F::SuperscriptBottomMin));
        }
        if (subNode && !supNode) {
            v = qMax(v, c.k(F::SubscriptShiftDown));
            v = qMax(v, sub.height - c.k(F::SubscriptTopMax));
        } else if (subNode) {
            v = qMax(v, c.k(F::SubscriptShiftDown));
            const qreal gap = (u - sup.depth) - (sub.height - v);
            const qreal wanted = c.k(F::SubSuperscriptGapMin);
            if (gap < wanted) {
                v += wanted - gap;
                const qreal psi = c.k(F::SuperscriptBottomMaxWithSubscript) - (u - sup.depth);
                if (psi > 0) {
                    u += psi;
                    v -= psi;
                }
            }
        }
        Box out = base;
        out.single = false;
        out.accent = -1;
        qreal width = base.width;
        const qreal after = c.k(F::SpaceAfterScript);
        if (supNode) {
            const qreal x = base.width + (bigOp ? 0 : base.italic);
            out.add(sup, x, -u);
            out.height = qMax(out.height, u + sup.height);
            width = qMax(width, x + sup.width + after);
        }
        if (subNode) {
            const qreal x = base.width - (bigOp ? base.italic : 0);
            out.add(sub, x, v);
            out.depth = qMax(out.depth, v + sub.depth);
            width = qMax(width, x + sub.width + after);
        }
        out.width = width;
        out.italic = 0;
        p.box = out;
        return p;
    }

    Box accent(const NodePtr &node, const Ctx &c) const
    {
        const QString &name = node->text;
        Box body = boxOf(node->kids.value(0), c.cramp());
        const qreal s = c.em();
        Box out = body;
        out.single = false;
        if (name == QLatin1String("overline") || name == QLatin1String("underline")) {
            const bool over = name.startsWith(QLatin1Char('o'));
            const qreal t = c.k(over ? F::OverbarRuleThickness : F::UnderbarRuleThickness);
            const qreal gap = c.k(over ? F::OverbarVerticalGap : F::UnderbarVerticalGap);
            if (over) {
                out.add(ruleBox(0, -(body.height + gap + t), body.width, t, c, node));
                out.height = body.height + gap + t + c.k(F::OverbarExtraAscender);
            } else {
                out.add(ruleBox(0, body.depth + gap, body.width, t, c, node));
                out.depth = body.depth + gap + t + c.k(F::UnderbarExtraDescender);
            }
            return out;
        }
        if (name == QLatin1String("overbrace") || name == QLatin1String("underbrace")) {
            const bool over = name.startsWith(QLatin1Char('o'));
            const Box brace = wide(over ? 0x23DE : 0x23DF, body.width, c, node, true);
            const qreal gap = 0.2 * s;
            const qreal x = (body.width - brace.width) / 2;
            if (over) {
                out.add(brace, x, -(body.height + gap));
                out.height = body.height + gap + brace.height;
            } else {
                out.add(brace, x, body.depth + gap + brace.height);
                out.depth = body.depth + gap + brace.height;
            }
            return out;
        }
        if (name == QLatin1String("overrightarrow") || name == QLatin1String("overleftarrow")) {
            const Box arrow = wide(name == QLatin1String("overrightarrow") ? 0x2192 : 0x2190, body.width, c.sup(), node, true);
            const qreal gap = 0.08 * s;
            out.add(arrow, (body.width - arrow.width) / 2, -(body.height + gap));
            out.height = body.height + gap + arrow.height;
            return out;
        }
        static const QHash<QString, char32_t> marks = {
            {QStringLiteral("hat"), 0x302}, {QStringLiteral("widehat"), 0x302}, {QStringLiteral("bar"), 0x304},
            {QStringLiteral("vec"), 0x20D7}, {QStringLiteral("dot"), 0x307}, {QStringLiteral("ddot"), 0x308},
            {QStringLiteral("tilde"), 0x303}, {QStringLiteral("widetilde"), 0x303}, {QStringLiteral("check"), 0x30C},
            {QStringLiteral("breve"), 0x306}, {QStringLiteral("acute"), 0x301}, {QStringLiteral("grave"), 0x300},
            {QStringLiteral("mathring"), 0x30A},
        };
        static const QHash<char32_t, char32_t> spacing = {
            {0x302, 0x2C6}, {0x304, 0xAF}, {0x20D7, 0x2192}, {0x307, 0x2D9}, {0x308, 0xA8}, {0x303, 0x2DC},
            {0x30C, 0x2C7}, {0x306, 0x2D8}, {0x301, 0xB4}, {0x300, 0x60}, {0x30A, 0x2DA},
        };
        char32_t code = marks.value(name, 0x302);
        const bool hasMark = c.font->glyph(code) != 0;
        if (!hasMark) {
            code = spacing.value(code, code);
        }
        const bool isWide = name.startsWith(QLatin1String("wide"));
        Box mark = wide(code, body.width, code == 0x2192 ? c.sup() : c, node, false);
        if (mark.isEmpty()) {
            return out;
        }
        // The mark keeps the distance it has over a letter of x height, and rises with a taller one.
        const qreal baseHeight = c.k(F::AccentBaseHeight);
        const qreal gap = hasMark ? 0.06 * s : 0.08 * s;
        const qreal bottom = qMax(body.height, isWide ? 0.0 : baseHeight) + gap;
        const qreal over = body.accent >= 0 ? body.accent : body.width / 2 + body.italic / 2;
        const qreal at = mark.accent >= 0 ? mark.accent : mark.width / 2;
        out.add(mark, over - at, -bottom);
        out.height = bottom + mark.height;
        out.accent = body.accent;
        return out;
    }

    Box matrix(const NodePtr &node, const Ctx &c) const
    {
        const QString &env = node->text;
        const bool small = env == QLatin1String("smallmatrix") || env == QLatin1String("substack");
        const bool plain = env.endsWith(QLatin1String("matrix")) || env == QLatin1String("array") || env == QLatin1String("cases");
        const bool aligned = !small && !plain;
        Ctx cell = c;
        cell.middle = 0;
        if (small) {
            cell.style = c.style <= TextStyle ? ScriptStyle : ScriptScript;
        } else if (plain) {
            cell.style = qMax(c.style, int(TextStyle));
        }
        const qreal e = cell.em();
        const QString columns = node->columns.isEmpty() ? QStringLiteral("c") : node->columns;
        const bool pairs = columns == QLatin1String("rl");

        int count = 0;
        for (const QList<NodePtr> &line : node->cells) {
            count = qMax(count, line.size());
        }
        QList<QList<Box>> boxes;
        QList<qreal> widths(count, 0.0);
        for (const QList<NodePtr> &line : node->cells) {
            QList<Box> out;
            for (int i = 0; i < line.size(); ++i) {
                const NodePtr &cellNode = line.at(i);
                // The right half of an aligned pair starts as if something stood before it.
                const bool lead = pairs && i % 2 == 1;
                Box box = cellNode ? row(cellNode->kids, cell, lead) : Box();
                widths[i] = qMax(widths.at(i), box.width);
                out.append(box);
            }
            boxes.append(out);
        }
        const qreal strutHeight = (small ? 0.6 : 0.84) * e;
        const qreal strutDepth = (small ? 0.2 : 0.36) * e;
        const qreal rowGap = aligned && cell.style == Display ? 0.3 * e : small ? 0.1 * e : 0;
        QList<qreal> baselines;
        qreal y = 0;
        qreal lastDepth = 0;
        for (int r = 0; r < boxes.size(); ++r) {
            qreal h = strutHeight;
            qreal d = strutDepth;
            for (const Box &box : boxes.at(r)) {
                h = qMax(h, box.height);
                d = qMax(d, box.depth);
            }
            // A row taller than the strut still keeps clear of the one above.
            const qreal clear = r > 0 && (h > strutHeight || lastDepth > strutDepth) ? 0.14 * e : 0;
            y += h + (r > 0 ? rowGap : 0) + clear;
            baselines.append(y);
            y += d;
            lastDepth = d;
        }
        const qreal total = y;
        const qreal axis = c.font->axis() * c.em();
        const qreal top = -(total / 2 + axis);

        Box grid;
        QList<qreal> lefts;
        qreal x = 0;
        for (int i = 0; i < count; ++i) {
            if (i > 0) {
                x += pairs ? (i % 2 == 0 ? 2 * e : 0)
                    : small ? 0.35 * e
                    : env == QLatin1String("eqnarray") || env == QLatin1String("eqnarray*") ? 0.5 * e : e;
            }
            lefts.append(x);
            x += widths.at(i);
        }
        for (int r = 0; r < boxes.size(); ++r) {
            for (int i = 0; i < boxes.at(r).size(); ++i) {
                const QChar align = columns.at(i % columns.size());
                const Box &box = boxes.at(r).at(i);
                const qreal slack = widths.at(i) - box.width;
                const qreal dx = align == QLatin1Char('l') ? 0 : align == QLatin1Char('r') ? slack : slack / 2;
                grid.add(box, lefts.at(i) + dx, top + baselines.at(r));
            }
        }
        grid.width = x;
        grid.height = -top;
        grid.depth = total + top;
        if (node->left.isEmpty() && node->right.isEmpty()) {
            return grid;
        }
        const qreal target = delimiterTarget(grid, c);
        const Box left = delimiter(node->left, target, c, node);
        const Box right = delimiter(node->right, target, c, node);
        const qreal pad = env == QLatin1String("cases") ? 0 : 0.2 * e;
        Box out;
        out.add(left);
        out.add(grid, left.width + pad, 0);
        out.add(right, left.width + grid.width + 2 * pad, 0);
        out.width = left.width + grid.width + 2 * pad + right.width;
        out.height = qMax(grid.height, qMax(left.height, right.height));
        out.depth = qMax(grid.depth, qMax(left.depth, right.depth));
        return out;
    }
};

void appendPath(QByteArray &out, const QPainterPath &path, qreal dx, qreal dy)
{
    const auto number = [](qreal v) { return QByteArray::number(qRound(v * 100) / 100.0, 'f', 2).replace(".00", ""); };
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            out += 'M' + number(e.x + dx) + ' ' + number(e.y + dy);
            break;
        case QPainterPath::LineToElement:
            out += 'L' + number(e.x + dx) + ' ' + number(e.y + dy);
            break;
        case QPainterPath::CurveToElement: {
            out += 'C' + number(e.x + dx) + ' ' + number(e.y + dy);
            for (int k = 1; k <= 2 && i + k < path.elementCount(); ++k) {
                const QPainterPath::Element d = path.elementAt(i + k);
                out += ' ' + number(d.x + dx) + ' ' + number(d.y + dy);
            }
            i += 2;
            break;
        }
        default:
            break;
        }
    }
    if (path.elementCount() > 0) {
        out += 'Z';
    }
}

}

void Box::shift(qreal dx, qreal dy)
{
    for (Item &it : items) {
        it.path.translate(dx, dy);
    }
}

void Box::add(const Box &other, qreal dx, qreal dy)
{
    for (Item it : other.items) {
        it.path.translate(dx, dy);
        items.append(it);
    }
}

namespace Formula {

Layout layout(const QString &latex, const LayoutOptions &options)
{
    Layout out;
    const Parsed parsed = Latex::parse(latex);
    out.root = parsed.root;
    out.errors = parsed.errors;
    out.box = layout(parsed.root, options);
    out.family = MathFont::get(options.family)->family();
    return out;
}

Box layout(const NodePtr &root, const LayoutOptions &options)
{
    return Engine(options).run(root);
}

QSizeF size(const Box &box, qreal padding)
{
    return QSizeF(box.width + 2 * padding, box.height + box.depth + 2 * padding);
}

void paint(QPainter *painter, const Box &box, const QPointF &origin, const QColor &color)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->translate(origin);
    for (const Item &it : box.items) {
        QColor fill = it.color.isValid() ? it.color : color;
        if (it.ghost) {
            fill.setAlphaF(fill.alphaF() * 0.35);
        }
        painter->fillPath(it.path, fill);
    }
    painter->restore();
}

QImage image(const Box &box, const QColor &color, qreal scale, qreal padding, const QColor &background)
{
    const QSizeF natural = size(box, padding);
    const QSize pixels(qMax(1, qCeil(natural.width() * scale)), qMax(1, qCeil(natural.height() * scale)));
    QImage out(pixels, QImage::Format_ARGB32_Premultiplied);
    out.fill(background);
    QPainter painter(&out);
    painter.scale(scale, scale);
    paint(&painter, box, QPointF(padding, padding + box.height), color);
    painter.end();
    return out;
}

QByteArray svg(const Box &box, const QColor &color, qreal padding)
{
    const QSizeF natural = size(box, padding);
    const auto number = [](qreal v) { return QByteArray::number(qCeil(v * 100) / 100.0, 'f', 2); };
    QByteArray out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\""
        + number(natural.width()) + "\" height=\"" + number(natural.height()) + "\" viewBox=\"0 0 " + number(natural.width()) + ' '
        + number(natural.height()) + "\">\n";
    // Runs of one colour share a path.
    QByteArray data;
    QColor current;
    bool ghost = false;
    const auto flush = [&] {
        if (data.isEmpty()) {
            return;
        }
        const QColor fill = current.isValid() ? current : color;
        qreal opacity = fill.alphaF() * (ghost ? 0.35 : 1);
        out += "  <path fill=\"" + fill.name(QColor::HexRgb).toLatin1() + '"';
        if (opacity < 0.999) {
            out += " fill-opacity=\"" + QByteArray::number(opacity, 'f', 2) + '"';
        }
        out += " d=\"" + data + "\"/>\n";
        data.clear();
    };
    for (const Item &it : box.items) {
        if (it.color != current || it.ghost != ghost) {
            flush();
            current = it.color;
            ghost = it.ghost;
        }
        appendPath(data, it.path, padding, padding + box.height);
    }
    flush();
    out += "</svg>\n";
    return out;
}

int sourceAt(const Box &box, const QPointF &point)
{
    int best = -1;
    qreal bestDistance = 1e18;
    for (const Item &it : box.items) {
        const QRectF r = it.path.boundingRect();
        const qreal dx = qMax(0.0, qMax(r.left() - point.x(), point.x() - r.right()));
        const qreal dy = qMax(0.0, qMax(r.top() - point.y(), point.y() - r.bottom()));
        // Among those under the point, the smallest wins.
        const qreal distance = dx * dx + dy * dy + (dx == 0 && dy == 0 ? r.width() * r.height() * 1e-9 : 1);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = it.end;
        }
    }
    return best;
}

QRectF rectOf(const Box &box, int position)
{
    int span = INT_MAX;
    for (const Item &it : box.items) {
        if (position >= it.start && position <= it.end && it.end > it.start) {
            span = qMin(span, it.end - it.start);
        }
    }
    QRectF out;
    for (const Item &it : box.items) {
        if (position >= it.start && position <= it.end && it.end - it.start == span) {
            out = out.united(it.path.boundingRect());
        }
    }
    return out;
}

}
