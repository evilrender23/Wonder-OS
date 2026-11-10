#include "albums.h"
#include "library.h"
#include "photomodel.h"
#include "sample.h"
#include "store.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

/// A library of a few pictures across three months, in a folder of its own.
struct Fixture {
    QTemporaryDir pictures;
    QTemporaryDir data;
    QString root;

    Fixture()
    {
        root = pictures.filePath(QStringLiteral("Imágenes"));
        add(QStringLiteral("Viajes/IMG_20240131_101500.jpg"));
        add(QStringLiteral("Viajes/IMG_20240130_090000.jpg"));
        add(QStringLiteral("Viajes/IMG_20240105_120000.jpg"));
        add(QStringLiteral("IMG_20231224_200000.jpg"));
        add(QStringLiteral("Screenshots/Screenshot_20231201-080000.png"));
        add(QStringLiteral("Cumpleaños/IMG_20220610_170000.jpg"));
        // A library of no folders: without this it would start with the real
        // Pictures and Videos of whoever runs the tests.
        Store(settings()).set(QStringLiteral("folders"), QJsonArray());
    }
    QString add(const QString &relative)
    {
        return Sample::picture(root + QLatin1Char('/') + relative);
    }
    QString path(const QString &relative) const
    {
        return root + QLatin1Char('/') + relative;
    }
    QString settings() const { return data.filePath(QStringLiteral("settings.json")); }
    QString cache() const { return data.filePath(QStringLiteral("library.json")); }
};

static QStringList names(const QStringList &paths)
{
    QStringList out;
    for (const QString &path : paths) {
        out << QFileInfo(path).fileName();
    }
    return out;
}

class TestLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scans()
    {
        Fixture f;
        Store store(f.settings());
        Library library(&store, f.cache(), false);
        library.addFolder(QUrl::fromLocalFile(f.root));
        library.waitForScan();
        QCoreApplication::processEvents();

        QCOMPARE(library.folders(), QStringList{f.root});
        QCOMPARE(library.count(), 6);
        QCOMPARE(library.photoCount(), 6);
        QCOMPARE(library.videoCount(), 0);
        QCOMPARE(library.screenshotCount(), 1);
        // Newest first.
        QCOMPARE(library.items().first().name(), QStringLiteral("IMG_20240131_101500.jpg"));
        QCOMPARE(library.items().last().name(), QStringLiteral("IMG_20220610_170000.jpg"));
        QVERIFY(library.contains(f.path(QStringLiteral("IMG_20231224_200000.jpg"))));
        QVERIFY(library.item(f.path(QStringLiteral("nada.jpg"))).isEmpty());
        QCOMPARE(library.item(f.path(QStringLiteral("IMG_20231224_200000.jpg"))).value(QStringLiteral("width")).toInt(), 64);
    }

    void skipsWhatIsNotAPicture()
    {
        Fixture f;
        QFile note(f.path(QStringLiteral("notas.txt")));
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("hola");
        note.close();
        // An empty file with a picture's name, a hidden folder, a folder that asks to be left alone.
        QFile empty(f.path(QStringLiteral("vacio.jpg")));
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        f.add(QStringLiteral(".cache/oculta.jpg"));
        f.add(QStringLiteral("Privado/secreta.jpg"));
        QFile nomedia(f.path(QStringLiteral("Privado/.nomedia")));
        QVERIFY(nomedia.open(QIODevice::WriteOnly));
        nomedia.close();
        f.add(QStringLiteral("Privado/Dentro/tambien.jpg"));
        // A link back up: walked once, not forever.
        QVERIFY(QFile::link(f.root, f.path(QStringLiteral("Viajes/arriba"))));

        const Library::Scan scan = Library::scan({f.root}, {});
        QCOMPARE(scan.items.size(), 6);
        for (const MediaItem &item : scan.items) {
            QVERIFY2(!item.path.contains(QStringLiteral("Privado")) && !item.path.contains(QStringLiteral(".cache")), qPrintable(item.path));
        }
        QVERIFY(scan.dirs.contains(f.root));
        QVERIFY(!scan.dirs.contains(f.path(QStringLiteral("Privado"))));
    }

    void keepsWhatItKnows()
    {
        Fixture f;
        const Library::Scan first = Library::scan({f.root}, {});
        QHash<QString, MediaItem> known;
        for (MediaItem item : first.items) {
            // A mark no reading of the file would leave: kept means not read again.
            item.camera = QStringLiteral("de memoria");
            known.insert(item.path, item);
        }
        const QString changed = f.path(QStringLiteral("IMG_20231224_200000.jpg"));
        Sample::picture(changed, 100, 80);
        Sample::dated(changed, QDateTime::currentDateTime().addSecs(10));

        const Library::Scan second = Library::scan({f.root}, known);
        QCOMPARE(second.items.size(), 6);
        for (const MediaItem &item : second.items) {
            if (item.path == changed) {
                QCOMPARE(item.camera, QString());
                QCOMPARE(item.width, 100);
            } else {
                QCOMPARE(item.camera, QStringLiteral("de memoria"));
            }
        }
    }

    void remembers()
    {
        Fixture f;
        {
            Store store(f.settings());
            Library library(&store, f.cache(), false);
            library.addFolder(QUrl::fromLocalFile(f.root));
            library.waitForScan();
            QCoreApplication::processEvents();
            QCOMPARE(library.count(), 6);
        }
        // The next run shows the library before reading the disk.
        Store store(f.settings());
        Library library(&store, f.cache(), false);
        QCOMPARE(library.folders(), QStringList{f.root});
        QCOMPARE(library.count(), 6);

        // What is gone from disk goes at the next reading; what is new arrives.
        QVERIFY(QFile::remove(f.path(QStringLiteral("IMG_20231224_200000.jpg"))));
        f.add(QStringLiteral("Viajes/IMG_20240201_100000.jpg"));
        QSignalSpy spy(&library, &Library::changed);
        library.rescan();
        library.waitForScan();
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(library.count(), 6);
        QCOMPARE(library.items().first().name(), QStringLiteral("IMG_20240201_100000.jpg"));
        QVERIFY(!library.contains(f.path(QStringLiteral("IMG_20231224_200000.jpg"))));

        // Nothing new: nothing said.
        library.rescan();
        library.waitForScan();
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);
    }

    void folders()
    {
        Fixture f;
        Store store(f.settings());
        Library library(&store, f.cache(), false);
        // A folder inside one already in adds nothing; the one that holds others replaces them.
        library.addFolder(QUrl::fromLocalFile(f.path(QStringLiteral("Viajes"))));
        library.addFolder(QUrl::fromLocalFile(f.path(QStringLiteral("Viajes"))));
        QCOMPARE(library.folders(), QStringList{f.path(QStringLiteral("Viajes"))});
        library.waitForScan();
        QCoreApplication::processEvents();
        QCOMPARE(library.count(), 3);
        library.addFolder(QUrl::fromLocalFile(f.root));
        QCOMPARE(library.folders(), QStringList{f.root});
        library.addFolder(QUrl::fromLocalFile(f.path(QStringLiteral("Cumpleaños"))));
        QCOMPARE(library.folders(), QStringList{f.root});
        library.addFolder(QUrl::fromLocalFile(f.path(QStringLiteral("no existe"))));
        QCOMPARE(library.folders(), QStringList{f.root});
        library.waitForScan();
        QCoreApplication::processEvents();
        QCOMPARE(library.count(), 6);

        QCOMPARE(library.placeName(f.root), QStringLiteral("Imágenes"));
        QCOMPARE(library.placeName(f.path(QStringLiteral("Viajes"))), QStringLiteral("Imágenes/Viajes"));
        const QVariantList places = library.places();
        QCOMPARE(places.size(), 4);
        QCOMPARE(places.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Imágenes"));
        QCOMPARE(places.at(0).toMap().value(QStringLiteral("count")).toInt(), 1);
        QVariantMap trips;
        for (const QVariant &place : places) {
            if (place.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Imágenes/Viajes")) {
                trips = place.toMap();
            }
        }
        QCOMPARE(trips.value(QStringLiteral("count")).toInt(), 3);
        // Its cover is its newest picture.
        QCOMPARE(QFileInfo(trips.value(QStringLiteral("cover")).toString()).fileName(), QStringLiteral("IMG_20240131_101500.jpg"));

        // Taken out, its pictures leave at once and its files stay.
        library.removeFolder(f.root);
        QCOMPARE(library.count(), 0);
        QVERIFY(QFileInfo::exists(f.path(QStringLiteral("IMG_20231224_200000.jpg"))));
        library.waitForScan();
        // The choice of «no folders» is kept: the defaults do not come back.
        Store again(f.settings());
        Library reopened(&again, f.cache(), false);
        QVERIFY(reopened.folders().isEmpty());
    }

    void forgetsAndRefreshes()
    {
        Fixture f;
        Store store(f.settings());
        Library library(&store, f.cache(), false);
        library.addFolder(QUrl::fromLocalFile(f.root));
        library.waitForScan();
        QCoreApplication::processEvents();

        library.forget({f.path(QStringLiteral("Viajes/IMG_20240131_101500.jpg")), f.path(QStringLiteral("nada.jpg"))});
        QCOMPARE(library.count(), 5);
        QCOMPARE(library.items().first().name(), QStringLiteral("IMG_20240130_090000.jpg"));

        // Turned: read again, in the same place in time.
        const QString turned = f.path(QStringLiteral("Viajes/IMG_20240105_120000.jpg"));
        const qint64 taken = library.find(turned).taken;
        QVERIFY(Metadata_rotate(turned));
        QSignalSpy spy(&library, &Library::itemChanged);
        library.refresh(turned);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(library.find(turned).taken, taken);
        QCOMPARE(library.find(turned).width, 48);
        QCOMPARE(library.find(turned).height, 64);
    }

    void model()
    {
        Fixture f;
        Store store(f.settings());
        Library library(&store, f.cache(), false);
        Albums albums(f.data.filePath(QStringLiteral("albums.json")));
        library.addFolder(QUrl::fromLocalFile(f.root));
        library.waitForScan();
        QCoreApplication::processEvents();

        PhotoModel model;
        model.setLibrary(&library);
        model.setAlbums(&albums);
        model.setColumns(2);
        QCOMPARE(model.count(), 6);
        QCOMPARE(model.paths().size(), 6);

        const auto shape = [&model] {
            // «H:title» for a heading, the number of pictures for a line.
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row) {
                const QModelIndex index = model.index(row);
                out << (model.data(index, PhotoModel::HeaderRole).toBool() ? QStringLiteral("H:") + model.data(index, PhotoModel::TitleRole).toString()
                                                                           : QString::number(model.data(index, PhotoModel::CellsRole).toList().size()));
            }
            return out;
        };
        QCOMPARE(shape(), (QStringList{QStringLiteral("H:Enero de 2024"), QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("H:Diciembre de 2023"), QStringLiteral("2"),
                                       QStringLiteral("H:Junio de 2022"), QStringLiteral("1")}));
        QCOMPARE(model.data(model.index(0), PhotoModel::DetailRole).toString(), QStringLiteral("3 fotos"));
        QCOMPARE(model.data(model.index(5), PhotoModel::DetailRole).toString(), QStringLiteral("1 foto"));

        model.setGrouping(QStringLiteral("year"));
        QCOMPARE(shape(), (QStringList{QStringLiteral("H:2024"), QStringLiteral("2"), QStringLiteral("1"), QStringLiteral("H:2023"), QStringLiteral("2"), QStringLiteral("H:2022"),
                                       QStringLiteral("1")}));
        model.setGrouping(QStringLiteral("none"));
        QCOMPARE(shape(), (QStringList{QStringLiteral("2"), QStringLiteral("2"), QStringLiteral("2")}));
        model.setColumns(4);
        QCOMPARE(shape(), (QStringList{QStringLiteral("4"), QStringLiteral("2")}));
        model.setGrouping(QStringLiteral("month"));
        model.setColumns(2);

        // A cell says which picture of the page it is.
        const QVariantList cells = model.data(model.index(2), PhotoModel::CellsRole).toList();
        QCOMPARE(cells.first().toMap().value(QStringLiteral("index")).toInt(), 2);
        QCOMPARE(QFileInfo(cells.first().toMap().value(QStringLiteral("path")).toString()).fileName(), QStringLiteral("IMG_20240105_120000.jpg"));
        QCOMPARE(model.rowOf(0), 1);
        QCOMPARE(model.rowOf(2), 2);
        QCOMPARE(model.rowOf(5), 6);
        QCOMPARE(model.rowOf(6), -1);
        QCOMPARE(model.indexOf(f.path(QStringLiteral("IMG_20231224_200000.jpg"))), 3);
        QCOMPARE(model.range(4, 2).size(), 3);
        QCOMPARE(model.range(4, 2), model.range(2, 4));
        QCOMPARE(model.range(4, 99).size(), 2);
        QCOMPARE(names(model.group(3)), (QStringList{QStringLiteral("IMG_20231224_200000.jpg"), QStringLiteral("Screenshot_20231201-080000.png")}));
        QVERIFY(model.group(1).isEmpty());

        model.setMode(QStringLiteral("screenshots"));
        QCOMPARE(names(model.paths()), QStringList{QStringLiteral("Screenshot_20231201-080000.png")});
        model.setMode(QStringLiteral("videos"));
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.rowCount(), 0);
        model.setMode(QStringLiteral("folder"));
        model.setKey(f.path(QStringLiteral("Viajes")));
        QCOMPARE(model.count(), 3);
        // Of that folder, not of the ones inside it or around it.
        model.setKey(f.root);
        QCOMPARE(names(model.paths()), QStringList{QStringLiteral("IMG_20231224_200000.jpg")});

        // Words: of the name, of the folder, of the date; without accents; all of them.
        model.setMode(QStringLiteral("library"));
        model.setKey(QString());
        model.setQuery(QStringLiteral("cumpleanos"));
        QCOMPARE(names(model.paths()), QStringList{QStringLiteral("IMG_20220610_170000.jpg")});
        model.setQuery(QStringLiteral("enero 2024"));
        QCOMPARE(model.count(), 3);
        model.setQuery(QStringLiteral("viajes 0105"));
        QCOMPARE(names(model.paths()), QStringList{QStringLiteral("IMG_20240105_120000.jpg")});
        model.setQuery(QStringLiteral("captura"));
        QCOMPARE(model.count(), 1);
        model.setQuery(QStringLiteral("nada de nada"));
        QCOMPARE(model.count(), 0);
        model.setQuery(QString());

        // Favourites by date, an album in its own order and never grouped.
        albums.setFavorite({f.path(QStringLiteral("Cumpleaños/IMG_20220610_170000.jpg")), f.path(QStringLiteral("Viajes/IMG_20240130_090000.jpg"))}, true);
        model.setMode(QStringLiteral("favorites"));
        QCOMPARE(names(model.paths()), (QStringList{QStringLiteral("IMG_20240130_090000.jpg"), QStringLiteral("IMG_20220610_170000.jpg")}));
        const QString id = albums.create(QStringLiteral("Lo mejor"), {f.path(QStringLiteral("Cumpleaños/IMG_20220610_170000.jpg")), f.path(QStringLiteral("no está.jpg")),
                                                                     f.path(QStringLiteral("Viajes/IMG_20240131_101500.jpg"))});
        model.setMode(QStringLiteral("album"));
        model.setKey(id);
        // What is not on disk waits out of sight.
        QCOMPARE(names(model.paths()), (QStringList{QStringLiteral("IMG_20220610_170000.jpg"), QStringLiteral("IMG_20240131_101500.jpg")}));
        QCOMPARE(shape(), QStringList{QStringLiteral("2")});

        // The page follows its album and the library.
        QSignalSpy spy(&model, &PhotoModel::contentChanged);
        albums.add(id, {f.path(QStringLiteral("IMG_20231224_200000.jpg"))});
        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.count(), 3);
        library.forget({f.path(QStringLiteral("IMG_20231224_200000.jpg"))});
        QCOMPARE(model.count(), 2);
    }

private:
    static bool Metadata_rotate(const QString &path);
};

#include "metadata.h"
bool TestLibrary::Metadata_rotate(const QString &path)
{
    return Metadata::rotate(path, true);
}

QTEST_MAIN(TestLibrary)
#include "tst_library.moc"
