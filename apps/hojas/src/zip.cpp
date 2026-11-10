#include "zip.h"

#include <QtEndian>

#include <zlib.h>

namespace
{
quint16 u16(const QByteArray &d, qsizetype at)
{
    return at + 2 <= d.size() ? qFromLittleEndian<quint16>(d.constData() + at) : 0;
}
quint32 u32(const QByteArray &d, qsizetype at)
{
    return at + 4 <= d.size() ? qFromLittleEndian<quint32>(d.constData() + at) : 0;
}
void put16(QByteArray &d, quint16 v)
{
    char b[2];
    qToLittleEndian(v, b);
    d.append(b, 2);
}
void put32(QByteArray &d, quint32 v)
{
    char b[4];
    qToLittleEndian(v, b);
    d.append(b, 4);
}
}

quint32 Zip::crc32(const QByteArray &data)
{
    return quint32(::crc32(::crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

QByteArray Zip::deflateRaw(const QByteArray &data)
{
    z_stream stream = {};
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
    out.resize(result == Z_STREAM_END ? qsizetype(stream.total_out) : 0);
    deflateEnd(&stream);
    return out;
}

QByteArray Zip::inflateRaw(const QByteArray &data, qsizetype expected)
{
    z_stream stream = {};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return {};
    }
    QByteArray out;
    out.resize(qMax<qsizetype>(expected, 64));
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = uInt(data.size());
    int result = Z_OK;
    qsizetype written = 0;
    while (result == Z_OK) {
        if (written == out.size()) {
            // The size in the header lied, or was not there: grow as needed.
            if (out.size() > (qsizetype(1) << 30)) {
                break;
            }
            out.resize(out.size() * 2);
        }
        stream.next_out = reinterpret_cast<Bytef *>(out.data() + written);
        stream.avail_out = uInt(out.size() - written);
        result = inflate(&stream, Z_NO_FLUSH);
        written = out.size() - stream.avail_out;
        if (result == Z_OK && stream.avail_in == 0 && stream.avail_out != 0) {
            break;
        }
    }
    inflateEnd(&stream);
    if (result != Z_STREAM_END) {
        return {};
    }
    out.resize(written);
    return out;
}

ZipReader::ZipReader(const QByteArray &archive)
    : m_data(archive)
{
    // The end-of-central-directory record sits at the very end, after an
    // optional comment of up to 64 KiB.
    const qsizetype least = qMax<qsizetype>(0, m_data.size() - 65557);
    qsizetype end = -1;
    for (qsizetype at = m_data.size() - 22; at >= least; --at) {
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
        if (u32(m_data, at) != 0x02014b50 || at + 46 > m_data.size()) {
            return;
        }
        Entry entry;
        const quint16 flags = u16(m_data, at + 8);
        entry.method = u16(m_data, at + 10);
        entry.crc = u32(m_data, at + 16);
        entry.packed = u32(m_data, at + 20);
        entry.size = u32(m_data, at + 24);
        const int nameLength = u16(m_data, at + 28);
        const int extraLength = u16(m_data, at + 30);
        const int commentLength = u16(m_data, at + 32);
        entry.offset = u32(m_data, at + 42);
        const QByteArray raw = m_data.mid(at + 46, nameLength);
        const QString name = (flags & 0x800) ? QString::fromUtf8(raw) : QString::fromLatin1(raw);
        at += 46 + nameLength + extraLength + commentLength;
        if (name.endsWith(QLatin1Char('/'))) {
            continue;
        }
        m_entries.insert(name, entry);
        m_names << name;
    }
    m_valid = true;
}

QByteArray ZipReader::file(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd()) {
        return {};
    }
    const Entry &entry = it.value();
    const qsizetype local = entry.offset;
    if (u32(m_data, local) != 0x04034b50) {
        return {};
    }
    const qsizetype start = local + 30 + u16(m_data, local + 26) + u16(m_data, local + 28);
    if (start + qsizetype(entry.packed) > m_data.size()) {
        return {};
    }
    const QByteArray packed = QByteArray::fromRawData(m_data.constData() + start, entry.packed);
    QByteArray content;
    if (entry.method == 0) {
        content = QByteArray(packed.constData(), packed.size());
    } else if (entry.method == 8) {
        content = Zip::inflateRaw(packed, entry.size);
    } else {
        return {};
    }
    if (content.size() != qsizetype(entry.size) || Zip::crc32(content) != entry.crc) {
        return {};
    }
    // An empty entry is still an entry.
    return content.isNull() ? QByteArray("") : content;
}

void ZipWriter::add(const QString &name, const QByteArray &content, bool store)
{
    const QByteArray rawName = name.toUtf8();
    QByteArray packed = store ? QByteArray() : Zip::deflateRaw(content);
    const bool deflated = !store && !packed.isEmpty() && packed.size() < content.size();
    if (!deflated) {
        packed = content;
    }
    const quint32 crc = Zip::crc32(content);
    const quint32 offset = quint32(m_out.size());
    // A fixed moment (1 January 2020): the same workbook gives the same bytes.
    const quint16 time = 0;
    const quint16 date = quint16(((2020 - 1980) << 9) | (1 << 5) | 1);

    put32(m_out, 0x04034b50);
    put16(m_out, 20);
    put16(m_out, 0x800);
    put16(m_out, deflated ? 8 : 0);
    put16(m_out, time);
    put16(m_out, date);
    put32(m_out, crc);
    put32(m_out, quint32(packed.size()));
    put32(m_out, quint32(content.size()));
    put16(m_out, quint16(rawName.size()));
    put16(m_out, 0);
    m_out.append(rawName);
    m_out.append(packed);

    put32(m_directory, 0x02014b50);
    put16(m_directory, 20);
    put16(m_directory, 20);
    put16(m_directory, 0x800);
    put16(m_directory, deflated ? 8 : 0);
    put16(m_directory, time);
    put16(m_directory, date);
    put32(m_directory, crc);
    put32(m_directory, quint32(packed.size()));
    put32(m_directory, quint32(content.size()));
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
    return m_out;
}
