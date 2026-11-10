#pragma once

#include <QAbstractListModel>
#include <QRandomGenerator>
#include <QStringList>

/// What is lined up to play, and which of it comes next.
class Playlist : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    /// 0 no, 1 the whole list, 2 this one.
    Q_PROPERTY(int repeat READ repeat WRITE setRepeat NOTIFY modeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY modeChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, FolderRole, CurrentRole, AudioRole };

    explicit Playlist(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_paths.size()); }
    int current() const { return m_current; }
    int repeat() const { return m_repeat; }
    void setRepeat(int repeat);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    QStringList paths() const { return m_paths; }
    QString path(int index) const { return m_paths.value(index); }
    QString currentPath() const { return m_paths.value(m_current); }

    /// Replaces the list. A path named twice stays once.
    void set(const QStringList &paths, int current);
    /// Adds what is not there yet; returns the index of the first of `paths`, -1 if none.
    int append(const QStringList &paths);
    void setCurrent(int index);
    /// The index to play after the current one, -1 at the end of it all.
    /// `manual`: asked for by hand, so «repeat this one» does not hold it back.
    int next(bool manual) const;
    int previous() const;

    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void clear();

    /// Makes the shuffle the same every time (tests).
    void seed(quint32 seed);

Q_SIGNALS:
    void countChanged();
    void currentChanged();
    void modeChanged();
    /// The one that was playing has left the list; `current` is what took its place, or -1.
    void currentRemoved();

private:
    void reshuffle();
    void markCurrent(int before);

    QStringList m_paths;
    /// With shuffle on: every path once, in the order it will be played.
    QStringList m_order;
    int m_current = -1;
    int m_repeat = 0;
    bool m_shuffle = false;
    QRandomGenerator m_random;
};
