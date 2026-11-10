#include "exchange.h"

#include "ink.h"
#include "library.h"
#include "richdoc.h"
#include "store.h"
#include "zip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

namespace Exchange {

static bool writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

static QByteArray readFile(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    const bool opened = file.open(QIODevice::ReadOnly);
    if (ok) {
        *ok = opened;
    }
    return opened ? file.readAll() : QByteArray();
}

QString fileName(const QString &title)
{
    QString name = title.simplified();
    static const QRegularExpression unsafe(QStringLiteral("[\\x00-\\x1f/\\\\:*?\"<>|]"));
    name.replace(unsafe, QStringLiteral("-"));
    while (name.startsWith(QLatin1Char('.'))) {
        name.remove(0, 1);
    }
    return name.isEmpty() ? QCoreApplication::translate("Exchange", "Sin título") : name.left(80);
}

QString formatOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) return QStringLiteral("md");
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) return QStringLiteral("html");
    if (suffix == QLatin1String("txt") || suffix == QLatin1String("text")) return QStringLiteral("txt");
    if (suffix == QLatin1String("pdf")) return QStringLiteral("pdf");
    return {};
}

// --- Reading ------------------------------------------------------------------------

/// The first block is the title when it is the only top heading the page starts with.
static Read titled(QTextDocument &document)
{
    Read read;
    read.ok = true;
    const QTextBlock first = document.begin();
    const bool heads = first.isValid() && first.blockFormat().headingLevel() == 1 && !first.text().trimmed().isEmpty();
    read.doc = RichDoc::read(&document, heads ? 1 : 0);
    if (heads && !read.doc.blocks.isEmpty()) {
        read.title = first.text().simplified();
        read.doc.blocks.removeFirst();
    }
    // What a reader leaves at the ends: empty paragraphs.
    auto blank = [](const Block &block) { return block.type == Block::Paragraph && block.runs.isEmpty(); };
    while (!read.doc.blocks.isEmpty() && blank(read.doc.blocks.first())) {
        read.doc.blocks.removeFirst();
    }
    while (!read.doc.blocks.isEmpty() && blank(read.doc.blocks.last())) {
        read.doc.blocks.removeLast();
    }
    // The space HTML leaves at the end of a paragraph.
    for (Block &block : read.doc.blocks) {
        if (block.type != Block::Code && block.type != Block::Table && !block.runs.isEmpty() && !block.runs.last().isImage()) {
            QString &tail = block.runs.last().text;
            while (tail.endsWith(QLatin1Char(' ')) || tail.endsWith(QLatin1Char('\n'))) {
                tail.chop(1);
            }
            if (tail.isEmpty()) {
                block.runs.removeLast();
            }
        }
    }
    // And after every table, where Qt always keeps one.
    for (int i = read.doc.blocks.size() - 2; i > 0; --i) {
        if (blank(read.doc.blocks.at(i)) && read.doc.blocks.at(i - 1).type == Block::Table && read.doc.blocks.at(i + 1).type != Block::Table) {
            read.doc.blocks.removeAt(i);
        }
    }
    read.doc.linkWikiText();
    return read;
}

Read fromMarkdown(const QString &markdown)
{
    QString text = markdown;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    // Front matter is for other programs.
    static const QRegularExpression front(QStringLiteral("\\A---\\n.*?\\n---\\n"), QRegularExpression::DotMatchesEverythingOption);
    text.remove(front);
    // Qt's reader has no underline: <u> would come out as it is written.
    static const QRegularExpression underline(QStringLiteral("</?u>"));
    text.remove(underline);
    // Nor pictures without a description: it drops them.
    static const QRegularExpression bare(QStringLiteral("!\\[\\]\\("));
    text.replace(bare, QStringLiteral("![imagen]("));
    QTextDocument document;
    document.setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    Read read = titled(document);
    // Markdown has no table without a heading row, and Qt reads that row as
    // bold: a table of ours would come back with bold it never had.
    for (Block &block : read.doc.blocks) {
        if (block.type == Block::Table && !block.rows.isEmpty()) {
            for (Runs &cell : block.rows.first()) {
                for (Run &run : cell) {
                    run.bold = false;
                }
            }
        }
    }
    return read;
}

