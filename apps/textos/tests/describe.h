#pragma once

#include <QFileInfo>
#include "docmodel.h"

#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTextBlock>
#include <QTextList>
#include <QTextTable>

/// A document as lines of text, one per paragraph, with everything a file
/// format is expected to keep: what two documents must agree on to be the same.
inline QStringList describe(const QTextDocument *document, bool withFonts = true)
{
    QStringList lines;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const QTextBlockFormat format = block.blockFormat();
        const Qt::Alignment alignment = format.alignment();
        QString line = Doc::styleOf(block);
        line += alignment & Qt::AlignHCenter ? QStringLiteral(" center") : alignment & Qt::AlignRight ? QStringLiteral(" right")
            : alignment & Qt::AlignJustify ? QStringLiteral(" justify") : QStringLiteral(" left");
        if (const QTextList *list = block.textList()) {
            const QTextListFormat::Style style = list->format().style();
            const bool bullets = style == QTextListFormat::ListDisc || style == QTextListFormat::ListCircle || style == QTextListFormat::ListSquare;
            line += QStringLiteral(" %1%2#%3").arg(bullets ? QStringLiteral("bullet") : QStringLiteral("number")).arg(list->format().indent()).arg(list->itemNumber(block) + 1);
        }
        QTextCursor at(block);
        if (const QTextTable *table = at.currentTable()) {
            const QTextTableCell cell = table->cellAt(at);
            line += QStringLiteral(" cell%1,%2of%3x%4").arg(cell.row()).arg(cell.column()).arg(table->rows()).arg(table->columns());
        }
        if (format.pageBreakPolicy() & QTextFormat::PageBreak_AlwaysBefore) {
            line += QStringLiteral(" break");
        }
        line += QStringLiteral(" |");
        QString text;
        Doc::Look last;
        QString lastHref;
        bool open = false;
        const auto close = [&] {
            if (!open) {
                return;
            }
            QString flags;
            flags += last.bold ? QLatin1Char('b') : QLatin1Char('-');
            flags += last.italic ? QLatin1Char('i') : QLatin1Char('-');
            flags += last.underline ? QLatin1Char('u') : QLatin1Char('-');
            flags += last.strike ? QLatin1Char('s') : QLatin1Char('-');
            flags += QString::number(last.vertical);
            line += QStringLiteral(" [%1 %2 %3%4%5%6]").arg(text, flags, last.color.name(),
                last.background.isValid() ? QStringLiteral(" bg") + last.background.name() : QString(),
                withFonts ? QStringLiteral(" %1 %2").arg(last.family).arg(last.size) : QString(),
                lastHref.isEmpty() ? QString() : QStringLiteral(" ->") + lastHref);
            text.clear();
            open = false;
        };
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat chars = fragment.charFormat();
            if (chars.isImageFormat()) {
                close();
                const QTextImageFormat image = chars.toImageFormat();
                const QImage stored = Doc::image(document, image.name());
                line += QStringLiteral(" [image %1x%2 of %3x%4]").arg(qRound(image.width())).arg(qRound(image.height())).arg(stored.width()).arg(stored.height());
                continue;
            }
            Doc::Look look = Doc::lookOf(chars);
            const QString href = chars.isAnchor() ? chars.anchorHref() : QString();
            if (open && (!(look == last) || href != lastHref)) {
                close();
            }
            last = look;
            lastHref = href;
            open = true;
            text += fragment.text();
        }
        close();
        lines << line;
    }
    return lines;
}

/// Runs LibreOffice on a file with a profile of its own; the converted file's path, or empty.
inline QString sofficeConvert(const QString &file, const QString &to, const QString &outDir)
{
    const QString soffice = QStandardPaths::findExecutable(QStringLiteral("soffice"));
    if (soffice.isEmpty()) {
        return QString();
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(soffice, {QStringLiteral("-env:UserInstallation=file://") + outDir + QStringLiteral("/profile"), QStringLiteral("--headless"),
                            QStringLiteral("--convert-to"), to, QStringLiteral("--outdir"), outDir, file});
    if (!process.waitForFinished(180000)) {
        process.kill();
        process.waitForFinished();
        return QString();
    }
    const QString out = outDir + QLatin1Char('/') + QFileInfo(file).completeBaseName() + QLatin1Char('.') + to.section(QLatin1Char(':'), 0, 0);
    return QFileInfo::exists(out) ? out : QString();
}

#define COMPARE_LINES(actual, expected) \
    do { \
        const QStringList a_ = (actual); \
        const QStringList e_ = (expected); \
        for (int i_ = 0; i_ < qMin(a_.size(), e_.size()); ++i_) { \
            QCOMPARE(a_.at(i_), e_.at(i_)); \
        } \
        QCOMPARE(a_.size(), e_.size()); \
    } while (false)
