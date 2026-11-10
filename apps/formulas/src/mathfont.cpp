#include "mathfont.h"

#include <QFont>
#include <QFontDatabase>
#include <QtEndian>

namespace {

const qreal masterSize = 1000;

/// Latin Modern Math's constants, in thousandths of an em: what a font without a MATH table gets.
const int defaults[MathFont::ConstantCount] = {
    70, 50, 1300, 1300,
    154, 250, 450, 664,
    247, 344, 200,
    363, 289, 108, 250,
    160, 344, 56,
    200, 111, 167, 600,
    444, 677, 345, 686,
    120, 280,
    111, 600, 200, 167,
    394, 677,
    345, 686,
    40, 120, 40,
    40, 120,
    350, 96,
    120, 40, 40,
    120, 40, 40,
    50, 148, 40, 40,
    278, -556, 60,
};

struct Table {
    QByteArray data;
    bool has(int offset, int bytes = 2) const { return offset >= 0 && offset + bytes <= data.size(); }
    quint16 u16(int offset) const { return has(offset) ? qFromBigEndian<quint16>(data.constData() + offset) : 0; }
    qint16 s16(int offset) const { return has(offset) ? qFromBigEndian<qint16>(data.constData() + offset) : 0; }

    /// The glyphs of a coverage table, in the order of their coverage index.
    QList<quint32> coverage(int offset) const
    {
        QList<quint32> out;
        const int format = u16(offset);
        const int count = u16(offset + 2);
        if (format == 1) {
            for (int i = 0; i < count && has(offset + 4 + 2 * i); ++i) {
                out.append(u16(offset + 4 + 2 * i));
            }
        } else if (format == 2) {
            for (int i = 0; i < count && has(offset + 4 + 6 * i, 6); ++i) {
                const int first = u16(offset + 4 + 6 * i);
                const int last = u16(offset + 6 + 6 * i);
                for (int g = first; g <= last && out.size() < 65536; ++g) {
                    out.append(g);
                }
            }
        }
        return out;
    }
};

QRawFont rawOf(const QString &family)
{
    QFont font(family);
    font.setPixelSize(int(masterSize));
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::NoFontMerging);
    return QRawFont::fromFont(font);
}

}

QStringList MathFont::available()
{
    static const QStringList found = [] {
        static const char *const preferred[] = {
            "Latin Modern Math", "STIX Two Math", "Libertinus Math", "TeX Gyre Termes Math", "TeX Gyre Pagella Math",
            "Cambria Math", "XITS Math", "Asana Math", "DejaVu Math TeX Gyre", "Fira Math", "Noto Sans Math",
        };
        QStringList with;
        for (const QString &family : QFontDatabase::families()) {
            if (!family.contains(QLatin1String("math"), Qt::CaseInsensitive)) {
                continue;
            }
            const QRawFont raw = rawOf(family);
            if (raw.isValid() && raw.familyName() == family && !raw.fontTable("MATH").isEmpty()) {
                with.append(family);
            }
        }
        QStringList out;
        for (const char *name : preferred) {
            const QString family = QString::fromLatin1(name);
            if (with.removeAll(family) > 0) {
                out.append(family);
            }
        }
        return out + with;
    }();
    return found;
}

const MathFont *MathFont::get(const QString &family)
{
    static QHash<QString, MathFont *> cache;
    QString wanted = family;
    if (wanted.isEmpty()) {
        const QStringList fonts = available();
        wanted = fonts.isEmpty() ? QStringLiteral("serif") : fonts.first();
    }
    auto it = cache.constFind(wanted);
    if (it != cache.constEnd()) {
        return it.value();
    }
    MathFont *font = new MathFont(wanted);
    // Asked for by name and not installed: Qt would hand over whatever it has.
    if (!family.isEmpty() && (!font->m_raw.isValid() || font->m_raw.familyName() != family)) {
        delete font;
        return get(QString());
    }
    cache.insert(wanted, font);
    return font;
}

MathFont::MathFont(const QString &family)
    : m_family(family)
    , m_raw(rawOf(family))
{
    for (int i = 0; i < ConstantCount; ++i) {
        m_constants[i] = defaults[i] / (i < 2 || i == RadicalDegreeBottomRaisePercent ? 100.0 : 1000.0);
    }
    if (!m_raw.isValid()) {
        return;
    }
    m_unit = m_raw.unitsPerEm() > 0 ? m_raw.unitsPerEm() : 1000;
    if (m_raw.xHeight() > 0) {
        m_xHeight = m_raw.xHeight() / masterSize;
    }
    readMath();
    readScriptForms();
}