Read fromHtml(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    Read read = titled(document);
    if (read.title.isEmpty()) {
        read.title = document.metaInformation(QTextDocument::DocumentTitle).simplified();
    }
    return read;
}

Read fromText(const QString &text)
{
    Read read;
    read.ok = true;
    read.doc = Doc::fromText(text);
    while (!read.doc.blocks.isEmpty() && read.doc.blocks.first().runs.isEmpty()) {
        read.doc.blocks.removeFirst();
    }
    if (!read.doc.blocks.isEmpty()) {
        read.title = read.doc.blocks.takeFirst().runs.first().text.simplified().left(120);
    }
    while (!read.doc.blocks.isEmpty() && read.doc.blocks.first().runs.isEmpty()) {
        read.doc.blocks.removeFirst();
    }
    read.doc.linkWikiText();
    return read;
}

static QString decoded(const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xfeff))) {
        text.remove(0, 1);
    }
    return text;
}

QString importFile(Library *library, const QString &path, const QString &section, const QString &parent)
{
    const QString format = formatOf(path);
    bool opened = false;
    const QByteArray bytes = readFile(path, &opened);
    if (!opened || format.isEmpty() || format == QLatin1String("pdf") || bytes.size() > 32 * 1024 * 1024) {
        return {};
    }
    Read read = format == QLatin1String("md") ? fromMarkdown(decoded(bytes)) : format == QLatin1String("html") ? fromHtml(decoded(bytes)) : fromText(decoded(bytes));
    const QFileInfo info(path);
    if (read.title.isEmpty()) {
        read.title = info.completeBaseName();
    }
    const QString id = library->importPage(section, parent, read.title, Doc());
    if (id.isEmpty()) {
        return {};
    }
    // Pictures and files beside the document come into the page; the rest of
    // the world stays where it is.
    const QDir base = info.absoluteDir();
    read.doc.mapPaths([&](const QString &target, bool picture) {
        if (!Doc::wikiTitle(target).isEmpty()) {
            return target;
        }
        const QUrl url(target);
        const bool remote = !url.scheme().isEmpty() && url.scheme() != QLatin1String("file");
        if (remote) {
            return picture ? QString() : target;
        }
        QString local = url.isLocalFile() ? url.toLocalFile() : QUrl::fromPercentEncoding(target.toUtf8());
        const int hash = local.indexOf(QLatin1Char('#'));
        if (hash >= 0) {
            local.truncate(hash);
        }
        if (local.isEmpty()) {
            return picture ? QString() : target;
        }
        const QFileInfo file(base.absoluteFilePath(local));
        if (!file.isFile()) {
            return picture ? QString() : target;
        }
        const QString kept = library->addFile(id, file.absoluteFilePath());
        return kept.isEmpty() && !picture ? target : kept;
    });
    // The size a picture has, for those the document did not say.
    for (Block &block : read.doc.blocks) {
        for (Run &run : block.runs) {
            if (run.isImage() && run.width <= 0) {
                const QSize size = QImageReader(library->pageDir(id) + QLatin1Char('/') + run.image).size();
                if (size.isValid()) {
                    const qreal scale = qMin(1.0, 640.0 / size.width());
                    run.width = qRound(size.width() * scale);
                    run.height = qRound(size.height() * scale);
                }
            }
        }
    }
    library->setDoc(id, read.doc);
    return id;
}

