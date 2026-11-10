#include "config.h"

#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

static QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

static void write(const QString &path, const QString &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
}

/// Stands in for `driftwm --config F --check-config`: refuses a file holding "REFUSE", warns on "WARN".
static QString fakeCompositor(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("driftwm"));
    write(path, QStringLiteral(
        "#!/bin/sh\n"
        "if grep -q REFUSE \"$2\"; then echo 'driftwm: config error: unknown field'; exit 1; fi\n"
        "if grep -q WARN \"$2\"; then echo '2026-01-01T00:00:00Z  WARN driftwm::config::parse_helpers: config: unknown keysym: WARN'; echo 'Config OK, 1 warning(s)'; fi\n"
        "exit 0\n"));
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

class ConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writesBacksUpAndUndoes()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("driftwm/config.toml"));
        QDir().mkpath(dir.filePath(QStringLiteral("driftwm")));
        write(path, QStringLiteral("[snap]\ngap = 12.0 # px\n"));
        Config config(path, QString());

        QVERIFY(config.set({"snap", "gap"}, 20, QStringLiteral("float")));
        QCOMPARE(read(path), QStringLiteral("[snap]\ngap = 20.0 # px\n"));
        QCOMPARE(read(path + QStringLiteral(".sistema.bak")), QStringLiteral("[snap]\ngap = 12.0 # px\n"));
        QVERIFY(config.set({"snap", "corners"}, false));
        QVERIFY(config.canUndo());

        config.undo();
        QVERIFY(!config.has({"snap", "corners"}));
        config.undo();
        QCOMPARE(read(path), QStringLiteral("[snap]\ngap = 12.0 # px\n"));
        QVERIFY(!config.canUndo());
        // The backup is the file as it was before this session, not before the last change.
        QCOMPARE(read(path + QStringLiteral(".sistema.bak")), QStringLiteral("[snap]\ngap = 12.0 # px\n"));
    }

    void createsAMissingFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("new/config.toml"));
        Config config(path, QString());
        QVERIFY(config.error().isEmpty());
        QVERIFY(config.set({"focus_follows_mouse"}, true));
        QCOMPARE(read(path), QStringLiteral("focus_follows_mouse = true\n"));
    }

    void appliesSeveralChangesAsOne()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(path, QStringLiteral("[keybindings]\n\"mod+j\" = \"quit\"\n"));
        Config config(path, QString());
        QVERIFY(config.apply({
            QVariantMap{{"path", QStringList{"keybindings", "mod+j"}}, {"remove", true}},
            QVariantMap{{"path", QStringList{"keybindings", "mod+k"}}, {"value", "quit"}},
        }));
        QCOMPARE(read(path), QStringLiteral("[keybindings]\n\"mod+k\" = \"quit\"\n"));
        config.undo();
        QCOMPARE(read(path), QStringLiteral("[keybindings]\n\"mod+j\" = \"quit\"\n"));
    }

    void keepsTheFileWhenTheCompositorRefuses()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(path, QStringLiteral("mod_key = \"super\"\n"));
        Config config(path, fakeCompositor(dir));
        QSignalSpy changed(&config, &Config::changed);

        QVERIFY(!config.set({"mod_key"}, QStringLiteral("REFUSE")));
        QCOMPARE(read(path), QStringLiteral("mod_key = \"super\"\n"));
        QCOMPARE(config.get({"mod_key"}).toString(), QStringLiteral("super"));
        QVERIFY(config.error().contains(QLatin1String("unknown field")));
        // Controls re-read on every revision, so a refused value snaps back.
        QCOMPARE(changed.count(), 1);
        QVERIFY(!config.canUndo());

        QVERIFY(!config.set({"keybindings", "mod+WARN"}, QStringLiteral("quit")));
        QCOMPARE(config.error(), QStringLiteral("unknown keysym: WARN"));
        QCOMPARE(read(path), QStringLiteral("mod_key = \"super\"\n"));

        QVERIFY(config.set({"mod_key"}, QStringLiteral("alt")));
        QVERIFY(config.error().isEmpty());
    }

    void toleratesWarningsThatWereAlreadyThere()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(path, QStringLiteral("[keybindings]\n\"mod+WARN\" = \"quit\"\n"));
        Config config(path, fakeCompositor(dir));
        QVERIFY(config.set({"snap", "gap"}, 3.0));
        QVERIFY(read(path).contains(QLatin1String("gap = 3.0")));
    }

    void refusesToEditABrokenFile()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        const QString broken = QStringLiteral("[snap\ngap = 1\n");
        write(path, broken);
        Config config(path, QString());
        QVERIFY(!config.error().isEmpty());
        QVERIFY(!config.set({"snap", "gap"}, 2));
        QCOMPARE(read(path), broken);
    }

    void picksUpOutsideEdits()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(path, QStringLiteral("[snap]\ngap = 1.0\n"));
        Config config(path, QString());
        QVERIFY(config.set({"snap", "gap"}, 2.0));
        QSignalSpy changed(&config, &Config::changed);
        write(path, QStringLiteral("[snap]\ngap = 9.0\n"));
        QVERIFY(changed.wait(3000));
        QCOMPARE(config.get({"snap", "gap"}).toDouble(), 9.0);
        // Undoing over someone else's edit would throw it away.
        QVERIFY(!config.canUndo());
    }

    void writesThroughASymlink()
    {
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("dotfiles.toml"));
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(target, QStringLiteral("[snap]\ngap = 1.0\n"));
        QVERIFY(QFile::link(target, path));
        Config config(path, QString());
        QVERIFY(config.set({"snap", "gap"}, 2.0));
        QVERIFY(QFileInfo(path).isSymLink());
        QCOMPARE(read(target), QStringLiteral("[snap]\ngap = 2.0\n"));
    }

    // Against the compositor itself, where it is installed: its log format is what `check` reads.
    void readsTheRealCompositor()
    {
        const QString driftwm = QStandardPaths::findExecutable(QStringLiteral("driftwm"));
        if (driftwm.isEmpty()) {
            QSKIP("driftwm is not installed");
        }
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("config.toml"));
        write(path, QStringLiteral("[snap]\ngap = 1.0\n"));
        Config config(path, driftwm);

        QVERIFY(config.set({"keybindings", "mod+t"}, QStringLiteral("spawn foot")));
        QVERIFY(!config.set({"keybindings", "mod+zzz"}, QStringLiteral("quit")));
        QVERIFY2(config.error().contains(QLatin1String("unknown keysym: zzz")), qPrintable(config.error()));
        QVERIFY(!config.error().contains(QLatin1Char('\x1b')));
        QVERIFY(!config.set({"keybindings", "mod+y"}, QStringLiteral("bogus-action")));
        QVERIFY(!config.set({"snap", "bogus_key"}, 1));
        QCOMPARE(read(path), QStringLiteral("[snap]\ngap = 1.0\n\n[keybindings]\n\"mod+t\" = \"spawn foot\"\n"));
    }
};

QTEST_GUILESS_MAIN(ConfigTest)
#include "tst_config.moc"
