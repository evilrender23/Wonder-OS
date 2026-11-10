#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

/// A stretch of text with one look, or a picture.
struct Run {
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    /// Inline code.
    bool mono = false;
    /// "#rrggbb", or empty for the colour of the page.
    QString color;
    QString highlight;
    /// A web address, "notas:page/<title>" for another page, or a file under "files/".
    QString href;
    /// A picture instead of text: a path under "files/".
    QString image;
    int width = 0;
    int height = 0;

    bool isImage() const { return !image.isEmpty(); }
    bool sameLook(const Run &other) const;
    bool operator==(const Run &other) const = default;
};
using Runs = QList<Run>;

/// A paragraph of the page, of one kind; or a table.
struct Block {
    enum Type { Paragraph, Heading1, Heading2, Heading3, Bullet, Number, Task, Quote, Code, Table };
    Type type = Paragraph;
    /// A task that is ticked.
    bool done = false;
    /// How deep a list item is nested.
    int indent = 0;
    Runs runs;
    /// A table: rows of cells.
    QList<QList<Runs>> rows;

    bool isList() const { return type == Bullet || type == Number || type == Task; }
    bool operator==(const Block &other) const = default;
    static QString typeName(Type type);
    static Type typeFromName(const QString &name);
};

/// What a page says, apart from how any toolkit draws it. This is what is
/// kept on disk, and what every export starts from.
struct Doc {
    QList<Block> blocks;

    bool operator==(const Doc &other) const = default;
    bool isEmpty() const;

    QJsonObject toJson() const;
    static Doc fromJson(const QJsonObject &json);

    /// Lines of text; lists with their bullets, tables with tabs.
    QString toText() const;
    static Doc fromText(const QString &text);
    /// GitHub-flavoured Markdown. Colours are dropped; underline goes as <u>.
    /// `shift` pushes the headings down: with 1, «Encabezado» is «##».
    QString toMarkdown(int shift = 0) const;
    /// The body of an HTML page (no <html> around it).
    QString toHtmlBody() const;
    /// A whole HTML file, with a stylesheet.
    QString toHtml(const QString &title) const;

    /// «[[Title]]» in the text becomes a link to that page.
    void linkWikiText();
    /// Every picture and every file linked ("files/…"), once each.
    QStringList files() const;
    /// Rewrites pictures and links: gets the old path and whether it is a
    /// picture, answers the new one.
    void mapPaths(const std::function<QString(const QString &, bool)> &map);

    static QString wikiHref(const QString &title);
    /// The title a "notas:page/…" link points at, or empty.
    static QString wikiTitle(const QString &href);
};