QString importFolder(Library *library, const QString &dir, const QString &notebook, int *count)
{
    const QDir folder(dir);
    const QStringList files = folder.entryList({QStringLiteral("*.md"), QStringLiteral("*.markdown")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    if (count) {
        *count = 0;
    }
    if (!folder.exists() || files.isEmpty()) {
        return {};
    }
    const QString section = library->addSection(notebook, folder.dirName());
    if (section.isEmpty()) {
        return {};
    }
    for (const QString &file : files) {
        if (!importFile(library, folder.filePath(file), section).isEmpty() && count) {
            ++*count;
        }
    }
    return section;
}

// --- Writing ------------------------------------------------------------------------

/// Copies what the page uses beside the exported file and points the page at the copies.
static Doc withFilesBeside(const Library *library, const QString &id, const Doc &source, const QString &path)
{
    Doc doc = source;
    const QStringList files = doc.files();
    if (files.isEmpty()) {
        return doc;
    }
    const QFileInfo info(path);
    const QString folder = info.completeBaseName() + QStringLiteral("_archivos");
    const QString target = info.absolutePath() + QLatin1Char('/') + folder;
    QDir().mkpath(target);
    doc.mapPaths([&](const QString &file, bool) {
        if (!file.startsWith(QLatin1String("files/"))) {
            return file;
        }
        const QString name = file.mid(6);
        const QString to = target + QLatin1Char('/') + name;
        QFile::remove(to);
        QFile::copy(library->pageDir(id) + QLatin1Char('/') + file, to);
        return folder + QLatin1Char('/') + name;
    });
    return doc;
}

static QImage loadPicture(const QString &path, const QSize &wanted)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (wanted.isValid() && reader.format() == "svg") {
        // Vectors are drawn at the size they are printed, and then some.
        reader.setScaledSize(wanted * 3);
    }
    return reader.read();
}

void compose(const Library *library, const QString &id, QTextDocument *document)
{
    const PageInfo info = library->info(id);
    Doc doc = library->doc(id);
    QFont font(QStringLiteral("sans-serif"));
    font.setPixelSize(15);
    document->setDefaultFont(font);
    document->setDocumentMargin(0);
    RichDoc::write(doc, document, Style::of(false));
    for (const QString &file : doc.files()) {
        QSize wanted;
        for (const Block &block : std::as_const(doc.blocks)) {
            for (const Run &run : block.runs) {
                if (run.image == file && run.width > 0) {
                    wanted = QSize(run.width, run.height);
                }
            }
        }
        const QImage image = loadPicture(library->pageDir(id) + QLatin1Char('/') + file, wanted);
        if (!image.isNull()) {
            document->addResource(QTextDocument::ImageResource, QUrl(file), image);
        }
    }
    // The title above everything, as the window shows it.
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::Start);
    cursor.insertBlock();
    cursor.movePosition(QTextCursor::Start);
    QTextBlockFormat format;
    format.setBottomMargin(10);
    format.setHeadingLevel(1);
    cursor.setBlockFormat(format);
    QTextCharFormat look;
    look.setProperty(QTextFormat::FontPixelSize, 30);
    look.setFontWeight(QFont::Bold);
    cursor.setBlockCharFormat(look);
    cursor.insertText(info.title.isEmpty() ? QCoreApplication::translate("Exchange", "Sin título") : info.title, look);
    cursor.endEditBlock();
}

static bool writePdf(const Library *library, const QStringList &ids, const QString &path, const QString &title)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString part = path + QStringLiteral(".part");
    {
        QPdfWriter writer(part);
        writer.setTitle(title);
        writer.setCreator(QStringLiteral("Notas"));
        writer.setResolution(96);
        writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait, QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter));
        QPainter painter;
        if (!painter.begin(&writer)) {
            QFile::remove(part);
            return false;
        }
        const QSizeF sheet = writer.pageLayout().paintRectPixels(96).size();
        bool first = true;
        for (const QString &id : ids) {
            QTextDocument document;
            compose(library, id, &document);
            document.setPageSize(sheet);
            const int sheets = qMax(1, document.pageCount());
            for (int i = 0; i < sheets; ++i) {
                if (!first) {
                    writer.newPage();
                }
                first = false;
                painter.save();
                painter.translate(0, -i * sheet.height());
                document.drawContents(&painter, QRectF(0, i * sheet.height(), sheet.width(), sheet.height()));
                painter.restore();
            }
        }
        painter.end();
    }
    QFile::remove(path);
    return QFile::rename(part, path);
}