quint32 MathFont::scriptForm(quint32 glyph, int level) const
{
    const auto found = m_scriptForms.constFind(glyph);
    if (found == m_scriptForms.constEnd() || level < 1) {
        return glyph;
    }
    return found->at(qMin(level, int(found->size())) - 1);
}

void MathFont::readScriptForms()
{
    const Table t{m_raw.fontTable("GSUB")};
    if (t.data.size() < 10 || t.u16(0) != 1) {
        return;
    }
    const int features = t.u16(6);
    const int lookups = t.u16(8);
    QList<int> wanted;
    for (int i = 0, count = t.u16(features); i < count; ++i) {
        const int record = features + 2 + 6 * i;
        if (!t.has(record, 6) || t.data.mid(record, 4) != "ssty") {
            continue;
        }
        const int feature = features + t.u16(record + 4);
        for (int k = 0, n = t.u16(feature + 2); k < n; ++k) {
            const int index = t.u16(feature + 4 + 2 * k);
            if (!wanted.contains(index)) {
                wanted.append(index);
            }
        }
    }
    for (const int index : std::as_const(wanted)) {
        if (index >= t.u16(lookups)) {
            continue;
        }
        const int lookup = lookups + t.u16(lookups + 2 + 2 * index);
        for (int k = 0, n = t.u16(lookup + 4); k < n; ++k) {
            int type = t.u16(lookup);
            int table = lookup + t.u16(lookup + 6 + 2 * k);
            if (type == 7 && t.u16(table) == 1) {
                // An extension: the real table is further away than 16 bits reach.
                type = t.u16(table + 2);
                table += int((quint32(t.u16(table + 4)) << 16) | t.u16(table + 6));
            }
            const int format = t.u16(table);
            const QList<quint32> covered = t.coverage(table + t.u16(table + 2));
            for (int g = 0; g < covered.size(); ++g) {
                QList<quint32> forms;
                if (type == 1 && format == 1) {
                    forms.append((covered.at(g) + t.s16(table + 4)) & 0xffff);
                } else if (type == 1 && format == 2 && g < t.u16(table + 4)) {
                    forms.append(t.u16(table + 6 + 2 * g));
                } else if (type == 3 && format == 1 && g < t.u16(table + 4)) {
                    const int set = table + t.u16(table + 6 + 2 * g);
                    for (int a = 0, count = t.u16(set); a < count; ++a) {
                        forms.append(t.u16(set + 2 + 2 * a));
                    }
                }
                if (!forms.isEmpty() && !m_scriptForms.contains(covered.at(g))) {
                    m_scriptForms.insert(covered.at(g), forms);
                }
            }
        }
    }
}

