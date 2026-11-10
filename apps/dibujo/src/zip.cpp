#include "zip.h"

#include <QtEndian>

#include <zlib.h>

namespace
{
/// What one archive may unpack to: a guard against a bomb.
constexpr qint64 MaxTotal = qint64(1) << 30;

void put16(QByteArray &out, quint16 value)
{
    char bytes[2];
    qToLittleEndian(value, bytes);
    out.append(bytes, 2);
}
void put32(QByteArray &out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}
quint16 get16(const QByteArray &in, qsizetype at)
{
    return qFromLittleEndian<quint16>(in.constData() + at);
}
quint32 get32(const QByteArray &in, qsizetype at)
{
    return qFromLittleEndian<quint32>(in.constData() + at);
}
bool fail(QString *error, const QString &reason)
{
    if (error) {
        *error = reason;
    }
    return false;
}
}

quint32 Zip::crc32(const QByteArray &data)
{
    return quint32(::crc32(::crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

QByteArray Zip::deflateRaw(const QByteArray &data)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    QByteArray out;
    out.resize(qsizetype(deflateBound(&stream, uLong(data.size()))));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = deflate(&stream, Z_FINISH);
    const qsizetype written = qsizetype(stream.total_out);
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        return {};
    }
    out.truncate(written);
    return out;
}

bool Zip::inflateRaw(const QByteArray &data, qint64 expected, QByteArray *out)
{
    out->clear();
    if (expected < 0 || expected > MaxTotal) {
        return false;
    }
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    out->resize(qsizetype(expected));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out->data());
    stream.avail_out = uInt(out->size());
    const int result = inflate(&stream, Z_FINISH);
    const bool whole = result == Z_STREAM_END && qint64(stream.total_out) == expected;
    inflateEnd(&stream);
    if (!whole) {
        out->clear();
    }
    return whole;
}

QByteArray Zip::write(const QList<Entry> &entries)
{
    QByteArray out;
    QByteArray directory;
    // 1 January 2026, 00:00: files are the same bytes whenever they are written.
    const quint16 dosTime = 0;
    const quint16 dosDate = quint16(((2026 - 1980) << 9) | (1 << 5) | 1);
    for (const Entry &entry : entries) {
        const QByteArray name = entry.name.toUtf8();
        QByteArray packed = entry.stored ? QByteArray() : deflateRaw(entry.data);
        // Deflate that does not shrink (a PNG, a JPEG) is stored instead.
        const bool stored = entry.stored || entry.data.isEmpty() || packed.isEmpty() || packed.size() >= entry.data.size();
        const QByteArray &body = stored ? entry.data : packed;
        const quint32 crc = crc32(entry.data);
        const quint32 offset = quint32(out.size());
        const auto header = [&](QByteArray &to) {
            put16(to, 20); // version needed
            put16(to, 0x0800); // names are UTF-8
            put16(to, stored ? 0 : 8);
            put16(to, dosTime);
            put16(to, dosDate);
            put32(to, crc);
            put32(to, quint32(body.size()));
            put32(to, quint32(entry.data.size()));
            put16(to, quint16(name.size()));
            put16(to, 0); // extra
        };
        put32(out, 0x04034b50);
        header(out);
        out.append(name);
        out.append(body);

        put32(directory, 0x02014b50);
        put16(directory, 20); // version made by
        header(directory);
        put16(directory, 0); // comment
        put16(directory, 0); // disk
        put16(directory, 0); // internal attributes
        put32(directory, 0); // external attributes
        put32(directory, offset);
        directory.append(name);
    }
    const quint32 start = quint32(out.size());
    out.append(directory);
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(directory.size()));
    put32(out, start);
    put16(out, 0);
    return out;
}

bool Zip::read(const QByteArray &zip, QList<Entry> *entries, QString *error)
{
    entries->clear();
    // The end record is the last thing in the file, bar a comment.
    qsizetype end = -1;
    for (qsizetype at = zip.size() - 22; at >= 0 && at >= zip.size() - 22 - 65535; --at) {
        if (get32(zip, at) == 0x06054b50) {
            end = at;
            break;
        }
    }
    if (end < 0) {
        return fail(error, QStringLiteral("no es un archivo ZIP"));
    }
    const int count = get16(zip, end + 10);
    const quint32 size = get32(zip, end + 12);
    const quint32 start = get32(zip, end + 16);
    if (count == 0xffff || start == 0xffffffff || qint64(start) + size > end) {
        return fail(error, QStringLiteral("ZIP64 o directorio dañado"));
    }
    qint64 total = 0;
    qsizetype at = start;
    for (int i = 0; i < count; ++i) {
        if (at + 46 > zip.size() || get32(zip, at) != 0x02014b50) {
            return fail(error, QStringLiteral("directorio dañado"));
        }
        const quint16 flags = get16(zip, at + 8);
        const quint16 method = get16(zip, at + 10);
        const quint32 crc = get32(zip, at + 16);
        const quint32 packedSize = get32(zip, at + 20);
        const quint32 fullSize = get32(zip, at + 24);
        const quint16 nameLength = get16(zip, at + 28);
        const quint16 extraLength = get16(zip, at + 30);
        const quint16 commentLength = get16(zip, at + 32);
        const quint32 local = get32(zip, at + 42);
        if (at + 46 + nameLength > zip.size()) {
            return fail(error, QStringLiteral("directorio dañado"));
        }
        const QByteArray rawName = zip.mid(at + 46, nameLength);
        at += 46 + nameLength + extraLength + commentLength;

        Entry entry;
        entry.name = (flags & 0x0800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        if (flags & 1) {
            return fail(error, QStringLiteral("«%1» está cifrado").arg(entry.name));
        }
        if (packedSize == 0xffffffff || fullSize == 0xffffffff || local == 0xffffffff) {
            return fail(error, QStringLiteral("ZIP64 no admitido"));
        }
        if (qint64(local) + 30 > zip.size() || get32(zip, local) != 0x04034b50) {
            return fail(error, QStringLiteral("«%1»: cabecera dañada").arg(entry.name));
        }
        // The local header has its own name and extra lengths.
        const qint64 body = qint64(local) + 30 + get16(zip, local + 26) + get16(zip, local + 28);
        if (body + packedSize > zip.size()) {
            return fail(error, QStringLiteral("«%1»: datos cortados").arg(entry.name));
        }
        total += fullSize;
        if (total > MaxTotal) {
            return fail(error, QStringLiteral("el archivo es demasiado grande"));
        }
        if (entry.name.endsWith(QLatin1Char('/'))) {
            continue; // a directory
        }
        const QByteArray packed = zip.mid(qsizetype(body), packedSize);
        if (method == 0) {
            if (packedSize != fullSize) {
                return fail(error, QStringLiteral("«%1»: tamaño incoherente").arg(entry.name));
            }
            entry.data = packed;
            entry.stored = true;
        } else if (method == 8) {
            if (!inflateRaw(packed, fullSize, &entry.data)) {
                return fail(error, QStringLiteral("«%1»: no se puede descomprimir").arg(entry.name));
            }
        } else {
            return fail(error, QStringLiteral("«%1»: compresión no admitida").arg(entry.name));
        }
        if (crc32(entry.data) != crc) {
            return fail(error, QStringLiteral("«%1»: suma de comprobación incorrecta").arg(entry.name));
        }
        entries->append(entry);
    }
    return true;
}
