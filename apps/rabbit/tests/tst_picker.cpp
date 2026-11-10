#include "picker.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

class TestPicker : public QObject
{
    Q_OBJECT

    static QVariantMap folder(const QString &path)
    {
        return {{QStringLiteral("current_folder"), path.toLocal8Bit() + '\0'}};
    }

private Q_SLOTS:
    void opensInTheFolderTheAppNamed()
    {
        QVariantMap options = folder(QDir::tempPath());
        options.insert(QStringLiteral("multiple"), true);
        options.insert(QStringLiteral("accept_label"), QStringLiteral("_Import"));
        const Picker picker(Picker::Kind::OpenFile, QStringLiteral("Import"), options, nullptr);
        QCOMPARE(picker.folder(), QUrl::fromLocalFile(QDir::tempPath()));
        QCOMPARE(picker.acceptLabel(), QStringLiteral("Import"));
        QVERIFY(picker.multiple());
        QVERIFY(!picker.saving());
        QVERIFY(!picker.folders());
    }

    void aFolderThatIsGoneIsNotAStart()
    {
        const Picker picker(Picker::Kind::OpenFile, QString(), folder(QStringLiteral("/no/such/folder")), nullptr);
        QVERIFY(picker.folder().isEmpty());
    }

    void savingStartsFromTheFileBeingReplaced()
    {
        QVariantMap options;
        options.insert(QStringLiteral("current_file"), QByteArray("/tmp/report.pdf\0", 16));
        const Picker picker(Picker::Kind::SaveFile, QString(), options, nullptr);
        QVERIFY(picker.saving());
        QCOMPARE(picker.name(), QStringLiteral("report.pdf"));
        QCOMPARE(picker.folder(), QUrl::fromLocalFile(QStringLiteral("/tmp")));
    }

    void theFilterChosenIsTheOneApplied()
    {
        Picker picker(Picker::Kind::OpenFile, QString(), {}, nullptr);
        picker.setFilters({{QStringLiteral("Pictures"), {}, {QStringLiteral("image/*")}}, {QStringLiteral("Text"), {QStringLiteral("*.txt")}, {}}}, 1);
        QCOMPARE(picker.filterNames(), QStringList({QStringLiteral("Pictures"), QStringLiteral("Text")}));
        QCOMPARE(picker.globs(), QStringList({QStringLiteral("*.txt")}));
        QSignalSpy changed(&picker, &Picker::filterChanged);
        picker.setFilter(0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(picker.mimes(), QStringList({QStringLiteral("image/*")}));
        QVERIFY(picker.globs().isEmpty());
        picker.setFilter(7);
        QCOMPARE(picker.filter(), 0);
    }

    void answersOnceWithWhatWasChosen()
    {
        Picker picker(Picker::Kind::OpenFile, QString(), {}, nullptr);
        QSignalSpy finished(&picker, &Picker::finished);
        picker.accept({});
        QCOMPARE(finished.count(), 0);
        picker.accept({QUrl::fromLocalFile(QStringLiteral("/tmp/a file.txt"))});
        picker.cancel();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toUInt(), uint(Picker::Chosen));
        QCOMPARE(finished.at(0).at(1).toStringList(), QStringList({QStringLiteral("file:///tmp/a%20file.txt")}));
    }

    void cancellingAnswersWithNothing()
    {
        Picker picker(Picker::Kind::SaveFile, QString(), {}, nullptr);
        QSignalSpy finished(&picker, &Picker::finished);
        picker.cancel();
        QCOMPARE(finished.at(0).at(0).toUInt(), uint(Picker::Cancelled));
        QVERIFY(finished.at(0).at(1).toStringList().isEmpty());
    }

    void severalFilesToSaveLandInTheFolderChosen()
    {
        QVariantMap options;
        options.insert(QStringLiteral("files"), QStringList({QStringLiteral("one.png"), QStringLiteral("sub/two.png")}));
        const Picker picker(Picker::Kind::SaveFiles, QString(), options, nullptr);
        QVERIFY(picker.folders());
        QCOMPARE(picker.answerFor({QUrl::fromLocalFile(QStringLiteral("/tmp/out"))}),
                 QStringList({QStringLiteral("file:///tmp/out/one.png"), QStringLiteral("file:///tmp/out/two.png")}));
    }
};

QTEST_MAIN(TestPicker)
#include "tst_picker.moc"
