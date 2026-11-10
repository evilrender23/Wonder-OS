#pragma once

#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QRawFont>
#include <QString>
#include <QStringList>

/// A font to set mathematics with: its glyphs as outlines, and what its
/// OpenType MATH table says about how to stack them. A font without that
/// table still works, with TeX's defaults and stretched delimiters.
class MathFont
{
public:
    /// The MATH table's constants, in its order.
    enum Constant {
        ScriptPercentScaleDown, ScriptScriptPercentScaleDown, DelimitedSubFormulaMinHeight, DisplayOperatorMinHeight,
        MathLeading, AxisHeight, AccentBaseHeight, FlattenedAccentBaseHeight,
        SubscriptShiftDown, SubscriptTopMax, SubscriptBaselineDropMin,
        SuperscriptShiftUp, SuperscriptShiftUpCramped, SuperscriptBottomMin, SuperscriptBaselineDropMax,
        SubSuperscriptGapMin, SuperscriptBottomMaxWithSubscript, SpaceAfterScript,
        UpperLimitGapMin, UpperLimitBaselineRiseMin, LowerLimitGapMin, LowerLimitBaselineDropMin,
        StackTopShiftUp, StackTopDisplayStyleShiftUp, StackBottomShiftDown, StackBottomDisplayStyleShiftDown,
        StackGapMin, StackDisplayStyleGapMin,
        StretchStackTopShiftUp, StretchStackBottomShiftDown, StretchStackGapAboveMin, StretchStackGapBelowMin,
        FractionNumeratorShiftUp, FractionNumeratorDisplayStyleShiftUp,
        FractionDenominatorShiftDown, FractionDenominatorDisplayStyleShiftDown,
        FractionNumeratorGapMin, FractionNumDisplayStyleGapMin, FractionRuleThickness,
        FractionDenominatorGapMin, FractionDenomDisplayStyleGapMin,
        SkewedFractionHorizontalGap, SkewedFractionVerticalGap,
        OverbarVerticalGap, OverbarRuleThickness, OverbarExtraAscender,
        UnderbarVerticalGap, UnderbarRuleThickness, UnderbarExtraDescender,
        RadicalVerticalGap, RadicalDisplayStyleVerticalGap, RadicalRuleThickness, RadicalExtraAscender,
        RadicalKernBeforeDegree, RadicalKernAfterDegree, RadicalDegreeBottomRaisePercent,
        ConstantCount
    };

    struct Variant {
        quint32 glyph = 0;
        /// Its extent along the direction it grows, in em.
        qreal advance = 0;
    };
    struct Part {
        quint32 glyph = 0;
        qreal startConnector = 0;
        qreal endConnector = 0;
        qreal advance = 0;
        bool extender = false;
    };

    /// The installed families that carry a MATH table, the best known first.
    static QStringList available();
    /// The font of a family; an empty or unknown one gives the default. Never null.
    static const MathFont *get(const QString &family = QString());

    QString family() const { return m_family; }
    /// It has a MATH table.
    bool hasMath() const { return m_hasMath; }

    /// The glyph of a character, or 0 when the font lacks it.
    quint32 glyph(char32_t code) const;
    /// The drawing of `glyph` meant for scripts (`level` 1) or scripts of scripts (2), when the font has one: its `ssty` feature.
    quint32 scriptForm(quint32 glyph, int level) const;
    /// Everything below is in em: multiply by the size in pixels.
    qreal constant(Constant which) const { return m_constants[which]; }
    qreal advance(quint32 glyph) const;
    /// The ink of the glyph, y down from the baseline.
    QRectF bounds(quint32 glyph) const;
    /// Its outline at one em = 1 unit, y down.
    QPainterPath path(quint32 glyph) const;
    qreal italic(quint32 glyph) const { return m_italics.value(glyph, 0); }
    /// Where an accent sits over the glyph, or a negative number when the font does not say.
    qreal topAccent(quint32 glyph) const { return m_topAccents.value(glyph, -1); }
    QList<Variant> variants(quint32 glyph, bool vertical) const;
    QList<Part> assembly(quint32 glyph, bool vertical) const;
    qreal minConnectorOverlap() const { return m_minOverlap; }
    /// The line a fraction bar is on, and how thick a rule is.
    qreal axis() const { return m_constants[AxisHeight]; }
    qreal ruleThickness() const { return m_constants[FractionRuleThickness]; }
    /// The height of an x, for the accents of fonts that say nothing.
    qreal xHeight() const { return m_xHeight; }

    /// A font for what this one lacks (accented letters in `\text`, rare symbols). May be this one.
    const MathFont *fallback() const;

private:
    explicit MathFont(const QString &family);
    void readMath();
    void readScriptForms();

    QString m_family;
    QRawFont m_raw;
    qreal m_unit = 1000;
    bool m_hasMath = false;
    qreal m_constants[ConstantCount];
    qreal m_minOverlap = 0;
    qreal m_xHeight = 0.45;
    QHash<quint32, QList<quint32>> m_scriptForms;
    QHash<quint32, qreal> m_italics;
    QHash<quint32, qreal> m_topAccents;
    struct Construction {
        QList<Variant> variants;
        QList<Part> parts;
    };
    QHash<quint32, Construction> m_vertical;
    QHash<quint32, Construction> m_horizontal;
    mutable QHash<quint32, QPainterPath> m_paths;
    mutable QHash<char32_t, quint32> m_glyphs;
};
