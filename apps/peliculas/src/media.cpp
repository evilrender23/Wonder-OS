#include "media.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

namespace
{
const QSet<QString> &videoEndings()
{
    static const QSet<QString> set = {
        QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("mkv"), QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("avi"),
        QStringLiteral("wmv"),  QStringLiteral("flv"),  QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("m2v"), QStringLiteral("ts"),
        QStringLiteral("m2ts"), QStringLiteral("mts"),  QStringLiteral("3gp"), QStringLiteral("3g2"),  QStringLiteral("ogv"), QStringLiteral("ogm"),
        QStringLiteral("vob"),  QStringLiteral("divx"), QStringLiteral("asf"), QStringLiteral("rm"),   QStringLiteral("rmvb"), QStringLiteral("f4v"),
        QStringLiteral("mxf"),
    };
    return set;
}

const QSet<QString> &audioEndings()
{
    static const QSet<QString> set = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("aac"), QStringLiteral("wav"),  QStringLiteral("wma"), QStringLiteral("aiff"), QStringLiteral("aif"), QStringLiteral("mka"),
        QStringLiteral("ac3"), QStringLiteral("dts"),
    };
    return set;
}

const QSet<QString> &subtitleEndings()
{
    static const QSet<QString> set = {QStringLiteral("srt"), QStringLiteral("vtt"), QStringLiteral("ass"), QStringLiteral("ssa")};
    return set;
}

QString ending(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

QCollator collator()
{
    // Named, not the system's: under the «C» locale numbers would sort as text.
    QCollator natural{QLocale(QLocale::Spanish, QLocale::Spain)};
    natural.setNumericMode(true);
    natural.setCaseSensitivity(Qt::CaseInsensitive);
    return natural;
}

void gather(const QString &folder, int depth, QStringList &out)
{
    const QCollator natural = collator();
    QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(entries.begin(), entries.end(), [&natural](const QFileInfo &a, const QFileInfo &b) {
        return natural.compare(a.fileName(), b.fileName()) < 0;
    });
    for (const QFileInfo &entry : std::as_const(entries)) {
        if (entry.isFile() && Media::isPlayable(entry.fileName())) {
            out << entry.absoluteFilePath();
        }
    }
    if (depth <= 0) {
        return;
    }
    for (const QFileInfo &entry : std::as_const(entries)) {
        // A link to a folder could lead back here.
        if (entry.isDir() && !entry.isSymLink()) {
            gather(entry.absoluteFilePath(), depth - 1, out);
        }
    }
}
}

bool Media::isVideo(const QString &path)
{
    return videoEndings().contains(ending(path));
}

bool Media::isAudio(const QString &path)
{
    return audioEndings().contains(ending(path));
}

bool Media::isPlayable(const QString &path)
{
    return isVideo(path) || isAudio(path);
}

bool Media::isSubtitle(const QString &path)
{
    return subtitleEndings().contains(ending(path));
}

QString Media::title(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.completeBaseName();
    return name.isEmpty() ? info.fileName() : name;
}

QString Media::time(qint64 ms)
{
    const qint64 total = qMax(qint64(0), ms) / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

qint64 Media::parseTime(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.isEmpty() || parts.size() > 3) {
        return -1;
    }
    qint64 seconds = 0;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const int value = parts.at(i).toInt(&ok);
        // Only the first figure may pass 59: «90» is a minute and a half.
        if (!ok || value < 0 || (i > 0 && value > 59)) {
            return -1;
        }
        seconds = seconds * 60 + value;
    }
    return seconds * 1000;
}

void Media::sortNatural(QStringList &paths)
{
    const QCollator natural = collator();
    std::sort(paths.begin(), paths.end(), [&natural](const QString &a, const QString &b) {
        return natural.compare(a, b) < 0;
    });
}

QStringList Media::expand(const QStringList &paths)
{
    QStringList out;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            gather(info.absoluteFilePath(), 4, out);
        } else if (info.isFile()) {
            out << info.absoluteFilePath();
        }
    }
    out.removeDuplicates();
    return out;
}

QStringList Media::siblings(const QString &path)
{
    const QFileInfo info(path);
    QStringList out;
    gather(info.absolutePath(), 0, out);
    if (!out.contains(info.absoluteFilePath())) {
        // Asked for by name whatever it is called.
        out.prepend(info.absoluteFilePath());
    }
    return out;
}

QString Media::languageName(QLocale::Language language)
{
    static const QHash<QLocale::Language, QString> names = {
        {QLocale::Spanish, QStringLiteral("Español")},     {QLocale::English, QStringLiteral("Inglés")},      {QLocale::French, QStringLiteral("Francés")},
        {QLocale::German, QStringLiteral("Alemán")},       {QLocale::Italian, QStringLiteral("Italiano")},    {QLocale::Portuguese, QStringLiteral("Portugués")},
        {QLocale::Catalan, QStringLiteral("Catalán")},     {QLocale::Galician, QStringLiteral("Gallego")},    {QLocale::Basque, QStringLiteral("Euskera")},
        {QLocale::Japanese, QStringLiteral("Japonés")},    {QLocale::Chinese, QStringLiteral("Chino")},       {QLocale::Korean, QStringLiteral("Coreano")},
        {QLocale::Russian, QStringLiteral("Ruso")},        {QLocale::Arabic, QStringLiteral("Árabe")},        {QLocale::Dutch, QStringLiteral("Neerlandés")},
        {QLocale::Polish, QStringLiteral("Polaco")},       {QLocale::Swedish, QStringLiteral("Sueco")},       {QLocale::NorwegianBokmal, QStringLiteral("Noruego")},
        {QLocale::Danish, QStringLiteral("Danés")},        {QLocale::Finnish, QStringLiteral("Finés")},       {QLocale::Turkish, QStringLiteral("Turco")},
        {QLocale::Greek, QStringLiteral("Griego")},        {QLocale::Hebrew, QStringLiteral("Hebreo")},       {QLocale::Hindi, QStringLiteral("Hindi")},
        {QLocale::Czech, QStringLiteral("Checo")},         {QLocale::Hungarian, QStringLiteral("Húngaro")},   {QLocale::Romanian, QStringLiteral("Rumano")},
        {QLocale::Ukrainian, QStringLiteral("Ucraniano")}, {QLocale::Thai, QStringLiteral("Tailandés")},      {QLocale::Vietnamese, QStringLiteral("Vietnamita")},
        {QLocale::Indonesian, QStringLiteral("Indonesio")}, {QLocale::Latin, QStringLiteral("Latín")},
    };
    if (language == QLocale::AnyLanguage || language == QLocale::C) {
        return {};
    }
    const auto known = names.constFind(language);
    if (known != names.constEnd()) {
        return known.value();
    }
    // The rest in their own words: «Íslenska».
    QString own = QLocale(language).nativeLanguageName();
    if (own.isEmpty()) {
        own = QLocale::languageToString(language);
    }
    if (!own.isEmpty()) {
        own[0] = own.at(0).toUpper();
    }
    return own;
}
