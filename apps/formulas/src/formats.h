#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/// A document: a numbered list of equations and how they are drawn.
struct FormulaDoc {
    /// What its versions and its recovery copy are filed under; made when first needed.
    QString uid;
    QString title;
    QStringList equations;
    /// Empty: the colour of the text around it (black on paper).
    QString color;
    /// The size of the letters, in points.
    qreal size = 20;
    /// The math font; empty for the best one installed.
    QString font;

    bool operator==(const FormulaDoc &other) const = default;
    QJsonObject toJson() const;
    static FormulaDoc fromJson(const QJsonObject &object);
};

namespace Formats {

/// By the extension: wform, tex, odf, mml. Unknown ones are read as LaTeX.
QString kindOf(const QString &path);

/// False with a reason in `error` (in Spanish, for the window) when it cannot be read.
bool read(const QString &path, FormulaDoc *doc, QString *error);
/// Writes by the extension: wform, tex, odf, mml, svg, png, pdf.
bool write(const QString &path, const FormulaDoc &doc, QString *error, int current = 0, qreal scale = 2);

QByteArray toWform(const FormulaDoc &doc);
bool fromWform(const QByteArray &bytes, FormulaDoc *doc, QString *error);

/// A fragment: each equation between `\[` and `\]`.
QString toTex(const FormulaDoc &doc);
FormulaDoc fromTex(const QString &text);

/// Presentation MathML of one formula, with the LaTeX as an annotation.
QString toMathML(const QString &latex, bool annotate = true);
/// The LaTeX of a MathML document; several when it is a column of rows.
QStringList fromMathML(const QString &xml, QString *error = nullptr);
/// The formula as LibreOffice Math writes it.
QString toStarMath(const QString &latex);

/// An OpenDocument formula: the equations one under another.
QByteArray toOdf(const FormulaDoc &doc);
bool fromOdf(const QByteArray &bytes, FormulaDoc *doc, QString *error);

QByteArray toSvg(const FormulaDoc &doc, int index);
QImage toImage(const FormulaDoc &doc, int index, qreal scale, const QColor &background = Qt::transparent);
/// Every equation, numbered, on A4 pages.
bool toPdf(const FormulaDoc &doc, const QString &path);

/// The colour a formula is drawn with on paper.
QColor ink(const FormulaDoc &doc);
/// Points to the pixels the layout works in (at 96 to the inch).
qreal pixels(qreal points);

}
