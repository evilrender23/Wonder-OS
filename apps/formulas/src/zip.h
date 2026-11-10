#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

/// Reads a ZIP archive held in memory: stored and deflated entries, which is
/// all the office containers (xlsx, ods) use. No ZIP64, no encryption.
class ZipReader
{
public:
    explicit ZipReader(const QByteArray &archive);

    bool isValid() const { return m_valid; }
    /// The names in the order of the central directory.
    QStringList names() const { return m_names; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    /// The bytes of an entry, inflated; null when absent or broken.
    QByteArray file(const QString &name) const;

private:
    struct Entry {
        quint32 offset = 0;
        quint32 packed = 0;
        quint32 size = 0;
        quint32 crc = 0;
        quint16 method = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_names;
    bool m_valid = false;
};

/// Builds a ZIP archive in memory, deflating what shrinks.
class ZipWriter
{
public:
    /// `store` keeps the bytes as they are (the `mimetype` of an ODF file).
    void add(const QString &name, const QByteArray &content, bool store = false);
    /// The archive; nothing can be added afterwards.
    QByteArray finish();

private:
    QByteArray m_out;
    QByteArray m_directory;
    int m_count = 0;
};

namespace Zip
{
quint32 crc32(const QByteArray &data);
/// Raw deflate (no zlib header), as ZIP wants it.
QByteArray deflateRaw(const QByteArray &data);
QByteArray inflateRaw(const QByteArray &data, qsizetype expected);
}