bool exportPage(const Library *library, const QString &id, const QString &path, const QString &format)
{
    if (!library->hasPage(id)) {
        return false;
    }
    const QString kind = format.isEmpty() ? formatOf(path) : format;
    const PageInfo info = library->info(id);
    const QString title = info.title.isEmpty() ? QCoreApplication::translate("Exchange", "Sin título") : info.title;
    const Doc doc = library->doc(id);
    if (kind == QLatin1String("pdf")) {
        return writePdf(library, {id}, path, title);
    }
    if (kind == QLatin1String("txt")) {
        return writeFile(path, (title + QStringLiteral("\n\n") + doc.toText() + QLatin1Char('\n')).toUtf8());
    }
    if (kind == QLatin1String("md")) {
        const Doc beside = withFilesBeside(library, id, doc, path);
        QString heading = title;
        heading.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return writeFile(path, (QStringLiteral("# ") + heading + QStringLiteral("\n\n") + beside.toMarkdown(1)).toUtf8());
    }
    if (kind == QLatin1String("html")) {
        return writeFile(path, withFilesBeside(library, id, doc, path).toHtml(title).toUtf8());
    }
    return false;
}

int exportSection(const Library *library, const QString &section, const QString &path, const QString &format)
{
    const QStringList ids = library->pageIds(section);
    if (ids.isEmpty()) {
        return 0;
    }
    if (format == QLatin1String("pdf")) {
        return writePdf(library, ids, path, library->section(section).value(QStringLiteral("name")).toString()) ? int(ids.size()) : 0;
    }
    if (format != QLatin1String("md") && format != QLatin1String("html") && format != QLatin1String("txt")) {
        return 0;
    }
    if (!QDir().mkpath(path)) {
        return 0;
    }
    int written = 0;
    QStringList used;
    for (const QString &id : ids) {
        // Two pages of the same title are two files.
        const QString base = fileName(library->info(id).title);
        QString name = base;
        for (int n = 2; used.contains(name.toLower()); ++n) {
            name = base + QLatin1Char(' ') + QString::number(n);
        }
        used << name.toLower();
        if (exportPage(library, id, path + QLatin1Char('/') + name + QLatin1Char('.') + format, format)) {
            ++written;
        }
    }
    return written;
}

// --- Notebooks ------------------------------------------------------------------------

static const QString archiveKind = QStringLiteral("wonderland-notas");

bool exportNotebook(const Library *library, const QString &notebook, const QString &path)
{
    QVariantMap found;
    for (const QVariant &each : library->notebooks()) {
        if (each.toMap().value(QStringLiteral("id")).toString() == notebook) {
            found = each.toMap();
        }
    }
    if (found.isEmpty()) {
        return false;
    }
    ZipWriter zip;
    QJsonArray sections;
    QStringList ids;
    for (const QVariant &each : found.value(QStringLiteral("sections")).toList()) {
        const QVariantMap section = each.toMap();
        const QString id = section.value(QStringLiteral("id")).toString();
        sections << QJsonObject{{QLatin1String("id"), id}, {QLatin1String("name"), section.value(QStringLiteral("name")).toString()},
                                {QLatin1String("color"), section.value(QStringLiteral("color")).toString()}};
        ids << library->pageIds(id);
    }
    QJsonArray pages;
    for (const QString &id : std::as_const(ids)) {
        pages << id;
    }
    const QJsonObject manifest{{QLatin1String("format"), archiveKind}, {QLatin1String("v"), 1}, {QLatin1String("name"), found.value(QStringLiteral("name")).toString()},
                               {QLatin1String("sections"), sections}, {QLatin1String("pages"), pages}};
    // First, and stored: what says the kind of file this is.
    zip.add(QStringLiteral("mimetype"), QByteArrayLiteral("application/x-wonderland-notas"));
    zip.add(QStringLiteral("notebook.json"), QJsonDocument(manifest).toJson(QJsonDocument::Indented));
    for (const QString &id : std::as_const(ids)) {
        const QString dir = library->pageDir(id);
        const QString inside = QStringLiteral("pages/") + id + QLatin1Char('/');
        bool opened = false;
        const QByteArray page = readFile(dir + QStringLiteral("/page.json"), &opened);
        if (!opened) {
            return false;
        }
        zip.add(inside + QStringLiteral("page.json"), page);
        // Only what the page still uses: the rest is what was deleted from it.
        for (const QString &file : library->doc(id).files()) {
            const QByteArray data = readFile(dir + QLatin1Char('/') + file, &opened);
            if (opened) {
                zip.add(inside + file, data);
            }
        }
    }
    return zip.save(path);
}

