#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

/// How one folder was customised: a colour for the folder itself and a symbol
/// or an emoji on its front, as Finder's "Customize Folder…" does on macOS.
struct FolderLook {
    /// "rrggbb", or empty for the usual blue.
    QString color;
    /// The name of one of our symbols, or empty.
    QString symbol;
    /// One emoji, or empty. A look carries a symbol or an emoji, never both.
    QString emoji;

    bool isEmpty() const { return color.isEmpty() && symbol.isEmpty() && emoji.isEmpty(); }
    bool operator==(const FolderLook &other) const = default;

    /// What is stored on the folder; empty for the plain look.
    QByteArray toAttribute() const;
    static FolderLook fromAttribute(const QByteArray &value);

    /// The look as part of an icon name: `folder~<color>~<s|e>~<what>`, safe inside an image:// URL.
    QString iconName(const QString &base) const;
    /// Splits such a name; a plain "folder-music" gives that name and an empty look.
    static FolderLook fromIconName(const QString &name, QString *base);
};

/// The looks of folders. The folder's `user.wonderland.folder` attribute is
/// the truth, so the look moves with the folder and the desktop reads it too.
class FolderLooks : public QObject
{
    Q_OBJECT
public:
    static FolderLooks *self();

    FolderLook lookFor(const QString &path) const;
    /// False when the disk keeps no attributes or the folder isn't ours to change.
    bool setLook(const QString &path, const FolderLook &look);

    /// The colour every folder without one of its own takes: "rrggbb", or empty for the usual
    /// blue. Chosen in Sistema, which keeps it in `~/.config/wonderland/appearance.json`.
    QString tint() const { return m_tint; }
    static QString tintFile();
    /// Follows that file from now on; needs the event loop of the thread that asks.
    void watchTint();
    /// Reads the file again; `tintChanged` if it says something else now.
    void reloadTint();

    /// Draws a plain folder with a look, for the picture other programs show.
    void setPainter(std::function<QImage(const FolderLook &)> painter) { m_painter = std::move(painter); }
    /// Where that picture is kept: named after the attribute (and the tint, when there is one),
    /// so whoever reads both finds the picture without asking us.
    static QString picturePath(const QByteArray &attribute, const QString &tint = QString());
    /// Makes sure the picture of this folder's look exists; its path, or empty for a plain
    /// folder in the usual blue. Under a tint every folder has a picture.
    QString ensurePicture(const QString &path) const;

Q_SIGNALS:
    void changed(const QString &path);
    void tintChanged();

private:
    FolderLooks();
    QString paint(const QByteArray &stored, const FolderLook &look) const;

    std::function<QImage(const FolderLook &)> m_painter;
    QString m_tint;
    bool m_watching = false;
};
