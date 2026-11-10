#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

/// Reads a ZIP archive held in memory: stored and deflated entries, no ZIP64,
/// no encryption — what .docx and .odt are made of.
class ZipReader
{
public:
    explicit ZipReader(const QByteArray &data);
    static ZipReader fromFile(const QString &path);

    bool isValid() const { return m_valid; }
    /// In the order of the central directory.
    QStringList names() const { return m_names; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    /// The bytes of an entry; null when it is absent, damaged or too big to trust.
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
    QStringList m_names;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;
};

/// Writes a ZIP archive: raw deflate from zlib, CRC-32, UTF-8 names.
class ZipWriter
{
public:
    /// `compress` false stores the bytes as they are (the `mimetype` of an .odt, images).
    void add(const QString &name, const QByteArray &data, bool compress = true);
    /// The archive, with its central directory. Nothing can be added after.
    QByteArray finish();
    bool save(const QString &path);

private:
    QByteArray m_out;
    QByteArray m_directory;
    int m_count = 0;
    bool m_finished = false;
};
