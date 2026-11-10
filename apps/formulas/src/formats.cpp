#include "formats.h"

#include "layout.h"
#include "zip.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>

namespace {

const QLatin1String wformMark("wonderland-formula");

LayoutOptions options(const FormulaDoc &doc)
{
    LayoutOptions out;
    out.family = doc.font;
    out.size = Formats::pixels(doc.size);
    return out;
}

/// The text without its comments: a `%` that no backslash escapes, to the end of its line.
QString uncommented(const QString &text)
{
    QString out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        int cut = -1;
        for (int i = 0; i < line.size(); ++i) {
            if (line.at(i) == QLatin1Char('\\')) {
                ++i;
            } else if (line.at(i) == QLatin1Char('%')) {
                cut = i;
                break;
            }
        }
        out += (cut < 0 ? line : line.left(cut)) + QLatin1Char('\n');
    }
    return out;
}

bool writeBytes(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("No se pudo escribir «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    return true;
}

}

QJsonObject FormulaDoc::toJson() const
{
    QJsonArray list;
    for (const QString &latex : equations) {
        list.append(QJsonObject{{QStringLiteral("latex"), latex}});
    }
    QJsonObject out{
        {QStringLiteral("format"), wformMark},
        {QStringLiteral("version"), 1},
        {QStringLiteral("title"), title},
        {QStringLiteral("size"), size},
        {QStringLiteral("equations"), list},
    };
    if (!uid.isEmpty()) {
        out.insert(QStringLiteral("uid"), uid);
    }
    if (!color.isEmpty()) {
        out.insert(QStringLiteral("color"), color);
    }
    if (!font.isEmpty()) {
        out.insert(QStringLiteral("font"), font);
    }
    return out;
}

FormulaDoc FormulaDoc::fromJson(const QJsonObject &object)
{
    FormulaDoc doc;
    doc.uid = object.value(QLatin1String("uid")).toString();
    doc.title = object.value(QLatin1String("title")).toString();
    doc.color = object.value(QLatin1String("color")).toString();
    if (!doc.color.isEmpty() && !QColor(doc.color).isValid()) {
        doc.color.clear();
    }
    doc.size = qBound(6.0, object.value(QLatin1String("size")).toDouble(20), 144.0);
    doc.font = object.value(QLatin1String("font")).toString();
    for (const QJsonValue &value : object.value(QLatin1String("equations")).toArray()) {
        // A bare string is accepted too: easier to write by hand.
        doc.equations.append(value.isString() ? value.toString() : value.toObject().value(QLatin1String("latex")).toString());
    }
    return doc;
}

namespace Formats {

qreal pixels(qreal points)
{
    return points * 96.0 / 72.0;
}

QColor ink(const FormulaDoc &doc)
{
    const QColor color(doc.color);
    return doc.color.isEmpty() || !color.isValid() ? QColor(Qt::black) : color;
}

QString kindOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("wform") || suffix == QLatin1String("odf") || suffix == QLatin1String("svg") || suffix == QLatin1String("png")
        || suffix == QLatin1String("pdf")) {
        return suffix;
    }
    if (suffix == QLatin1String("mml") || suffix == QLatin1String("mathml") || suffix == QLatin1String("xml")) {
        return QStringLiteral("mml");
    }
    return QStringLiteral("tex");
}

QByteArray toWform(const FormulaDoc &doc)
{
    return QJsonDocument(doc.toJson()).toJson(QJsonDocument::Indented);
}

bool fromWform(const QByteArray &bytes, FormulaDoc *doc, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject() || json.object().value(QLatin1String("format")).toString() != wformMark) {
        if (error) {
            *error = QStringLiteral("No es un documento de Fórmulas.");
        }
        return false;
    }
    *doc = FormulaDoc::fromJson(json.object());
    return true;
}

QString toTex(const FormulaDoc &doc)
{
    QString out;
    if (!doc.title.isEmpty()) {
        out += QStringLiteral("% ") + doc.title + QStringLiteral("\n\n");
    }
    for (const QString &latex : doc.equations) {
        out += QStringLiteral("\\[\n") + latex.trimmed() + QStringLiteral("\n\\]\n\n");
    }
    return out.trimmed() + QLatin1Char('\n');
}

