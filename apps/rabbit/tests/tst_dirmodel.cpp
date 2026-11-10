#include "dirmodel.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TestDirModel : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    void touch(const QString &name, int bytes = 0)
    {
        QFile file(m_dir.path() + QLatin1Char('/') + name);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(bytes, 'x'));
    }
    static void load(DirModel &model, const QUrl &url)
    {
        model.setUrl(url);
        QTRY_VERIFY_WITH_TIMEOUT(!model.loading() && model.count() > 0, 10000);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        touch(QStringLiteral("file10.txt"), 30);
        touch(QStringLiteral("file2.txt"), 2000);
        touch(QStringLiteral("Alpha.md"), 10);
        touch(QStringLiteral(".hidden"));
        QVERIFY(QDir(m_dir.path()).mkdir(QStringLiteral("beta")));
    }

    void listsNaturallySorted()
    {
        DirModel model;
        load(model, QUrl::fromLocalFile(m_dir.path()));
        // Numbers count as numbers, case doesn't, folders mix in, hidden files stay out.
        QCOMPARE(model.fileNames(), QStringList({QStringLiteral("Alpha.md"), QStringLiteral("beta"), QStringLiteral("file2.txt"), QStringLiteral("file10.txt")}));
        QCOMPARE(model.rowForFileName(QStringLiteral("beta")), 1);
        QVERIFY(model.get(1).value(QStringLiteral("isDir")).toBool());
    }

    void sortsBySize()
    {
        DirModel model;
        model.setSortKey(DirModel::BySize);
        model.setAscending(false);
        load(model, QUrl::fromLocalFile(m_dir.path()));
        QCOMPARE(model.fileNames(), QStringList({QStringLiteral("file2.txt"), QStringLiteral("file10.txt"), QStringLiteral("Alpha.md"), QStringLiteral("beta")}));
    }

    void aFilterKeepsFoldersAndTheFilesThatFit()
    {
        DirModel model;
        model.setGlobs({QStringLiteral("*.MD")});
        load(model, QUrl::fromLocalFile(m_dir.path()));
        QCOMPARE(model.fileNames(), QStringList({QStringLiteral("Alpha.md"), QStringLiteral("beta")}));
        model.setGlobs({});
        model.setMimes({QStringLiteral("text/plain")});
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 10000);
        model.setMimes({QStringLiteral("image/*")});
        QTRY_COMPARE_WITH_TIMEOUT(model.fileNames(), QStringList({QStringLiteral("beta")}), 10000);
    }

    void showsHidden()
    {
        DirModel model;
        model.setShowHidden(true);
        load(model, QUrl::fromLocalFile(m_dir.path()));
        QVERIFY(model.fileNames().contains(QStringLiteral(".hidden")));
    }

    void selection()
    {
        DirModel model;
        load(model, QUrl::fromLocalFile(m_dir.path()));
        model.click(0, Qt::NoModifier);
        QCOMPARE(model.selectedRows(), QList<int>({0}));
        model.click(2, Qt::ShiftModifier);
        QCOMPARE(model.selectedRows(), QList<int>({0, 1, 2}));
        model.click(1, Qt::ControlModifier);
        QCOMPARE(model.selectedRows(), QList<int>({0, 2}));
        QCOMPARE(model.selectedCount(), 2);
        model.click(3, Qt::NoModifier);
        QCOMPARE(model.selectedRows(), QList<int>({3}));
        model.selectRows({1}, {3});
        QCOMPARE(model.selectedRows(), QList<int>({1, 3}));
        model.selectAll();
        QCOMPARE(model.selectedCount(), 4);
        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
    }

    void typeAhead()
    {
        DirModel model;
        load(model, QUrl::fromLocalFile(m_dir.path()));
        QCOMPARE(model.typeAhead(QStringLiteral("fi")), 2);
        QCOMPARE(model.typeAhead(QStringLiteral("B")), 1);
        QCOMPARE(model.typeAhead(QStringLiteral("zz")), -1);
    }

    void followsTheDisk()
    {
        DirModel model;
        load(model, QUrl::fromLocalFile(m_dir.path()));
        const QUrl added = QUrl::fromLocalFile(m_dir.path() + QStringLiteral("/gamma.txt"));
        QSignalSpy revealed(&model, &DirModel::revealed);
        model.selectUrls({added});
        touch(QStringLiteral("gamma.txt"));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 5, 10000);
        // Asked for before it existed, selected as it arrives.
        QTRY_COMPARE_WITH_TIMEOUT(revealed.count(), 1, 5000);
        QCOMPARE(model.selectedUrls(), QList<QUrl>({added}));
        // Straight away: a file that comes and goes within a moment must not stay behind.
        QVERIFY(QFile::remove(added.toLocalFile()));
        QTRY_COMPARE_WITH_TIMEOUT(model.count(), 4, 10000);
    }

    void theFolderIsNotOneOfItsItems()
    {
        // With the parent folder open as well (another window, or the desktop), deleting inside
        // a folder makes the lister report the folder itself as refreshed: it must not turn up
        // among its own items as ".".
        const QString inner = m_dir.path() + QStringLiteral("/inner");
        QVERIFY(QDir().mkpath(inner + QStringLiteral("/doomed")));
        touch(QStringLiteral("inner/kept.txt"));
        touch(QStringLiteral("inner/doomed.txt"));
        DirModel parent;
        load(parent, QUrl::fromLocalFile(m_dir.path()));
        DirModel model;
        load(model, QUrl::fromLocalFile(inner));
        QCOMPARE(model.count(), 3);
        QTest::qWait(1500);
        QVERIFY(QDir(inner).rmdir(QStringLiteral("doomed")));
        QTest::qWait(1200);
        QVERIFY(QFile::remove(inner + QStringLiteral("/doomed.txt")));
        QTRY_VERIFY_WITH_TIMEOUT(!model.fileNames().contains(QStringLiteral("doomed.txt")), 10000);
        QTest::qWait(2500);
        QCOMPARE(model.fileNames(), QStringList({QStringLiteral("kept.txt")}));
        QVERIFY(QDir(inner).removeRecursively());
    }
};

QTEST_MAIN(TestDirModel)
#include "tst_dirmodel.moc"