QString importNotebook(Library *library, const QString &path, int *count)
{
    if (count) {
        *count = 0;
    }
    const ZipReader zip = ZipReader::open(path);
    bool ok = false;
    const QJsonObject manifest = QJsonDocument::fromJson(zip.file(QStringLiteral("notebook.json"), &ok)).object();
    if (!zip.valid() || !ok || manifest.value(QLatin1String("format")).toString() != archiveKind || manifest.value(QLatin1String("v")).toInt() > 1) {
        return {};
    }
    QString name = manifest.value(QLatin1String("name")).toString();
    const QString notebook = library->addNotebook(name);
    QHash<QString, QString> sections;
    for (const QJsonValue &value : manifest.value(QLatin1String("sections")).toArray()) {
        const QJsonObject o = value.toObject();
        sections.insert(o.value(QLatin1String("id")).toString(),
                        library->addSection(notebook, o.value(QLatin1String("name")).toString(), o.value(QLatin1String("color")).toString()));
    }
    // Parents come before their subpages in the list; a second pass hangs
    // those that did not.
    QHash<QString, QString> pages;
    QList<QPair<QString, QString>> orphans;
    for (const QJsonValue &value : manifest.value(QLatin1String("pages")).toArray()) {
        const QString old = value.toString();
        static const QRegularExpression safe(QStringLiteral("^[0-9a-zA-Z_-]{1,64}$"));
        if (!safe.match(old).hasMatch()) {
            continue;
        }
        const QString inside = QStringLiteral("pages/") + old + QLatin1Char('/');
        const QJsonObject o = QJsonDocument::fromJson(zip.file(inside + QStringLiteral("page.json"), &ok)).object();
        const QString section = sections.value(o.value(QLatin1String("section")).toString());
        if (!ok || o.isEmpty() || section.isEmpty()) {
            continue;
        }
        Doc doc = Doc::fromJson(o.value(QLatin1String("doc")).toObject());
        QStringList tags;
        for (const QJsonValue &tag : o.value(QLatin1String("tags")).toArray()) {
            tags << tag.toString();
        }
        const QString parent = o.value(QLatin1String("parent")).toString();
        const QString id = library->importPage(section, pages.value(parent), o.value(QLatin1String("title")).toString(), Doc(), Library::parseTags(tags.join(QLatin1Char(','))),
                                               QDateTime::fromString(o.value(QLatin1String("created")).toString(), Qt::ISODate),
                                               QDateTime::fromString(o.value(QLatin1String("modified")).toString(), Qt::ISODate), o.value(QLatin1String("pinned")).toBool());
        if (id.isEmpty()) {
            continue;
        }
        pages.insert(old, id);
        if (!parent.isEmpty() && !pages.contains(parent)) {
            orphans << qMakePair(id, parent);
        }
        doc.mapPaths([&](const QString &file, bool picture) {
            if (!file.startsWith(QLatin1String("files/"))) {
                return file;
            }
            bool there = false;
            const QByteArray data = ZipReader::safeName(inside + file) ? zip.file(inside + file, &there) : QByteArray();
            const QString kept = there ? library->addFile(id, file.mid(6), data) : QString();
            return kept.isEmpty() && !picture ? file : kept;
        });
        library->setDoc(id, doc, true);
        if (count) {
            ++*count;
        }
    }
    for (const auto &orphan : std::as_const(orphans)) {
        library->setParent(orphan.first, pages.value(orphan.second));
    }
    return notebook;
}

}
