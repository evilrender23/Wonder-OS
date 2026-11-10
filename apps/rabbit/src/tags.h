#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

/// Finder's seven colour tags. The file's `user.xdg.tags` attribute is the
/// truth (it travels with the file and Dolphin reads it); the index only
/// answers "which files carry this tag" for the sidebar.
class Tags : public QObject
{
    Q_OBJECT
public:
    static Tags *self();
    static QStringList names();
    static QString label(const QString &name);

    QStringList tagsFor(const QString &path) const;
    bool setTag(const QString &path, const QString &name, bool on);
    QStringList filesFor(const QString &name);
    void pathRenamed(const QString &from, const QString &to);

Q_SIGNALS:
    void changed(const QString &path);

private:
    Tags();
    QString indexPath() const;
    void load();
    void save() const;

    QHash<QString, QStringList> m_index;
};
