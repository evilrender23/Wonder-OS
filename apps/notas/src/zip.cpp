#include "zip.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>

#include <zlib.h>

namespace {

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

quint16 get16(const QByteArray &in, qsizetype at)
{
    return quint16(quint8(in.at(at))) | quint16(quint8(in.at(at + 1))) << 8;
}

quint32 get32(const QByteArray &in, qsizetype at)
{
    return quint32(get16(in, at)) | quint32(get16(in, at + 2)) << 16;
}

/// Raw deflate, as ZIP wants it: no zlib header nor trailer.
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

bool inflateRaw(const QByteArray &packed, quint32 size, QByteArray &out)
{
    out.resize(size);
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(packed.constData()));
    stream.avail_in = uInt(packed.size());
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = uInt(out.size());
    const int result = inflate(&stream, Z_FINISH);
    const bool whole = result == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    return whole;
}

quint32 checksum(const QByteArray &data)
{
    return quint32(crc32(crc32(0L, Z_NULL, 0), reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
}

const quint32 localSignature = 0x04034b50;
const quint32 centralSignature = 0x02014b50;
const quint32 endSignature = 0x06054b50;
/// No single member of a notebook is this big; a header that says so is a bomb.
const quint32 largestMember = 1u << 30;

}

void ZipWriter::add(const QString &name, const QByteArray &data)
{
    Entry entry;
    entry.name = name.toUtf8();
    entry.crc = checksum(data);
    entry.size = quint32(data.size());
    entry.offset = quint32(m_out.size());
    QByteArray packed = data.isEmpty() ? QByteArray() : deflateRaw(data);
    if (!packed.isEmpty() && packed.size() < data.size()) {
        entry.method = 8;
    } else {
        packed = data;
    }
    entry.packed = quint32(packed.size());

    const QDateTime now = QDateTime::currentDateTime();
    const quint16 time = quint16(now.time().hour() << 11 | now.time().minute() << 5 | now.time().second() / 2);
    const quint16 date = quint16((qMax(1980, now.date().year()) - 1980) << 9 | now.date().month() << 5 | now.date().day());

    put32(m_out, localSignature);
    put16(m_out, 20);
    put16(m_out, 0x0800); // names in UTF-8
    put16(m_out, entry.method);
    put16(m_out, time);
    put16(m_out, date);
    put32(m_out, entry.crc);
    put32(m_out, entry.packed);
    put32(m_out, entry.size);
    put16(m_out, quint16(entry.name.size()));
    put16(m_out, 0);
    m_out.append(entry.name);
    m_out.append(packed);
    m_entries << entry;
}

QByteArray ZipWriter::finish()
{
    const QDateTime now = QDateTime::currentDateTime();
    const quint16 time = quint16(now.time().hour() << 11 | now.time().minute() << 5 | now.time().second() / 2);
    const quint16 date = quint16((qMax(1980, now.date().year()) - 1980) << 9 | now.date().month() << 5 | now.date().day());
    const quint32 start = quint32(m_out.size());
    for (const Entry &entry : std::as_const(m_entries)) {
        put32(m_out, centralSignature);
        put16(m_out, 0x031e); // made on Unix
        put16(m_out, 20);
        put16(m_out, 0x0800);
        put16(m_out, entry.method);
        put16(m_out, time);
        put16(m_out, date);
        put32(m_out, entry.crc);
        put32(m_out, entry.packed);
        put32(m_out, entry.size);
        put16(m_out, quint16(entry.name.size()));
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0);
        put16(m_out, 0);
        put32(m_out, entry.name.endsWith('/') ? 0x41ed0010u : 0x81a40000u);
        put32(m_out, entry.offset);
        m_out.append(entry.name);
    }
    const quint32 length = quint32(m_out.size()) - start;
    put32(m_out, endSignature);
    put16(m_out, 0);
    put16(m_out, 0);
    put16(m_out, quint16(m_entries.size()));
    put16(m_out, quint16(m_entries.size()));
    put32(m_out, length);
    put32(m_out, start);
    put16(m_out, 0);
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

ZipReader ZipReader::open(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ZipReader(QByteArray());
    }
    return ZipReader(file.readAll());
}

ZipReader::ZipReader(const QByteArray &archive)
    : m_data(archive)
{
    // The end record is the last thing in the file, but for a comment of up to 64 KB.
    qsizetype end = -1;
    for (qsizetype at = m_data.size() - 22; at >= 0 && at >= m_data.size() - 22 - 65535; --at) {
        if (get32(m_data, at) == endSignature) {
            end = at;
            break;
        }
    }
    if (end < 0) {
        return;
    }
    const int count = get16(m_data, end + 10);
    qsizetype at = get32(m_data, end + 16);
    for (int i = 0; i < count; ++i) {
        if (at + 46 > m_data.size() || get32(m_data, at) != centralSignature) {
            return;
        }
        Entry entry;
        entry.method = get16(m_data, at + 10);
        entry.crc = get32(m_data, at + 16);
        entry.packed = get32(m_data, at + 20);
        entry.size = get32(m_data, at + 24);
        entry.offset = get32(m_data, at + 42);
        const int nameLength = get16(m_data, at + 28);
        const int extraLength = get16(m_data, at + 30);
        const int commentLength = get16(m_data, at + 32);
        if (at + 46 + nameLength > m_data.size()) {
            return;
        }
        const QString name = QString::fromUtf8(m_data.mid(at + 46, nameLength));
        m_names << name;
        m_entries.insert(name, entry);
        at += 46 + nameLength + extraLength + commentLength;
    }
    m_valid = true;
}

QByteArray ZipReader::file(const QString &name, bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    const auto found = m_entries.constFind(name);
    if (found == m_entries.constEnd()) {
        return {};
    }
    const Entry &entry = found.value();
    const qsizetype at = entry.offset;
    if (at + 30 > m_data.size() || get32(m_data, at) != localSignature || entry.size > largestMember) {
        return {};
    }
    const qsizetype start = at + 30 + get16(m_data, at + 26) + get16(m_data, at + 28);
    if (start + qsizetype(entry.packed) > m_data.size()) {
        return {};
    }
    const QByteArray packed = m_data.mid(start, entry.packed);
    QByteArray out;
    if (entry.method == 0) {
        out = packed;
    } else if (entry.method != 8 || !inflateRaw(packed, entry.size, out)) {
        return {};
    }
    if (quint32(out.size()) != entry.size || checksum(out) != entry.crc) {
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return out;
}

bool ZipReader::safeName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || name.contains(QChar(0))) {
        return false;
    }
    const QStringList parts = name.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part == QLatin1String("..")) {
            return false;
        }
    }
    return true;
}
