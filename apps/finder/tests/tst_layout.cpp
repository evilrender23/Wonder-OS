#include "layout.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

static const qreal W = 520;
static const qreal CW = 104;
static const qreal CH = 118;

class TestLayout : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QTemporaryDir m_root;
    int m_serial = 0;

    QString makeFolder()
    {
        const QString path = m_root.path() + QStringLiteral("/folder%1").arg(++m_serial);
        QDir().mkpath(path);
        // A layout forgets names that are no longer in the folder, so these have to be.
        for (const QString &name : abc()) {
            QFile(path + QLatin1Char('/') + name).open(QIODevice::WriteOnly);
        }
        return path;
    }
    static QPointF at(const QVariantMap &map, const QString &name)
    {
        return map.value(name).toPointF();
    }
    static QStringList abc()
    {
        return {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("f"), QStringLiteral("g")};
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_data.isValid() && m_root.isValid());
        qputenv("WONDERLAND_FINDER_DATA", m_data.path().toUtf8());
    }

    void flowsInRows()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        QVERIFY(!layout.arranged());
        const QVariantMap map = layout.flow(abc(), W, CW, CH);
        QCOMPARE(at(map, QStringLiteral("a")), QPointF(0, 0));
        QCOMPARE(at(map, QStringLiteral("e")), QPointF(4 * CW, 0));
        QCOMPARE(at(map, QStringLiteral("f")), QPointF(0, CH));
        // Looking stores nothing.
        QVERIFY(!layout.arranged());
        QVERIFY(QDir(FolderLayout::dataDirectory()).isEmpty());
    }

    void movedIconStaysAcrossSessions()
    {
        const QUrl url = QUrl::fromLocalFile(makeFolder());
        {
            FolderLayout layout;
            layout.setUrl(url);
            layout.freeze(abc(), W, CW, CH);
            layout.moveTo({{QStringLiteral("b"), QPointF(333, 444)}}, false, CW, CH);
            QVERIFY(layout.arranged());
        }
        FolderLayout again;
        again.setUrl(url);
        QVERIFY(again.arranged());
        const QVariantMap map = again.place(abc(), W, CW, CH);
        QCOMPARE(at(map, QStringLiteral("b")), QPointF(333, 444));
        QCOMPARE(at(map, QStringLiteral("c")), QPointF(2 * CW, 0));
        QCOMPARE(at(map, QStringLiteral("f")), QPointF(0, CH));
    }

    void viewAndSizeAreTheFoldersOwn()
    {
        const QUrl one = QUrl::fromLocalFile(makeFolder());
        const QUrl two = QUrl::fromLocalFile(makeFolder());
        {
            FolderLayout layout;
            layout.setUrl(one);
            layout.setView(QStringLiteral("list"));
            layout.setIconSize(96);
            layout.setArrange(QStringLiteral("grid"));
            layout.setUrl(two);
            QCOMPARE(layout.view(), QString());
            QCOMPARE(layout.iconSize(), 0);
            QCOMPARE(layout.arrange(), QString());
        }
        FolderLayout again;
        again.setUrl(one);
        QCOMPARE(again.view(), QStringLiteral("list"));
        QCOMPARE(again.iconSize(), 96);
        QCOMPARE(again.arrange(), QStringLiteral("grid"));
    }

    void newFileTakesFirstFreeCell()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        layout.freeze({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, W, CW, CH);
        layout.moveTo({{QStringLiteral("b"), QPointF(0, 3 * CH)}}, false, CW, CH);
        const QVariantMap map = layout.place({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("new")}, W, CW, CH);
        // b left its cell free; the newcomer takes it, and nothing else moves.
        QCOMPARE(at(map, QStringLiteral("new")), QPointF(CW, 0));
        QCOMPARE(at(map, QStringLiteral("a")), QPointF(0, 0));
        QCOMPARE(at(map, QStringLiteral("b")), QPointF(0, 3 * CH));
        // And it keeps that place.
        const QVariantMap later = layout.place({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("new"), QStringLiteral("newer")}, W, CW, CH);
        QCOMPARE(at(later, QStringLiteral("new")), QPointF(CW, 0));
        QCOMPARE(at(later, QStringLiteral("newer")), QPointF(3 * CW, 0));
    }

    void snapLandsOnFreeCells()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        layout.freeze({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, W, CW, CH);
        // Dropped almost on top of a: it snaps to a cell, but never a's.
        layout.moveTo({{QStringLiteral("c"), QPointF(10, 8)}}, true, CW, CH);
        const QVariantMap map = layout.place({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, W, CW, CH);
        const QPointF c = at(map, QStringLiteral("c"));
        QCOMPARE(std::fmod(c.x(), CW), 0.0);
        QCOMPARE(std::fmod(c.y(), CH), 0.0);
        QVERIFY(c != at(map, QStringLiteral("a")));
        QVERIFY(c != at(map, QStringLiteral("b")));
    }

    void cleanUpSeparatesOverlaps()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        const QStringList names{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
        layout.freeze(names, W, CW, CH);
        layout.moveTo({{QStringLiteral("a"), QPointF(210, 121)}, {QStringLiteral("b"), QPointF(214, 119)}, {QStringLiteral("c"), QPointF(205, 125)}}, false, CW, CH);
        layout.cleanUp(names, CW, CH);
        const QVariantMap map = layout.place(names, W, CW, CH);
        QSet<QPair<int, int>> cells;
        for (const QString &name : names) {
            const QPointF p = at(map, name);
            QCOMPARE(std::fmod(p.x(), CW), 0.0);
            QCOMPARE(std::fmod(p.y(), CH), 0.0);
            cells.insert({int(p.x() / CW), int(p.y() / CH)});
        }
        QCOMPARE(cells.size(), 3);
    }

    void renameKeepsThePlace()
    {
        const QUrl url = QUrl::fromLocalFile(makeFolder());
        FolderLayout layout;
        layout.setUrl(url);
        layout.freeze({QStringLiteral("a"), QStringLiteral("b")}, W, CW, CH);
        layout.moveTo({{QStringLiteral("a"), QPointF(300, 300)}}, false, CW, CH);
        layout.rename(QStringLiteral("a"), QStringLiteral("z"));
        const QVariantMap map = layout.place({QStringLiteral("b"), QStringLiteral("z")}, W, CW, CH);
        QCOMPARE(at(map, QStringLiteral("z")), QPointF(300, 300));
    }

    void reserveKeepsADropWhereItFell()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        layout.freeze({QStringLiteral("a")}, W, CW, CH);
        layout.reserve({QStringLiteral("x"), QStringLiteral("y")}, QPointF(250, 260), W, CW, CH);
        const QVariantMap map = layout.place({QStringLiteral("a"), QStringLiteral("x"), QStringLiteral("y")}, W, CW, CH);
        QCOMPARE(at(map, QStringLiteral("x")), QPointF(250, 260));
        QVERIFY(at(map, QStringLiteral("y")) != at(map, QStringLiteral("x")));
        QVERIFY(at(map, QStringLiteral("y")) != at(map, QStringLiteral("a")));
    }

    void resetFlowsAgain()
    {
        const QUrl url = QUrl::fromLocalFile(makeFolder());
        FolderLayout layout;
        layout.setUrl(url);
        layout.freeze(abc(), W, CW, CH);
        QVERIFY(layout.arranged());
        layout.reset();
        QVERIFY(!layout.arranged());
        layout.flush();
        FolderLayout again;
        again.setUrl(url);
        QVERIFY(!again.arranged());
    }

    void iconSizeChangeKeepsTheShape()
    {
        FolderLayout layout;
        layout.setUrl(QUrl::fromLocalFile(makeFolder()));
        layout.freeze(abc(), W, CW, CH);
        // Twice the cell: every icon twice as far, so none overlap.
        const QVariantMap map = layout.place(abc(), 2 * W, 2 * CW, 2 * CH);
        QCOMPARE(at(map, QStringLiteral("b")), QPointF(2 * CW, 0));
        QCOMPARE(at(map, QStringLiteral("f")), QPointF(0, 2 * CH));
    }

    void twoWindowsShareOneLayout()
    {
        const QUrl url = QUrl::fromLocalFile(makeFolder());
        FolderLayout one;
        FolderLayout two;
        one.setUrl(url);
        two.setUrl(url);
        one.freeze(abc(), W, CW, CH);
        QSignalSpy spy(&two, &FolderLayout::changed);
        one.moveTo({{QStringLiteral("a"), QPointF(50, 60)}}, false, CW, CH);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(at(two.place(abc(), W, CW, CH), QStringLiteral("a")), QPointF(50, 60));
    }

    void layoutFollowsARenamedFolder()
    {
        const QString path = makeFolder();
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(path));
            layout.freeze(abc(), W, CW, CH);
            layout.moveTo({{QStringLiteral("a"), QPointF(77, 88)}}, false, CW, CH);
        }
        if (FolderLayout::folderId(path).isEmpty()) {
            QSKIP("This filesystem keeps no extended attributes: folders are remembered by path.");
        }
        const QString moved = m_root.path() + QStringLiteral("/elsewhere");
        QVERIFY(QDir().mkpath(moved));
        QVERIFY(QDir().rename(path, moved + QStringLiteral("/renamed")));
        FolderLayout again;
        again.setUrl(QUrl::fromLocalFile(moved + QStringLiteral("/renamed")));
        QVERIFY(again.arranged());
        QCOMPARE(at(again.place(abc(), W, CW, CH), QStringLiteral("a")), QPointF(77, 88));
    }

    void aCopiedFolderGoesItsOwnWay()
    {
        const QString path = makeFolder();
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(path));
            layout.freeze(abc(), W, CW, CH);
            layout.moveTo({{QStringLiteral("a"), QPointF(77, 88)}}, false, CW, CH);
        }
        const QString id = FolderLayout::folderId(path);
        if (id.isEmpty()) {
            QSKIP("This filesystem keeps no extended attributes.");
        }
        // What `cp -a` does: a second folder carrying the same id.
        const QString copy = makeFolder();
        QVERIFY(QProcess::execute(QStringLiteral("setfattr"), {QStringLiteral("-n"), QStringLiteral("user.wonderland.folder-id"), QStringLiteral("-v"), id, copy}) == 0);
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(copy));
            QCOMPARE(at(layout.place(abc(), W, CW, CH), QStringLiteral("a")), QPointF(77, 88));
            layout.moveTo({{QStringLiteral("a"), QPointF(1, 2)}}, false, CW, CH);
        }
        QVERIFY(FolderLayout::folderId(copy) != id);
        FolderLayout original;
        original.setUrl(QUrl::fromLocalFile(path));
        QCOMPARE(at(original.place(abc(), W, CW, CH), QStringLiteral("a")), QPointF(77, 88));
    }

    void aSymlinkIsTheSameFolder()
    {
        const QString path = makeFolder();
        const QString link = m_root.path() + QStringLiteral("/link");
        QVERIFY(QFile::link(path, link));
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(path));
            layout.freeze(abc(), W, CW, CH);
            layout.moveTo({{QStringLiteral("a"), QPointF(77, 88)}}, false, CW, CH);
        }
        const QString id = FolderLayout::folderId(path);
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(link));
            QCOMPARE(at(layout.place(abc(), W, CW, CH), QStringLiteral("a")), QPointF(77, 88));
        }
        QCOMPARE(FolderLayout::folderId(path), id);
    }

    void deletedFilesFreeTheirPlace()
    {
        const QString path = makeFolder();
        {
            FolderLayout layout;
            layout.setUrl(QUrl::fromLocalFile(path));
            layout.freeze({QStringLiteral("a"), QStringLiteral("b")}, W, CW, CH);
        }
        QVERIFY(QFile::remove(path + QStringLiteral("/a")));
        FolderLayout again;
        again.setUrl(QUrl::fromLocalFile(path));
        const QVariantMap map = again.place({QStringLiteral("b"), QStringLiteral("c")}, W, CW, CH);
        QCOMPARE(at(map, QStringLiteral("c")), QPointF(0, 0));
        QCOMPARE(at(map, QStringLiteral("b")), QPointF(CW, 0));
    }

    void virtualPlacesKeepNothing()
    {
        FolderLayout layout;
        layout.setUrl(QUrl());
        layout.setView(QStringLiteral("list"));
        layout.freeze(abc(), W, CW, CH);
        QVERIFY(!layout.arranged());
        QCOMPARE(layout.view(), QString());
    }
};

QTEST_GUILESS_MAIN(TestLayout)
#include "tst_layout.moc"
