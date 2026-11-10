#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

/// A small ZIP reader and writer over zlib: stored and deflated entries, no
/// encryption, no ZIP64. Enough for the container a drawing lives in.
namespace Zip
{
struct Entry {
    QString name;
    QByteArray data;
    /// Written without compression (the `mimetype` entry of a container).
    bool stored = false;
};

quint32 crc32(const QByteArray &data);
/// Raw deflate (no zlib header), and its inverse; `expected` caps the output.
QByteArray deflateRaw(const QByteArray &data);
bool inflateRaw(const QByteArray &data, qint64 expected, QByteArray *out);

QByteArray write(const QList<Entry> &entries);
/// False, with a reason, when the bytes are not a ZIP this reader can read.
bool read(const QByteArray &zip, QList<Entry> *entries, QString *error = nullptr);
}
