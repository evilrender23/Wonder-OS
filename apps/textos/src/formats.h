#pragma once

#include "docmodel.h"

#include <QAbstractTextDocumentLayout>
#include <QByteArray>
#include <QString>
#include <QStringList>

class QPainter;
class QTextDocument;

/// The files Textos reads and writes, told apart by their extension, and the
/// drawing of a page that the window, the PDF and the printer share.
namespace Formats {

enum class Kind { Unknown, Docx, Odt, Text, Markdown, Html, Rtf, Pdf };

Kind kindOf(const QString &path);
/// Whether a document saved as this kind comes back as it was (plain text and Markdown do not).
bool keepsFormatting(Kind kind);

/// Fills an empty document from a file. The document comes out prepared.
bool load(const QString &path, QTextDocument *document, Doc::PageSetup *setup, QString *error = nullptr);
/// Writes a document in the format its extension names.
bool save(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error = nullptr);

void fromPlainText(const QString &text, QTextDocument *document);
QString toPlainText(const QTextDocument *document);
/// `folder` is where relative pictures are looked for.
void fromMarkdown(const QString &text, QTextDocument *document, const QString &folder = QString());
QString toMarkdown(const QTextDocument *document);
void fromHtml(const QString &html, QTextDocument *document, const QString &folder = QString());
/// A whole page of HTML, with its pictures inside it.
QString toHtml(const QTextDocument *document);
bool fromRtf(const QByteArray &rtf, QTextDocument *document, Doc::PageSetup *setup);
QByteArray toRtf(const QTextDocument *document, const Doc::PageSetup &setup);

/// The pages of a document laid out for its setup (Editor keeps it that way).
int pageCount(const QTextDocument *document);
/// Draws page `page` (from 0) with its header and footer, in layout pixels
/// from the corner of the sheet. The paper itself is the caller's.
void paintPage(QPainter *painter, const QTextDocument *document, const Doc::PageSetup &setup, int page, int pages,
               const QAbstractTextDocumentLayout::PaintContext *context = nullptr);
bool writePdf(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error = nullptr);

} // namespace Formats
