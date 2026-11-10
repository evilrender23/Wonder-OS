#pragma once

#include "doc.h"

#include <QString>

class Library;
class QTextDocument;

/// Pages in and out of the library: Markdown, HTML, text, PDF, and whole
/// notebooks as a .wnotas archive.
namespace Exchange {

/// A page read from somewhere else.
struct Read {
    QString title;
    Doc doc;
    bool ok = false;
};

/// The title is the first «# » line when the text starts with one.
Read fromMarkdown(const QString &markdown);
Read fromHtml(const QString &html);
/// The first line is the title.
Read fromText(const QString &text);

/// "md", "html", "txt" or "pdf", from the name of a file; empty when it is none.
QString formatOf(const QString &path);

/// Writes a page. Markdown and HTML take the pictures and attachments along
/// in a directory beside the file, «<name>_archivos».
bool exportPage(const Library *library, const QString &id, const QString &path, const QString &format = QString());
/// A section: one PDF with a page after another; or, for Markdown, HTML and
/// text, a directory with a file for each page. Answers how many pages went.
int exportSection(const Library *library, const QString &section, const QString &path, const QString &format);
/// Reads a .md, .html or .txt file as a new page; answers its id, or empty.
QString importFile(Library *library, const QString &path, const QString &section, const QString &parent = QString());
/// A directory of .md files as a new section of the notebook; answers the id of the section.
QString importFolder(Library *library, const QString &dir, const QString &notebook, int *count = nullptr);

/// A notebook with its sections, pages and files, in one archive.
bool exportNotebook(const Library *library, const QString &notebook, const QString &path);
/// Adds the notebook in the archive to the library; answers its id, or empty.
QString importNotebook(Library *library, const QString &path, int *count = nullptr);

/// Fills a document with the page as it is printed: on white, with its title and its pictures.
void compose(const Library *library, const QString &id, QTextDocument *document);
/// A name a file can have, from a title.
QString fileName(const QString &title);

}
