#include "subtitles.h"

#include "media.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStringDecoder>

namespace
{
/// «01:02:03,456», «02:03.456», «1:02:03.45» (hundredths, as SubStation writes).
qint64 stamp(QStringView text)
{
    text = text.trimmed();
    const int dot = int(qMax(text.lastIndexOf(QLatin1Char(',')), text.lastIndexOf(QLatin1Char('.'))));
    qint64 fraction = 0;
    QStringView clock = text;
    if (dot >= 0) {
        const QStringView digits = text.mid(dot + 1);
        bool ok = false;
        const int value = digits.toInt(&ok);
        if (!ok || digits.size() > 3 || digits.isEmpty()) {
            return -1;
        }
        fraction = digits.size() == 1 ? value * 100 : (digits.size() == 2 ? value * 10 : value);
        clock = text.left(dot);
    }
    const QList<QStringView> parts = clock.split(QLatin1Char(':'));
    if (parts.size() < 2 || parts.size() > 3) {
        return -1;
    }
    qint64 seconds = 0;
    for (const QStringView part : parts) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (!ok || value < 0) {
            return -1;
        }
        seconds = seconds * 60 + value;
    }
    return seconds * 1000 + fraction;
}

/// Drops what only a renderer of styles would use: <i>, <font …>, {\an8}.
QString plain(QString text)
{
    static const QRegularExpression braces(QStringLiteral("\\{[^}]*\\}"));
    static const QRegularExpression tags(QStringLiteral("</?[A-Za-z][^>]*>"));
    text.remove(braces);
    text.remove(tags);
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    QStringList lines;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            lines << trimmed;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

/// SubRip and WebVTT: blocks apart by an empty line, one of whose lines is «start --> end».
QList<Cue> parseBlocks(const QString &text)
{
    QList<Cue> cues;
    const QStringList lines = text.split(QLatin1Char('\n'));
    int i = 0;
    while (i < lines.size()) {
        const int arrow = int(lines.at(i).indexOf(QLatin1String("-->")));
        if (arrow < 0) {
            ++i;
            continue;
        }
        const QString &timing = lines.at(i);
        // After the end time WebVTT may say where to draw the line: «align:start».
        QStringView after = QStringView(timing).mid(arrow + 3).trimmed();
        const int space = int(after.indexOf(QLatin1Char(' ')));
        if (space > 0) {
            after = after.left(space);
        }
        Cue cue;
        cue.start = stamp(QStringView(timing).left(arrow));
        cue.end = stamp(after);
        ++i;
        QStringList body;
        while (i < lines.size() && !lines.at(i).trimmed().isEmpty() && !lines.at(i).contains(QLatin1String("-->"))) {
            body << lines.at(i);
            ++i;
        }
        if (i < lines.size() && lines.at(i).contains(QLatin1String("-->")) && !body.isEmpty()) {
            // No empty line before the next block: its number is not dialogue.
            bool number = false;
            body.last().trimmed().toInt(&number);
            if (number) {
                body.removeLast();
            }
        }
        cue.text = plain(body.join(QLatin1Char('\n')));
        if (cue.start >= 0 && cue.end > cue.start && !cue.text.isEmpty()) {
            cues << cue;
        }
    }
    return cues;
}

/// SubStation: «Dialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Text, with commas».
QList<Cue> parseStation(const QString &text)
{
    QList<Cue> cues;
    int fields = 10;
    int startAt = 1;
    int endAt = 2;
    bool events = false;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            events = line.compare(QLatin1String("[Events]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (events && line.startsWith(QLatin1String("Format:"), Qt::CaseInsensitive)) {
            QStringList names = line.mid(7).split(QLatin1Char(','));
            for (QString &name : names) {
                name = name.trimmed().toLower();
            }
            if (names.contains(QLatin1String("start")) && names.contains(QLatin1String("end")) && names.last() == QLatin1String("text")) {
                fields = int(names.size());
                startAt = int(names.indexOf(QLatin1String("start")));
                endAt = int(names.indexOf(QLatin1String("end")));
            }
            continue;
        }
        if (!line.startsWith(QLatin1String("Dialogue:"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString rest = line.mid(9);
        // The text is the last field and keeps its own commas.
        QStringList parts;
        int from = 0;
        for (int n = 0; n < fields - 1; ++n) {
            const int comma = int(rest.indexOf(QLatin1Char(','), from));
            if (comma < 0) {
                break;
            }
            parts << rest.mid(from, comma - from);
            from = comma + 1;
        }
        if (parts.size() != fields - 1) {
            continue;
        }
        QString body = rest.mid(from);
        body.replace(QLatin1String("\\N"), QLatin1String("\n"));
        body.replace(QLatin1String("\\n"), QLatin1String("\n"));
        body.replace(QLatin1String("\\h"), QLatin1String(" "));
        Cue cue;
        cue.start = stamp(parts.at(startAt));
        cue.end = stamp(parts.at(endAt));
        cue.text = plain(body);
        if (cue.start >= 0 && cue.end > cue.start && !cue.text.isEmpty()) {
            cues << cue;
        }
    }
    return cues;
}
}

QString Subtitles::decode(const QByteArray &data)
{
    if (data.startsWith("\xEF\xBB\xBF")) {
        return QString::fromUtf8(data.mid(3));
    }
    if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) {
        QStringDecoder utf16(QStringDecoder::Utf16);
        return utf16(data);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    if (!utf8.hasError()) {
        return text;
    }
    return QString::fromLatin1(data);
}

QList<Cue> Subtitles::parse(const QString &text, const QString &kind)
{
    QString normal = text;
    normal.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normal.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QString ending = kind.toLower();
    QList<Cue> cues = ending == QLatin1String("ass") || ending == QLatin1String("ssa") ? parseStation(normal) : parseBlocks(normal);
    std::stable_sort(cues.begin(), cues.end(), [](const Cue &a, const Cue &b) {
        return a.start < b.start;
    });
    return cues;
}

QStringList Subtitles::beside(const QString &videoPath)
{
    const QFileInfo video(videoPath);
    const QString base = video.completeBaseName();
    if (base.isEmpty()) {
        return {};
    }
    QStringList exact;
    QStringList tagged;
    const QFileInfoList entries = video.dir().entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries) {
        const QString suffix = entry.suffix().toLower();
        if (suffix != QLatin1String("srt") && suffix != QLatin1String("vtt") && suffix != QLatin1String("ass") && suffix != QLatin1String("ssa")) {
            continue;
        }
        const QString name = entry.completeBaseName();
        if (name.compare(base, Qt::CaseInsensitive) == 0) {
            exact << entry.absoluteFilePath();
        } else if (name.startsWith(base + QLatin1Char('.'), Qt::CaseInsensitive)) {
            tagged << entry.absoluteFilePath();
        }
    }
    return exact + tagged;
}

QString Subtitles::label(const QString &videoPath, const QString &subtitlePath)
{
    const QString base = QFileInfo(videoPath).completeBaseName();
    const QFileInfo file(subtitlePath);
    const QString name = file.completeBaseName();
    if (!base.isEmpty() && name.startsWith(base + QLatin1Char('.'), Qt::CaseInsensitive)) {
        // «Film.es.srt», «Film.pt-BR.srt»: the tag is a language more often than not.
        const QString tag = name.mid(base.size() + 1);
        if (tag.size() >= 2 && tag.size() <= 6) {
            const QLocale locale(tag);
            // An unknown tag comes back as «C», and so does plain «c»: neither is a language here.
            const QString language = Media::languageName(locale.language());
            if (!language.isEmpty()) {
                return language;
            }
        }
    }
    return file.fileName();
}

bool Subtitles::load(const QString &path)
{
    clear();
    QFile file(path);
    // Subtitles are small; anything huge is not one.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) {
        return false;
    }
    m_cues = parse(decode(file.readAll()), QFileInfo(path).suffix());
    for (const Cue &cue : std::as_const(m_cues)) {
        m_longest = qMax(m_longest, cue.end - cue.start);
    }
    if (m_cues.isEmpty()) {
        return false;
    }
    m_path = path;
    return true;
}

void Subtitles::clear()
{
    m_path.clear();
    m_cues.clear();
    m_longest = 0;
}

QString Subtitles::textAt(qint64 ms) const
{
    // The first cue that starts after this moment; those on screen are before it.
    auto after = std::upper_bound(m_cues.cbegin(), m_cues.cend(), ms, [](qint64 moment, const Cue &cue) {
        return moment < cue.start;
    });
    QStringList shown;
    while (after != m_cues.cbegin()) {
        --after;
        if (after->start + m_longest <= ms) {
            // Nothing that started earlier can still be on screen.
            break;
        }
        if (ms < after->end) {
            shown.prepend(after->text);
        }
    }
    return shown.join(QLatin1Char('\n'));
}
