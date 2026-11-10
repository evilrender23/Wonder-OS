#include "app.h"
#include "mpris.h"
#include "providers.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("wonderland"));
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-peliculas"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PELICULAS_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Películas"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-peliculas"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandPeliculas/wonderland-peliculas.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("El reproductor de vídeo de Wonderland."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivos"), QStringLiteral("Vídeos, carpetas o un archivo de subtítulos que abrir."), QStringLiteral("[archivos…]"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QStringLiteral("auto (como el escritorio), light o dark."), QStringLiteral("modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QStringLiteral("Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QStringLiteral("Con --shot: acciones previas, separadas por «;»."), QStringLiteral("acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QStringLiteral("tamaño"));
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
    App peliculas(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());
    // One Películas: a second launch passes its files to the first and leaves.
    if (!testing && !Mpris::publish(&peliculas)) {
        Mpris::forward(uris);
        return 0;
    }

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        peliculas.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        peliculas.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1040, 640);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        peliculas.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandPeliculas", "Main");

    peliculas.open(urls);
    return app.exec();
}
