#include "app.h"
#include "fileops.h"
#include "folderlook.h"
#include "opsreport.h"
#include "providers.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTextStream>
#include <QTranslator>

static const QString service = QStringLiteral("org.wonderland.Finder");
static const QString fileManagerPath = QStringLiteral("/org/freedesktop/FileManager1");
static const QString fileManagerInterface = QStringLiteral("org.freedesktop.FileManager1");
static const QString lookInterface = QStringLiteral("org.wonderland.Finder.QuickLook");

static QUrl urlFromArgument(const QString &argument)
{
    return QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("wonderland"));
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-finder"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FINDER_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Finder"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-finder"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandFinder/icons/app.svg")));

    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("wonderland-finder"), QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&translator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "The Wonderlands OS file manager."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("folders"), QCoreApplication::translate("main", "Folders to open, or files to reveal."), QStringLiteral("[folders…]"));
    const QCommandLineOption viewOption(QStringLiteral("view"), QCoreApplication::translate("main", "Start in this view: icons, list or gallery."), QStringLiteral("view"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QCoreApplication::translate("main", "auto (follow the desktop), light or dark."), QStringLiteral("mode"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QCoreApplication::translate("main", "Save a picture of the window to this file and quit (for tests)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QCoreApplication::translate("main", "With --shot: actions to perform first, separated by “;”."), QStringLiteral("actions"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QCoreApplication::translate("main", "With --shot: the window size, as WIDTHxHEIGHT."), QStringLiteral("size"));
    const QCommandLineOption serviceOption(QStringLiteral("service"), QCoreApplication::translate("main", "Start without a window and wait to be asked for one (how the session starts it for “Show in folder”)."));
    const QCommandLineOption lookOption(QStringLiteral("look"), QCoreApplication::translate("main", "Quick Look: preview the given files, or close the preview if it is showing them."));
    const QCommandLineOption lookUpdateOption(QStringLiteral("look-update"), QCoreApplication::translate("main", "Quick Look: if a preview is open, show the given files in it."));
    const QCommandLineOption customizeOption(QStringLiteral("customize"), QCoreApplication::translate("main", "Open “Customize Folder” on the folders instead of showing them."));
    const QCommandLineOption folderIconOption(QStringLiteral("folder-icon"), QCoreApplication::translate("main", "Print the picture of each customized folder, making it if needed, and quit."));
    parser.addOptions({viewOption, appearanceOption, shotOption, doOption, sizeOption, serviceOption, lookOption, lookUpdateOption, customizeOption, folderIconOption});
    parser.process(app);

    FolderLooks::self()->setPainter([](const FolderLook &look) {
        return IconProvider::folderImage(look, 256);
    });
    if (parser.isSet(folderIconOption)) {
        QTextStream out(stdout);
        for (const QString &argument : parser.positionalArguments()) {
            out << FolderLooks::self()->ensurePicture(urlFromArgument(argument).toLocalFile()) << '\n';
        }
        return 0;
    }

    const bool looking = parser.isSet(lookOption) || parser.isSet(lookUpdateOption);
    QList<QUrl> folders;
    QList<QUrl> reveal;
    QList<QUrl> look;
    for (const QString &argument : parser.positionalArguments()) {
        const QUrl url = urlFromArgument(argument);
        if (looking) {
            look << url;
        } else if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) {
            reveal << url;
        } else {
            folders << url;
        }
    }

    const bool testing = parser.isSet(shotOption);
    const bool customize = parser.isSet(customizeOption);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!testing && bus.isConnected() && !bus.registerService(service)) {
        if (looking) {
            QStringList items;
            for (const QUrl &url : std::as_const(look)) {
                items << url.toString();
            }
            if (!items.isEmpty()) {
                QDBusInterface preview(service, fileManagerPath, lookInterface, bus);
                preview.call(parser.isSet(lookUpdateOption) ? QStringLiteral("Update") : QStringLiteral("Toggle"), items);
            }
            return 0;
        }
        // Already running: hand the folders to that instance, as one app with many windows.
        QDBusInterface running(service, fileManagerPath, fileManagerInterface, bus);
        QStringList uris;
        for (const QUrl &url : std::as_const(folders)) {
            uris << url.toString();
        }
        if (parser.isSet(serviceOption) && uris.isEmpty() && reveal.isEmpty()) {
            return 0;
        }
        if (customize) {
            QDBusInterface own(service, fileManagerPath, service, bus);
            for (const QString &uri : std::as_const(uris)) {
                own.call(QStringLiteral("CustomizeFolder"), uri);
            }
            return 0;
        }
        if (!uris.isEmpty() || reveal.isEmpty()) {
            running.call(QStringLiteral("ShowFolders"), uris, QString());
        }
        if (!reveal.isEmpty()) {
            QStringList items;
            for (const QUrl &url : std::as_const(reveal)) {
                items << url.toString();
            }
            running.call(QStringLiteral("ShowItems"), items, QString());
        }
        return 0;
    }

    if (looking && (look.isEmpty() || parser.isSet(lookUpdateOption))) {
        // No preview is open to update, and none to open.
        return 0;
    }

    App finder(nullptr);
    finder.setStartView(parser.value(viewOption));
    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        finder.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        finder.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1000, 620);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        finder.setTest(QFileInfo(parser.value(shotOption)).absoluteFilePath(), parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
        finder.setTestLook(look);
    } else {
        finder.ops()->reportTo(OpsReport::defaultPath());
    }
    if (!testing && bus.isConnected()) {
        new FileManagerAdaptor(&finder);
        new FinderAdaptor(&finder);
        new LookAdaptor(&finder);
        bus.registerObject(fileManagerPath, &finder);
        // Other apps' "Show in folder" looks for this name; a running Dolphin may hold it already.
        // Queued: if that Dolphin quits, the name comes to us.
        bus.interface()->registerService(QStringLiteral("org.freedesktop.FileManager1"), QDBusConnectionInterface::QueueService, QDBusConnectionInterface::DontAllowReplacement);
    }

    // Outside Plasma, Qt starts with no icon theme at all.
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor")) {
        QIcon::setThemeName(QStringLiteral("breeze"));
    }
    QIcon::setFallbackThemeName(QStringLiteral("Adwaita"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandFinder", "Main");

    if (customize) {
        // Only the small window: asked for from the desktop, there is no folder to show.
        if (testing && !folders.isEmpty()) {
            finder.setTestCustomize(folders.constFirst());
        }
        for (const QUrl &url : std::as_const(folders)) {
            finder.customizeFolder(url);
        }
        if (folders.isEmpty()) {
            return 0;
        }
        return app.exec();
    }
    for (const QUrl &url : std::as_const(folders)) {
        finder.requestWindow(url);
    }
    QHash<QUrl, QList<QUrl>> byFolder;
    for (const QUrl &url : std::as_const(reveal)) {
        byFolder[url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash)] << url;
    }
    for (auto it = byFolder.begin(); it != byFolder.end(); ++it) {
        finder.requestWindow(it.key(), it.value());
    }
    if (looking) {
        // Only the preview: the app ends when it is closed, unless a window was asked for meanwhile.
        if (!testing) {
            finder.quickLook(look);
        }
    } else if (folders.isEmpty() && reveal.isEmpty() && !parser.isSet(serviceOption)) {
        finder.requestWindow(finder.homeUrl());
    }
    return app.exec();
}
