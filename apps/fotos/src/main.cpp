#include "app.h"
#include "media.h"
#include "providers.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("wonderland"));
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-fotos"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FOTOS_VERSION));

    // Los textos están escritos en español; cualquier otro idioma recibe el inglés.
    QTranslator translator;
    if (QLocale().language() != QLocale::Spanish
        && translator.load(QStringLiteral(":/i18n/wonderland-fotos_en.qm"))) {
        QCoreApplication::installTranslator(&translator);
        // Dates and numbers go with the texts: in English, the way of the user's country.
        Media::setLocale(QLocale().language() == QLocale::English ? QLocale() : QLocale(QLocale::English, QLocale().territory()));
    }

    QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("main", "Fotos"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-fotos"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandFotos/wonderland-fotos.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "La biblioteca de imágenes y vídeos de Wonderland."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QCoreApplication::translate("main", "archivos"), QCoreApplication::translate("main", "Imágenes o vídeos que mostrar, o carpetas que añadir a la biblioteca."), QCoreApplication::translate("main", "[archivos…]"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QCoreApplication::translate("main", "auto (como el escritorio), light o dark."), QCoreApplication::translate("main", "modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QCoreApplication::translate("main", "Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QCoreApplication::translate("main", "Con --shot: acciones previas, separadas por «;»."), QCoreApplication::translate("main", "acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QCoreApplication::translate("main", "Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QCoreApplication::translate("main", "tamaño"));
    parser.addOptions({appearanceOption, shotOption, doOption, sizeOption});
    parser.process(app);

    QList<QUrl> urls;
    QStringList uris;
    for (const QString &argument : parser.positionalArguments()) {
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        urls << url;
        uris << url.toString();
    }

    const bool testing = parser.isSet(shotOption);
    App fotos(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());
    // One Fotos: a second launch passes its files to the first and leaves.
    if (!testing && !fotos.publish()) {
        App::forward(uris);
        return 0;
    }

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        fotos.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        fotos.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1120, 720);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        fotos.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.addImageProvider(QStringLiteral("thumb"), new PictureProvider(PictureProvider::Kind::Thumbnail));
    engine.addImageProvider(QStringLiteral("photo"), new PictureProvider(PictureProvider::Kind::Photo));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandFotos", "Main");

    fotos.open(urls);
    return app.exec();
}
