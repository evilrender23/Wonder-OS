#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

/// Writes a ZIP archive in memory: deflate (or stored, when that is smaller),
/// UTF-8 names, no ZIP64 — archives and members under 4 GB.
class ZipWriter
{
public:
    void add(const QString &name, const QByteArray &data);
    /// The whole archive; nothing can be added afterwards.
    QByteArray finish();
    bool save(const QString &path);

private:
    struct Entry {
        QByteArray name;
        quint32 crc = 0;
        quint32 packed = 0;
        quint32 size = 0;
        quint32 offset = 0;
        quint16 method = 0;
    };
    QByteArray m_out;
    QList<Entry> m_entries;
};

/// Reads a ZIP archive from memory: stored and deflated members.
class ZipReader
{
public:
    explicit ZipReader(const QByteArray &archive);
    static ZipReader open(const QString &path);

    bool valid() const { return m_valid; }
    /// In the order of the archive; directories ("name/") included.
    QStringList names() const { return m_names; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    /// The member inflated and checked against its CRC; `ok` says whether it was there and whole.
    QByteArray file(const QString &name, bool *ok = nullptr) const;
    /// A member name that stays inside the directory it is unpacked to.
    static bool safeName(const QString &name);

private:
    struct Entry {
        quint32 crc = 0;
        quint32 packed = 0;
        quint32 size = 0;
        quint32 offset = 0;
        quint16 method = 0;
    };
    QByteArray m_data;
    QStringList m_names;
    QHash<QString, Entry> m_entries;
    bool m_valid = false;
};
