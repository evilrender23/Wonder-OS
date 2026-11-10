#pragma once

#include "docmodel.h"

#include <QByteArray>
#include <QString>

class QTextDocument;

/// Word documents (.docx, WordprocessingML in a ZIP), to and from a
/// QTextDocument: paragraphs and their styles, formatted runs, lists, tables,
/// inline pictures, links, page breaks, the page and its header and footer.
namespace Docx {

QByteArray toBytes(const QTextDocument *document, const Doc::PageSetup &setup);
bool write(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error = nullptr);

/// Fills an empty, prepared document. False (and `error`) when the bytes are
/// not a Word document.
bool fromBytes(const QByteArray &bytes, QTextDocument *document, Doc::PageSetup *setup, QString *error = nullptr);
bool read(const QString &path, QTextDocument *document, Doc::PageSetup *setup, QString *error = nullptr);

} // namespace Docx
