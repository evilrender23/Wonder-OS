#include "zip.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>

#include <zlib.h>

namespace {

// An entry larger than this is not a text document: refuse it rather than
// let a crafted archive ask for gigabytes.
constexpr quint32 maxEntrySize = 512u * 1024u * 1024u;

quint16 u16(const QByteArray &data, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray &data, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

void put16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char(value >> 8));
}

void put32(QByteArray &out, quint32 value)
{
    put16(out, quint16(value & 0xffff));
    put16(out, quint16(value >> 16));
}

quint32 crcOf(const QByteArray &data)
{
    return quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

QByteArray deflateRaw(const QByteArray &data)
{
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    QByteArray out(qsizetype(deflateBound(&stream, uLong(data.size()))), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = deflate(&stream, Z_FINISH);
    out.truncate(qsizetype(stream.total_out));
    deflateEnd(&stream);
    return result == Z_STREAM_END ? out : QByteArray();
}

QByteArray inflateRaw(const char *packed, quint32 packedSize, quint32 size)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return {};
    }
    QByteArray out(qsizetype(size), Qt::Uninitialized);
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed));
    stream.avail_in = packedSize;
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = size;
    const int result = inflate(&stream, Z_FINISH);
    const bool whole = result == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    return whole ? out : QByteArray();
}

} // namespace

ZipReader::ZipReader(const QByteArray &data)
    : m_data(data)
{
    // The end record sits in the last 22 bytes, or before a comment of up to 64 KiB.
    const qsizetype size = m_data.size();
    qsizetype end = -1;
    for (qsizetype at = size - 22; at >= 0 && at >= size - 22 - 65535; --at) {
        if (u32(m_data, at) == 0x06054b50) {
            end = at;
            break;
        }
    }
    if (end < 0) {
        return;
    }
    const int count = u16(m_data, end + 10);
    qsizetype at = u32(m_data, end + 16);
    for (int i = 0; i < count; ++i) {
        if (at + 46 > size || u32(m_data, at) != 0x02014b50) {
            return;
        }
        Entry entry;
        entry.method = u16(m_data, at + 10);
        entry.crc = u32(m_data, at + 16);
        entry.packed = u32(m_data, at + 20);
        entry.size = u32(m_data, at + 24);
        entry.offset = u32(m_data, at + 42);
        const int nameLength = u16(m_data, at + 28);
        const int skip = u16(m_data, at + 30) + u16(m_data, at + 32);
        if (at + 46 + nameLength > size) {
            return;
        }
        const QString name = QString::fromUtf8(m_data.constData() + at + 46, nameLength);
        if (!m_entries.contains(name)) {
            m_names << name;
        }
        m_entries.insert(name, entry);
        at += 46 + nameLength + skip;
    }
    m_valid = true;
}

ZipReader ZipReader::fromFile(const QString &path)
{
    QFile file(path);
    return ZipReader(file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray());
}

QByteArray ZipReader::file(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        return {};
    }
    const Entry &entry = it.value();
    const qsizetype size = m_data.size();
    if (qsizetype(entry.offset) + 30 > size || u32(m_data, entry.offset) != 0x04034b50 || entry.size > maxEntrySize) {
        return {};
    }
    const qsizetype start = qsizetype(entry.offset) + 30 + u16(m_data, entry.offset + 26) + u16(m_data, entry.offset + 28);
    if (start + qsizetype(entry.packed) > size) {
        return {};
    }
    QByteArray out;
    if (entry.method == 0) {
        if (entry.packed != entry.size) {
            return {};
        }
        out = m_data.mid(start, entry.size);
    } else if (entry.method == 8) {
        out = inflateRaw(m_data.constData() + start, entry.packed, entry.size);
        if (out.isNull() && entry.size > 0) {
            return {};
        }
    } else {
        return {};
    }
    if (crcOf(out) != entry.crc) {
        return {};
    }
    // An empty entry is still an entry: never null.
    return out.isNull() ? QByteArray("") : out;
}

void ZipWriter::add(const QString &name, const QByteArray &data, bool compress)
{
    if (m_finished) {
        return;
    }
    const QByteArray encoded = name.toUtf8();
    QByteArray packed = compress && !data.isEmpty() ? deflateRaw(data) : QByteArray();
    // Deflate that does not shrink is stored instead.
    const bool deflated = !packed.isEmpty() && packed.size() < data.size();
    const QByteArray &body = deflated ? packed : data;
    const quint32 crc = crcOf(data);
    const QDateTime now = QDateTime::currentDateTime();
    const quint16 time = quint16((now.time().hour() << 11) | (now.time().minute() << 5) | (now.time().second() / 2));
    const quint16 date = quint16(((qMax(1980, now.date().year()) - 1980) << 9) | (now.date().month() << 5) | now.date().day());
    const quint32 offset = quint32(m_out.size());

    const auto header = [&](QByteArray &out, bool central) {
        put32(out, central ? 0x02014b50 : 0x04034b50);
        if (central) {
            put16(out, 20); // made by
        }
        put16(out, 20); // needed
        put16(out, 0x0800); // names are UTF-8
        put16(out, deflated ? 8 : 0);
        put16(out, time);
        put16(out, date);
        put32(out, crc);
        put32(out, quint32(body.size()));
        put32(out, quint32(data.size()));
        put16(out, quint16(encoded.size()));
        put16(out, 0); // extra
        if (central) {
            put16(out, 0); // comment
            put16(out, 0); // disk
            put16(out, 0); // internal attributes
            put32(out, 0); // external attributes
            put32(out, offset);
        }
        out.append(encoded);
    };
    header(m_out, false);
    m_out.append(body);
    header(m_directory, true);
    ++m_count;
}

QByteArray ZipWriter::finish()
{
    if (!m_finished) {
        m_finished = true;
        const quint32 start = quint32(m_out.size());
        m_out.append(m_directory);
        put32(m_out, 0x06054b50);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, quint16(m_count));
        put16(m_out, quint16(m_count));
        put32(m_out, quint32(m_directory.size()));
        put32(m_out, start);
        put16(m_out, 0);
    }
    return m_out;
}

bool ZipWriter::save(const QString &path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(finish());
    return file.commit();
}
