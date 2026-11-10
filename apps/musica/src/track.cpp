#include "track.h"

QVariantMap Track::toMap() const
{
    return {
        {QStringLiteral("path"), path},
        {QStringLiteral("title"), title},
        {QStringLiteral("artist"), artist},
        {QStringLiteral("album"), album},
        {QStringLiteral("albumArtist"), albumOwner()},
        {QStringLiteral("albumKey"), albumKey()},
        {QStringLiteral("genre"), genre},
        {QStringLiteral("year"), year},
        {QStringLiteral("number"), number},
        {QStringLiteral("disc"), disc},
        {QStringLiteral("duration"), durationMs},
    };
}

QJsonObject Track::toJson() const
{
    QJsonObject object{
        {QStringLiteral("path"), path},
        {QStringLiteral("title"), title},
        {QStringLiteral("artist"), artist},
        {QStringLiteral("album"), album},
        {QStringLiteral("duration"), durationMs},
        {QStringLiteral("modified"), modified},
        {QStringLiteral("size"), size},
    };
    if (!albumArtist.isEmpty()) {
        object.insert(QStringLiteral("albumArtist"), albumArtist);
    }
    if (!genre.isEmpty()) {
        object.insert(QStringLiteral("genre"), genre);
    }
    if (year > 0) {
        object.insert(QStringLiteral("year"), year);
    }
    if (number > 0) {
        object.insert(QStringLiteral("number"), number);
    }
    if (disc > 0) {
        object.insert(QStringLiteral("disc"), disc);
    }
    return object;
}

Track Track::fromJson(const QJsonObject &object)
{
    Track track;
    track.path = object.value(QLatin1String("path")).toString();
    track.title = object.value(QLatin1String("title")).toString();
    track.artist = object.value(QLatin1String("artist")).toString();
    track.album = object.value(QLatin1String("album")).toString();
    track.albumArtist = object.value(QLatin1String("albumArtist")).toString();
    track.genre = object.value(QLatin1String("genre")).toString();
    track.year = object.value(QLatin1String("year")).toInt();
    track.number = object.value(QLatin1String("number")).toInt();
    track.disc = object.value(QLatin1String("disc")).toInt();
    track.durationMs = qint64(object.value(QLatin1String("duration")).toDouble());
    track.modified = qint64(object.value(QLatin1String("modified")).toDouble());
    track.size = qint64(object.value(QLatin1String("size")).toDouble());
    return track;
}

QString folded(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}
