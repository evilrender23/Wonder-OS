#include "zip.h"

#include <QFile>
#include <QtEndian>

#include <zlib.h>

namespace {

quint16 u16(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint16>(data.constData() + at);
}

quint32 u32(const QByteArray &data, qsizetype at)
{
    return qFromLittleEndian<quint32>(data.constData() + at);
}

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

constexpr quint32 localSignature = 0x04034b50;
constexpr quint32 centralSignature = 0x02014b50;
constexpr quint32 endSignature = 0x06054b50;

}

// --- Reading ---------------------------------------------------------------------

ZipReader::ZipReader(const QByteArray &data)
    : m_data(data)
{
    // The end record is the last thing in the file, bar a comment of up to 64 KiB.
    const qsizetype size = m_data.size();
    if (size < 22) {
        return;
    }
    qsizetype end = -1;
    for (qsizetype at = size - 22; at >= 0 && at >= size - 22 - 65535; --at) {
        if (u32(m_data, at) == endSignature) {
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
        if (at < 0 || at + 46 > size || u32(m_data, at) != centralSignature) {
            return;
        }
        Entry entry;
        const quint16 flags = u16(m_data, at + 8);
        entry.method = u16(m_data, at + 10);
        entry.crc = u32(m_data, at + 16);
        entry.compressedSize = u32(m_data, at + 20);
        entry.size = u32(m_data, at + 24);
        const int nameLength = u16(m_data, at + 28);
        const int extraLength = u16(m_data, at + 30);
        const int commentLength = u16(m_data, at + 32);
        entry.offset = u32(m_data, at + 42);
        if (at + 46 + nameLength > size) {
            return;
        }
        const QByteArray raw = m_data.mid(at + 46, nameLength);
        // Bit 11 says UTF-8; without it the name is CP437, which Latin-1 is close enough to.
        const QString name = (flags & 0x800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);
        at += 46 + nameLength + extraLength + commentLength;
        if (flags & 0x1) {
            continue; // Encrypted: not for us.
        }
        if (!m_entries.contains(name)) {
            m_names << name;
        }
        m_entries.insert(name, entry);
    }
    m_valid = true;
}

ZipReader ZipReader::fromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ZipReader(QByteArray());
    }
    return ZipReader(file.readAll());
}

QByteArray ZipReader::file(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        return {};
    }
    const Entry &entry = it.value();
    const qsizetype at = entry.offset;
    if (at + 30 > m_data.size() || u32(m_data, at) != localSignature) {
        return {};
    }
    // The sizes come from the central directory: the local header may leave
    // them to a data descriptor.
    const qsizetype start = at + 30 + u16(m_data, at + 26) + u16(m_data, at + 28);
    if (start + qsizetype(entry.compressedSize) > m_data.size() || entry.size > maxEntrySize) {
        return {};
    }
    const QByteArray packed = QByteArray::fromRawData(m_data.constData() + start, entry.compressedSize);
    QByteArray out;
    if (entry.method == 0) {
        out = QByteArray(packed.constData(), packed.size());
    } else if (entry.method == 8) {
        out = ZipWriter::inflate(packed);
    } else {
        return {};
    }
    if (ZipWriter::crc32(out) != entry.crc) {
        return {};
    }
    return out;
}

// --- Writing ---------------------------------------------------------------------

quint32 ZipWriter::crc32(const QByteArray &data)
{
    return quint32(::crc32(::crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

QByteArray ZipWriter::deflate(const QByteArray &data)
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
    const int result = ::deflate(&stream, Z_FINISH);
    out.truncate(qsizetype(stream.total_out));
    deflateEnd(&stream);
    return result == Z_STREAM_END ? out : QByteArray();
}

QByteArray ZipWriter::inflate(const QByteArray &data, qint64 limit)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return {};
    }
    QByteArray out;
    char buffer[65536];
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof(buffer);
        result = ::inflate(&stream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
            break;
        }
        out.append(buffer, qsizetype(sizeof(buffer) - stream.avail_out));
        if (out.size() > limit) {
            result = Z_DATA_ERROR;
        }
        if (result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) {
            result = Z_DATA_ERROR; // Ran out of input before the end of the stream.
        }
    }
    inflateEnd(&stream);
    return result == Z_STREAM_END ? out : QByteArray();
}

void ZipWriter::add(const QString &name, const QByteArray &data, bool compress)
{
    const QByteArray rawName = name.toUtf8();
    QByteArray packed;
    quint16 method = 0;
    if (compress && !data.isEmpty()) {
        packed = deflate(data);
        method = 8;
    }
    if (method == 0 || packed.isEmpty() || packed.size() >= data.size()) {
        packed = data;
        method = 0;
    }
    const quint32 crc = crc32(data);
    const quint32 offset = quint32(m_body.size());
    // A fixed moment (1 January 2024): the same deck gives the same bytes.
    const quint16 time = 0;
    const quint16 date = quint16(((2024 - 1980) << 9) | (1 << 5) | 1);

    put32(m_body, localSignature);
    put16(m_body, 20);
    put16(m_body, 0x800);
    put16(m_body, method);
    put16(m_body, time);
    put16(m_body, date);
    put32(m_body, crc);
    put32(m_body, quint32(packed.size()));
    put32(m_body, quint32(data.size()));
    put16(m_body, quint16(rawName.size()));
    put16(m_body, 0);
    m_body.append(rawName);
    m_body.append(packed);

    put32(m_directory, centralSignature);
    put16(m_directory, 20);
    put16(m_directory, 20);
    put16(m_directory, 0x800);
    put16(m_directory, method);
    put16(m_directory, time);
    put16(m_directory, date);
    put32(m_directory, crc);
    put32(m_directory, quint32(packed.size()));
    put32(m_directory, quint32(data.size()));
    put16(m_directory, quint16(rawName.size()));
    put16(m_directory, 0);
    put16(m_directory, 0);
    put16(m_directory, 0);
    put16(m_directory, 0);
    put32(m_directory, 0);
    put32(m_directory, offset);
    m_directory.append(rawName);
    ++m_count;
}

QByteArray ZipWriter::finish()
{
    QByteArray out = m_body;
    const quint32 start = quint32(out.size());
    out.append(m_directory);
    put32(out, endSignature);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(m_count));
    put16(out, quint16(m_count));
    put32(out, quint32(m_directory.size()));
    put32(out, start);
    put16(out, 0);
    m_body.clear();
    m_directory.clear();
    m_count = 0;
    return out;
}