FormulaDoc fromTex(const QString &source)
{
    FormulaDoc doc;
    // The title this app writes: a first line that is only a comment.
    static const QRegularExpression titleLine(QStringLiteral("\\A\\s*%\\s*(\\S[^\\n]*)\\n"));
    const QRegularExpressionMatch title = titleLine.match(source);
    if (title.hasMatch()) {
        doc.title = title.captured(1).trimmed();
    }
    QString text = uncommented(source);
    const int begin = text.indexOf(QLatin1String("\\begin{document}"));
    if (begin >= 0) {
        const int end = text.indexOf(QLatin1String("\\end{document}"), begin);
        text = text.mid(begin + 16, end < 0 ? -1 : end - begin - 16);
    }
    // Displayed formulas, wherever they are in the text.
    static const QRegularExpression display(QStringLiteral(
        "\\\\\\[(.*?)\\\\\\]|\\$\\$(.*?)\\$\\$|\\\\begin\\{(equation|displaymath)\\*?\\}(.*?)\\\\end\\{\\3\\*?\\}"
        "|(\\\\begin\\{(align|gather|multline|eqnarray|alignat)\\*?\\}.*?\\\\end\\{\\6\\*?\\})"),
        QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = display.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QString latex = match.captured(1);
        if (latex.isNull()) latex = match.captured(2);
        if (latex.isNull()) latex = match.captured(4);
        if (latex.isNull()) latex = match.captured(5);
        latex = latex.trimmed();
        if (!latex.isEmpty()) {
            doc.equations.append(latex);
        }
    }
    if (doc.equations.isEmpty()) {
        // A bare fragment: one formula to the paragraph.
        static const QRegularExpression blank(QStringLiteral("\\n\\s*\\n"));
        for (QString part : text.split(blank, Qt::SkipEmptyParts)) {
            part = part.trimmed();
            while (part.size() >= 2 && part.startsWith(QLatin1Char('$')) && part.endsWith(QLatin1Char('$'))) {
                part = part.mid(1, part.size() - 2).trimmed();
            }
            if (!part.isEmpty()) {
                doc.equations.append(part);
            }
        }
    }
    return doc;
}

QByteArray toSvg(const FormulaDoc &doc, int index)
{
    const Layout laid = Formula::layout(doc.equations.value(index), options(doc));
    return Formula::svg(laid.box, ink(doc), qRound(pixels(doc.size) * 0.2));
}

QImage toImage(const FormulaDoc &doc, int index, qreal scale, const QColor &background)
{
    const Layout laid = Formula::layout(doc.equations.value(index), options(doc));
    return Formula::image(laid.box, ink(doc), qBound(0.25, scale, 16.0), qRound(pixels(doc.size) * 0.2), background);
}

