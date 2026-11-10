#pragma once

#include "model.h"

class QTextDocument;

/// Paragraphs of runs to and from a QTextDocument, which lays them out for the
/// painter, and to and from the HTML the editor on the canvas works with.
namespace RichText {

/// Lays the paragraphs out in a document `width` points wide (0 for no wrapping).
void fill(QTextDocument &document, const QList<Model::Paragraph> &paragraphs, const Model::TextDefaults &defaults,
          const Model::Look &look, double width);
QString toHtml(const QList<Model::Paragraph> &paragraphs, const Model::TextDefaults &defaults, const Model::Look &look);
/// What is formatted as the defaults say comes back unset, so that it keeps following the theme.
QList<Model::Paragraph> fromDocument(const QTextDocument &document, const Model::TextDefaults &defaults);
QList<Model::Paragraph> fromHtml(const QString &html, const Model::TextDefaults &defaults);
/// Joins neighbouring runs of one format and drops the empty ones.
QList<Model::Paragraph> tidied(const QList<Model::Paragraph> &paragraphs);

}
