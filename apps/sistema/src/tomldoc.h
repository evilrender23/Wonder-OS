#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

/// A TOML file edited in place: values change, everything else (comments, order, spacing) stays.
///
/// A path is the table's parts followed by the key, e.g. {"input", "keyboard", "layout"}.
/// Arrays of tables (`[[outputs]]`) are parsed past, never edited.
class TomlDoc
{
public:
    enum class Kind { Auto, Bool, Int, Float, String };

    bool parse(const QString &text, QString *error = nullptr);
    QString text() const { return m_text; }

    bool contains(const QStringList &path) const;
    QVariant value(const QStringList &path) const;
    /// The keys set directly in a table, in file order.
    QStringList keys(const QStringList &table) const;

    void set(const QStringList &path, const QVariant &value, Kind kind = Kind::Auto);
    void remove(const QStringList &path);

    static QString encode(const QVariant &value, Kind kind = Kind::Auto);
    static QString encodeKey(const QString &key);

private:
    struct Entry {
        QStringList path;
        bool inArray = false;
        qsizetype lineStart = 0;
        qsizetype lineEnd = 0;
        qsizetype valueStart = 0;
        qsizetype valueEnd = 0;
        QVariant value;
    };
    struct Table {
        QStringList path;
        bool array = false;
        qsizetype headerStart = 0;
        qsizetype bodyStart = 0;
    };

    const Entry *find(const QStringList &path) const;
    const Entry *findPrefix(const QStringList &path) const;
    qsizetype insertionPoint(const QStringList &table, bool *exists) const;
    void reparse();

    QString m_text;
    QList<Entry> m_entries;
    QList<Table> m_tables;
};
