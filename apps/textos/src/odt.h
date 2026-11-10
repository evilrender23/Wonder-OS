#pragma once

#include "docmodel.h"

#include <QByteArray>
#include <QString>

class QTextDocument;

/// OpenDocument text (.odt), to and from a QTextDocument: the same things
/// Docx carries, in the vocabulary of ODF 1.2.
namespace Odt {

QByteArray toBytes(const QTextDocument *document, const Doc::PageSetup &setup);
bool write(const QString &path, const QTextDocument *document, const Doc::PageSetup &setup, QString *error = nullptr);

/// Fills an empty, prepared document. False (and `error`) when the bytes are
/// not an OpenDocument text.
bool fromBytes(const QByteArray &bytes, QTextDocument *document, Doc::PageSetup *setup, QString *error = nullptr);
bool read(const QString &path, QTextDocument *document, Doc::PageSetup *setup, QString *error = nullptr);

} // namespace Odt
