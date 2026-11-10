#include "folderlook.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <sys/xattr.h>

class TestFolderLook : public QObject
{
    Q_OBJECT

private:
    // Under the build directory: /tmp is often a tmpfs that keeps no user attributes.
    QTemporaryDir m_dir{QDir::currentPath() + QStringLiteral("/tst_folderlook-XXXXXX")};

    QString folder(const QString &name)
    {
        QDir(m_dir.path()).mkpath(name);
        return m_dir.filePath(name);
    }

    static QByteArray stored(const QString &path)
    {
        QByteArray value(1024, Qt::Uninitialized);
        const ssize_t read = getxattr(QFile::encodeName(path).constData(), "user.wonderland.folder", value.data(), value.size());
        value.truncate(qMax<ssize_t>(read, 0));
        return value;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        const QString probe = folder(QStringLiteral("probe"));
        if (setxattr(QFile::encodeName(probe).constData(), "user.probe", "1", 1, 0) != 0) {
            QSKIP("This disk keeps no user attributes.");
        }
    }

    void attributeRoundTrip()
    {
        const FolderLook symbol{QStringLiteral("ff453a"), QStringLiteral("music"), QString()};
        QCOMPARE(symbol.toAttribute(), QByteArray(R"({"color":"#ff453a","symbol":"music"})"));
        QCOMPARE(FolderLook::fromAttribute(symbol.toAttribute()), symbol);

        const FolderLook emoji{QString(), QString(), QStringLiteral("🎸")};
        QCOMPARE(FolderLook::fromAttribute(emoji.toAttribute()), emoji);
        QVERIFY(FolderLook().toAttribute().isEmpty());
    }

    void attributeFromElsewhereIsCleaned()
    {
        // Whatever another program wrote there never reaches a file name or a URL as it came.
        const FolderLook look = FolderLook::fromAttribute(R"({"color":"red","symbol":"../../etc/passwd","emoji":"hello world"})");
        QVERIFY(look.isEmpty());
        QVERIFY(FolderLook::fromAttribute("not json").isEmpty());
        QCOMPARE(FolderLook::fromAttribute(R"({"color":"#FF453A"})").color, QStringLiteral("ff453a"));
        // A symbol wins over an emoji: a folder wears one or the other.
        const FolderLook both = FolderLook::fromAttribute(R"({"symbol":"star","emoji":"🎸"})");
        QCOMPARE(both.symbol, QStringLiteral("star"));
        QVERIFY(both.emoji.isEmpty());
    }

    void iconNameRoundTrip_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<QString>("symbol");
        QTest::addColumn<QString>("emoji");
        QTest::newRow("colour") << "32d74b" << "" << "";
        QTest::newRow("symbol") << "" << "gamepad-2" << "";
        QTest::newRow("both") << "bf5af2" << "star" << "";
        QTest::newRow("emoji") << "" << "" << "🎸";
        QTest::newRow("family") << "ff9f0a" << "" << "👨‍👩‍👧";
        QTest::newRow("flag") << "" << "" << "🇪🇸";
        QTest::newRow("keycap") << "" << "" << "1️⃣";
    }

    void iconNameRoundTrip()
    {
        QFETCH(QString, color);
        QFETCH(QString, symbol);
        QFETCH(QString, emoji);
        const FolderLook look{color, symbol, emoji};
        const QString name = look.iconName(QStringLiteral("folder-music"));
        // Plain ASCII: the name travels inside an image:// URL.
        for (const QChar c : name) {
            QVERIFY2(c.unicode() < 0x80 && c != QLatin1Char('|') && c != QLatin1Char('/') && c != QLatin1Char('#') && c != QLatin1Char('?'), qPrintable(name));
        }
        QString base;
        QCOMPARE(FolderLook::fromIconName(name, &base), look);
        QCOMPARE(base, QStringLiteral("folder-music"));
    }

    void plainIconNameIsUntouched()
    {
        QCOMPARE(FolderLook().iconName(QStringLiteral("folder")), QStringLiteral("folder"));
        QString base;
        QVERIFY(FolderLook::fromIconName(QStringLiteral("folder-documents"), &base).isEmpty());
        QCOMPARE(base, QStringLiteral("folder-documents"));
    }

    void lookIsKeptOnTheFolder()
    {
        const QString path = folder(QStringLiteral("kept"));
        QSignalSpy changed(FolderLooks::self(), &FolderLooks::changed);
        const FolderLook look{QStringLiteral("0a84ff"), QString(), QStringLiteral("🚀")};
        QVERIFY(FolderLooks::self()->setLook(path, look));
        QCOMPARE(FolderLooks::self()->lookFor(path), look);
        QCOMPARE(stored(path), look.toAttribute());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), path);

        // The same look again changes nothing and announces nothing.
        QVERIFY(FolderLooks::self()->setLook(path, look));
        QCOMPARE(changed.count(), 1);

        // It moves with the folder.
        const QString moved = m_dir.filePath(QStringLiteral("moved"));
        QVERIFY(QDir().rename(path, moved));
        QCOMPARE(FolderLooks::self()->lookFor(moved), look);

        // The plain look leaves no attribute behind.
        QVERIFY(FolderLooks::self()->setLook(moved, FolderLook()));
        QVERIFY(FolderLooks::self()->lookFor(moved).isEmpty());
        QVERIFY(stored(moved).isEmpty());
        QCOMPARE(changed.count(), 2);
    }

    void onlyFoldersTakeALook()
    {
        const QString file = m_dir.filePath(QStringLiteral("note.txt"));
        QFile note(file);
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.close();
        QVERIFY(!FolderLooks::self()->setLook(file, FolderLook{QStringLiteral("ff453a"), QString(), QString()}));
        QVERIFY(!FolderLooks::self()->setLook(m_dir.filePath(QStringLiteral("missing")), FolderLook{QStringLiteral("ff453a"), QString(), QString()}));
    }

    void pictureIsMadeBeforeTheAttribute()
    {
        const QString path = folder(QStringLiteral("pictured"));
        const FolderLook look{QStringLiteral("ffd60a"), QStringLiteral("star"), QString()};
        const QString picture = FolderLooks::picturePath(look.toAttribute());
        QFile::remove(picture);

        int painted = 0;
        bool attributeAlreadyThere = false;
        FolderLooks::self()->setPainter([&](const FolderLook &asked) {
            ++painted;
            attributeAlreadyThere = !stored(path).isEmpty();
            QImage image(8, 8, QImage::Format_ARGB32);
            image.fill(QColor(QLatin1Char('#') + asked.color));
            return image;
        });
        QVERIFY(FolderLooks::self()->setLook(path, look));
        QCOMPARE(painted, 1);
        // Whoever is told by the attribute must find the picture waiting.
        QVERIFY(!attributeAlreadyThere);
        QVERIFY(QFile::exists(picture));
        QCOMPARE(FolderLooks::self()->ensurePicture(path), picture);
        QCOMPARE(painted, 1);

        // Lost (a cleared cache): asked for again, it is painted again.
        QVERIFY(QFile::remove(picture));
        QCOMPARE(FolderLooks::self()->ensurePicture(path), picture);
        QCOMPARE(painted, 2);
        QVERIFY(QFile::exists(picture));

        // A plain folder has no picture.
        QVERIFY(FolderLooks::self()->ensurePicture(folder(QStringLiteral("plain"))).isEmpty());
        FolderLooks::self()->setPainter(nullptr);
    }

    void aTintDressesFoldersWithNoColourOfTheirOwn()
    {
        const QString file = FolderLooks::tintFile();
        QDir().mkpath(QFileInfo(file).absolutePath());
        const auto write = [&file](const QByteArray &json) {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(json);
        };
        FolderLooks *looks = FolderLooks::self();
        QStringList painted;
        looks->setPainter([&painted](const FolderLook &asked) {
            painted << asked.color + QLatin1Char('/') + asked.symbol;
            return QImage(8, 8, QImage::Format_ARGB32);
        });

        const QString plain = folder(QStringLiteral("tint-plain"));
        const QString marked = folder(QStringLiteral("tint-marked"));
        const QString coloured = folder(QStringLiteral("tint-coloured"));
        const FolderLook mark{QString(), QStringLiteral("star"), QString()};
        const FolderLook colour{QStringLiteral("ff453a"), QString(), QString()};
        QVERIFY(looks->setLook(marked, mark));
        QVERIFY(looks->setLook(coloured, colour));
        painted.clear();

        QSignalSpy changed(looks, &FolderLooks::tintChanged);
        write("{\"folder_tint\": \"#32D74B\"}");
        looks->reloadTint();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(looks->tint(), QStringLiteral("32d74b"));

        // Named after the attribute and the tint: the desktop works the same name out.
        const QString tint = QStringLiteral("32d74b");
        const QString plainPicture = FolderLooks::picturePath(QByteArray(), tint);
        QVERIFY(plainPicture != FolderLooks::picturePath(QByteArray()));
        for (const QString &picture : {plainPicture, FolderLooks::picturePath(mark.toAttribute(), tint), FolderLooks::picturePath(colour.toAttribute(), tint)}) {
            QFile::remove(picture);
        }
        QCOMPARE(looks->ensurePicture(plain), plainPicture);
        QCOMPARE(looks->ensurePicture(marked), FolderLooks::picturePath(mark.toAttribute(), tint));
        QCOMPARE(looks->ensurePicture(coloured), FolderLooks::picturePath(colour.toAttribute(), tint));
        // The plain and the marked take the tint; the one with a colour keeps its own.
        QCOMPARE(painted, QStringList({QStringLiteral("32d74b/"), QStringLiteral("32d74b/star"), QStringLiteral("ff453a/")}));

        // What isn't a colour is no tint, and neither is a file that went away.
        write("{\"folder_tint\": \"green\"}");
        looks->reloadTint();
        QVERIFY(looks->tint().isEmpty());
        QVERIFY(looks->ensurePicture(plain).isEmpty());
        QCOMPARE(looks->ensurePicture(marked), FolderLooks::picturePath(mark.toAttribute()));
        QFile::remove(file);
        looks->reloadTint();
        QVERIFY(looks->tint().isEmpty());
        looks->setPainter(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestFolderLook)
#include "tst_folderlook.moc"
