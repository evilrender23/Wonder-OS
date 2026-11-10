#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

/// Reads a ZIP archive held in memory: stored and deflated entries, no ZIP64,
/// no encryption. Enough for the OOXML and OpenDocument containers.
class ZipReader
{
public:
    explicit ZipReader(const QByteArray &data);
    static ZipReader fromFile(const QString &path);

    /// False when the bytes are not a ZIP archive this reader understands.
    bool isValid() const { return m_valid; }
    /// The entries, in the order of the central directory.
    QStringList names() const { return m_names; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    /// The bytes of an entry; empty when it is absent, damaged or too large.
    QByteArray file(const QString &name) const;

    /// No entry may inflate past this: a guard against archive bombs.
    static constexpr qint64 maxEntrySize = 512ll * 1024 * 1024;

private:
    struct Entry {
        quint16 method = 0;
        quint32 crc = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_names;
    bool m_valid = false;
};

/// Writes a ZIP archive into memory, deflating what shrinks.
class ZipWriter
{
public:
    /// `compress` false stores the bytes as they are (ODF's `mimetype`, images).
    void add(const QString &name, const QByteArray &data, bool compress = true);
    /// The archive; the writer is spent afterwards.
    QByteArray finish();

    static quint32 crc32(const QByteArray &data);
    /// Raw deflate, without the zlib header.
    static QByteArray deflate(const QByteArray &data);
    /// Raw inflate; empty on damaged input or past `limit` bytes.
    static QByteArray inflate(const QByteArray &data, qint64 limit = ZipReader::maxEntrySize);

private:
    QByteArray m_body;
    QByteArray m_directory;
    int m_count = 0;
};