bool toPdf(const FormulaDoc &doc, const QString &path)
{
    QPdfWriter writer(path);
    writer.setResolution(1200);
    writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(22, 22, 22, 22), QPageLayout::Millimeter));
    writer.setTitle(doc.title);
    writer.setCreator(QStringLiteral("Fórmulas"));
    QPainter painter;
    if (!painter.begin(&writer)) {
        return false;
    }
    // Work in the pixels of the layout: 96 to the inch.
    const qreal unit = writer.resolution() / 96.0;
    painter.scale(unit, unit);
    const qreal width = writer.width() / unit;
    const qreal height = writer.height() / unit;
    const QColor color = ink(doc);
    LayoutOptions label = options(doc);
    label.size = qMin(label.size, pixels(12));
    const qreal gap = pixels(doc.size) * 0.9;
    const qreal numberRoom = doc.equations.size() > 1 ? label.size * 3.2 : 0;
    qreal y = 0;
    if (!doc.title.isEmpty()) {
        QFont font(QStringLiteral("sans-serif"));
        font.setPixelSize(22);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(0x1d, 0x1d, 0x1f));
        const QRectF used = painter.boundingRect(QRectF(0, 0, width, height), Qt::AlignLeft | Qt::TextWordWrap, doc.title);
        painter.drawText(QRectF(0, 0, width, height), Qt::AlignLeft | Qt::TextWordWrap, doc.title);
        y = used.height() + gap;
    }
    for (int i = 0; i < doc.equations.size(); ++i) {
        const Layout laid = Formula::layout(doc.equations.at(i), options(doc));
        // One too wide for the page is shrunk to fit.
        const qreal room = width - 2 * numberRoom;
        const qreal fit = laid.box.width > room ? room / laid.box.width : 1;
        const qreal tall = (laid.box.height + laid.box.depth) * fit;
        if (y > 0 && y + tall > height) {
            writer.newPage();
            y = 0;
        }
        painter.save();
        painter.translate((width - laid.box.width * fit) / 2, y + laid.box.height * fit);
        painter.scale(fit, fit);
        Formula::paint(&painter, laid.box, QPointF(0, 0), color);
        painter.restore();
        if (numberRoom > 0) {
            const Layout number = Formula::layout(QStringLiteral("(%1)").arg(i + 1), label);
            const qreal axis = y + laid.box.height * fit - label.size * 0.25;
            Formula::paint(&painter, number.box, QPointF(width - number.box.width, axis + label.size * 0.25), QColor(0x55, 0x55, 0x5a));
        }
        y += tall + gap;
    }
    return painter.end();
}

bool read(const QString &path, FormulaDoc *doc, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("No se pudo abrir «%1».").arg(QFileInfo(path).fileName());
        }
        return false;
    }
    const QByteArray bytes = file.readAll();
    const QString kind = kindOf(path);
    const QString base = QFileInfo(path).completeBaseName();
    FormulaDoc out;
    bool ok = true;
    if (bytes.startsWith("PK")) {
        ok = fromOdf(bytes, &out, error);
    } else if (kind == QLatin1String("wform") || bytes.trimmed().startsWith("{\n    \"") || bytes.contains("\"wonderland-formula\"")) {
        ok = fromWform(bytes, &out, error);
    } else if (kind == QLatin1String("mml") || bytes.contains("<math")) {
        QString problem;
        out.equations = fromMathML(QString::fromUtf8(bytes), &problem);
        ok = problem.isEmpty();
        if (!ok && error) {
            *error = problem;
        }
    } else if (kind == QLatin1String("tex")) {
        out = fromTex(QString::fromUtf8(bytes));
    } else {
        ok = false;
        if (error) {
            *error = QStringLiteral("«%1» no es un formato que Fórmulas pueda leer.").arg(QFileInfo(path).fileName());
        }
    }
    if (!ok) {
        return false;
    }
    if (out.title.isEmpty()) {
        out.title = base;
    }
    if (out.equations.isEmpty()) {
        out.equations.append(QString());
    }
    *doc = out;
    return true;
}

bool write(const QString &path, const FormulaDoc &doc, QString *error, int current, qreal scale)
{
    const QString kind = kindOf(path);
    if (kind == QLatin1String("wform")) {
        return writeBytes(path, toWform(doc), error);
    }
    if (kind == QLatin1String("odf")) {
        return writeBytes(path, toOdf(doc), error);
    }
    if (kind == QLatin1String("mml")) {
        return writeBytes(path, toMathML(doc.equations.value(current)).toUtf8(), error);
    }
    if (kind == QLatin1String("svg")) {
        return writeBytes(path, toSvg(doc, current), error);
    }
    if (kind == QLatin1String("png")) {
        if (!toImage(doc, current, scale).save(path, "PNG")) {
            if (error) {
                *error = QStringLiteral("No se pudo escribir «%1».").arg(QFileInfo(path).fileName());
            }
            return false;
        }
        return true;
    }
    if (kind == QLatin1String("pdf")) {
        if (!toPdf(doc, path)) {
            if (error) {
                *error = QStringLiteral("No se pudo escribir «%1».").arg(QFileInfo(path).fileName());
            }
            return false;
        }
        return true;
    }
    return writeBytes(path, toTex(doc).toUtf8(), error);
}

}