void MathFont::readMath()
{
    const Table t{m_raw.fontTable("MATH")};
    if (t.data.size() < 10 || t.u16(0) != 1) {
        return;
    }
    m_hasMath = true;
    const int constants = t.u16(4);
    const int glyphInfo = t.u16(6);
    const int variants = t.u16(8);

    if (constants > 0 && t.has(constants, 214)) {
        m_constants[ScriptPercentScaleDown] = t.s16(constants) / 100.0;
        m_constants[ScriptScriptPercentScaleDown] = t.s16(constants + 2) / 100.0;
        m_constants[DelimitedSubFormulaMinHeight] = t.u16(constants + 4) / m_unit;
        m_constants[DisplayOperatorMinHeight] = t.u16(constants + 6) / m_unit;
        for (int i = MathLeading; i < RadicalDegreeBottomRaisePercent; ++i) {
            m_constants[i] = t.s16(constants + 8 + 4 * (i - MathLeading)) / m_unit;
        }
        m_constants[RadicalDegreeBottomRaisePercent] = t.s16(constants + 8 + 4 * (RadicalDegreeBottomRaisePercent - MathLeading)) / 100.0;
    }

    const auto values = [&](int table, QHash<quint32, qreal> &into) {
        if (table <= 0 || !t.has(table, 4)) {
            return;
        }
        const QList<quint32> glyphs = t.coverage(table + t.u16(table));
        const int count = t.u16(table + 2);
        for (int i = 0; i < count && i < glyphs.size(); ++i) {
            into.insert(glyphs.at(i), t.s16(table + 4 + 4 * i) / m_unit);
        }
    };
    if (glyphInfo > 0) {
        if (const int offset = t.u16(glyphInfo)) {
            values(glyphInfo + offset, m_italics);
        }
        if (const int offset = t.u16(glyphInfo + 2)) {
            values(glyphInfo + offset, m_topAccents);
        }
    }

    if (variants > 0 && t.has(variants, 10)) {
        m_minOverlap = t.u16(variants) / m_unit;
        const int verticalCount = t.u16(variants + 6);
        const int horizontalCount = t.u16(variants + 8);
        const auto constructions = [&](int coverageOffset, int count, int firstOffset, QHash<quint32, Construction> &into) {
            if (coverageOffset == 0) {
                return;
            }
            const QList<quint32> glyphs = t.coverage(variants + coverageOffset);
            for (int i = 0; i < count && i < glyphs.size(); ++i) {
                const int at = variants + t.u16(firstOffset + 2 * i);
                Construction construction;
                const int assemblyOffset = t.u16(at);
                const int variantCount = t.u16(at + 2);
                for (int v = 0; v < variantCount && t.has(at + 4 + 4 * v, 4); ++v) {
                    construction.variants.append({t.u16(at + 4 + 4 * v), t.u16(at + 6 + 4 * v) / m_unit});
                }
                if (assemblyOffset > 0) {
                    const int assembly = at + assemblyOffset;
                    const int partCount = t.u16(assembly + 4);
                    for (int k = 0; k < partCount && t.has(assembly + 6 + 10 * k, 10); ++k) {
                        const int part = assembly + 6 + 10 * k;
                        construction.parts.append({t.u16(part), t.u16(part + 2) / m_unit, t.u16(part + 4) / m_unit,
                                                   t.u16(part + 6) / m_unit, (t.u16(part + 8) & 1) != 0});
                    }
                }
                into.insert(glyphs.at(i), construction);
            }
        };
        constructions(t.u16(variants + 2), verticalCount, variants + 10, m_vertical);
        constructions(t.u16(variants + 4), horizontalCount, variants + 10 + 2 * verticalCount, m_horizontal);
    }
}

quint32 MathFont::glyph(char32_t code) const
{
    const auto it = m_glyphs.constFind(code);
    if (it != m_glyphs.constEnd()) {
        return it.value();
    }
    quint32 glyphs[4] = {0, 0, 0, 0};
    int count = 4;
    const QString text = QString::fromUcs4(&code, 1);
    quint32 out = 0;
    if (m_raw.isValid() && m_raw.glyphIndexesForChars(text.constData(), text.size(), glyphs, &count) && count > 0) {
        out = glyphs[0];
    }
    m_glyphs.insert(code, out);
    return out;
}

qreal MathFont::advance(quint32 glyph) const
{
    QPointF advance;
    if (m_raw.isValid() && m_raw.advancesForGlyphIndexes(&glyph, &advance, 1, QRawFont::SeparateAdvances)) {
        return advance.x() / masterSize;
    }
    return 0.5;
}

QPainterPath MathFont::path(quint32 glyph) const
{
    const auto it = m_paths.constFind(glyph);
    if (it != m_paths.constEnd()) {
        return it.value();
    }
    QPainterPath path;
    if (m_raw.isValid()) {
        path = QTransform::fromScale(1 / masterSize, 1 / masterSize).map(m_raw.pathForGlyph(glyph));
        path.setFillRule(Qt::WindingFill);
    }
    m_paths.insert(glyph, path);
    return path;
}

QRectF MathFont::bounds(quint32 glyph) const
{
    return path(glyph).boundingRect();
}

QList<MathFont::Variant> MathFont::variants(quint32 glyph, bool vertical) const
{
    return (vertical ? m_vertical : m_horizontal).value(glyph).variants;
}

QList<MathFont::Part> MathFont::assembly(quint32 glyph, bool vertical) const
{
    return (vertical ? m_vertical : m_horizontal).value(glyph).parts;
}

const MathFont *MathFont::fallback() const
{
    // A text serif first: it has the accented letters a math font may lack.
    static const MathFont *serif = new MathFont(QStringLiteral("DejaVu Serif"));
    return serif == this || !serif->m_raw.isValid() ? this : serif;
}
