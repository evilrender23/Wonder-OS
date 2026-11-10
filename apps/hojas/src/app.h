#pragma once

#include "book.h"

#include <QObject>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class QFileSystemWatcher;
class QJSEngine;
class QQmlEngine;
class QTimer;
class Store;

/// What the window needs from outside itself: the document, the desktop's
/// light/dark and glass switches, the clipboard, and the files lately open.
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    /// The window is see-through where the compositor frosts what is behind it.
    Q_PROPERTY(bool glass READ glass NOTIFY darkChanged)
    Q_PROPERTY(bool testing READ testing CONSTANT)
    Q_PROPERTY(QSize testSize READ testSize CONSTANT)
    Q_PROPERTY(QStringList testActions READ testActions CONSTANT)
    Q_PROPERTY(Book *book READ book CONSTANT)
    Q_PROPERTY(QVariantList recents READ recents NOTIFY recentsChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    /// Why the file given on the command line could not be opened, if so.
    Q_PROPERTY(QString startProblem MEMBER m_startProblem CONSTANT)

public:
    enum class Appearance { Auto, Light, Dark };

    /// `shotPath` set: a test run, which quits after one picture.
    App(const QString &dataDir, const QString &shotPath, QObject *parent = nullptr);
    ~App() override;
    static App *create(QQmlEngine *, QJSEngine *);
    static App *instance() { return s_app; }
    /// Where settings, versions and recovery copies live (WONDERLAND_HOJAS_DATA moves it).
    static QString defaultDataDir();

    bool dark() const { return m_dark; }
    bool glass() const { return m_glass; }
    void setAppearance(Appearance appearance);
    bool testing() const { return !m_shotPath.isEmpty(); }
    QSize testSize() const { return m_testSize; }
    QStringList testActions() const { return m_testActions; }
    void setTest(const QStringList &actions, const QSize &size);
    void setStartProblem(const QString &problem) { m_startProblem = problem; }
    Book *book() const { return m_book; }
    QVariantList recents() const;
    QString version() const;

    /// Opens a file here when the window has no document, in a window of its
    /// own otherwise. An error message in Spanish, or nothing.
    Q_INVOKABLE QString open(const QString &path);
    /// Another Hojas, with a file, a template («--new budget») or the start screen.
    Q_INVOKABLE void launch(const QStringList &arguments);
    Q_INVOKABLE QString save();
    Q_INVOKABLE QString saveAs(const QString &path);
    Q_INVOKABLE void forgetRecent(const QString &path);
    Q_INVOKABLE QString localPath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QUrl fileUrl(const QString &path) const { return QUrl::fromLocalFile(path); }
    /// The folder a file dialog starts in: the document's, or Documents.
    Q_INVOKABLE QUrl folder() const;
    Q_INVOKABLE QString suggestedPath(const QString &suffix) const;

    Q_INVOKABLE QString clipboard() const;
    Q_INVOKABLE void setClipboard(const QString &text);

    Q_INVOKABLE QVariant setting(const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setSetting(const QString &key, const QVariant &value);
    Q_INVOKABLE void saveShot(QQuickWindow *window);

Q_SIGNALS:
    void darkChanged();
    void recentsChanged();

private:
    void refreshDark();
    void remember(const QString &path);
    QString barStatePath() const;

    static App *s_app;
    Store *m_store;
    Book *m_book;
    QFileSystemWatcher *m_watcher;
    QTimer *m_timer;
    Appearance m_appearance = Appearance::Auto;
    QString m_shotPath;
    QString m_testClipboard;
    QString m_startProblem;
    QStringList m_testActions;
    QSize m_testSize = QSize(1180, 760);
    bool m_dark = false;
    bool m_glass = false;
};

/// A chart of the sheet in sight, drawn where it sits.
class ChartItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int chartId MEMBER m_id NOTIFY changed)
    /// Bound to what the chart depends on, so a change paints it again.
    Q_PROPERTY(int revision MEMBER m_revision NOTIFY changed)
    Q_PROPERTY(bool dark MEMBER m_dark NOTIFY changed)

public:
    explicit ChartItem(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

Q_SIGNALS:
    void changed();

private:
    int m_id = 0;
    int m_revision = 0;
    bool m_dark = false;
};
