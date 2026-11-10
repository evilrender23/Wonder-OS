#include "app.h"
#include "mpris.h"
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
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-musica"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MUSICA_VERSION));

    // The texts are written in Spanish; every other language gets the English ones.
    QTranslator translator;
    if (QLocale().language() != QLocale::Spanish
        && translator.load(QStringLiteral(":/i18n/wonderland-musica_en.qm"))) {
        QCoreApplication::installTranslator(&translator);
    }
    QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("main", "Música"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-musica"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandMusica/wonderland-musica.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "El reproductor de música de Wonderland."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QCoreApplication::translate("main", "archivos"), QCoreApplication::translate("main", "Canciones que reproducir, o carpetas que añadir a la biblioteca."), QCoreApplication::translate("main", "[archivos…]"));
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
    App musica(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());
    // One Música: a second launch passes its files to the first and leaves.
    if (!testing && !Mpris::publish(&musica)) {
        Mpris::forward(uris);
        return 0;
    }

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        musica.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        musica.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1120, 720);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        musica.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.addImageProvider(QStringLiteral("cover"), new CoverProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandMusica", "Main");

    musica.open(urls);
    return app.exec();
}
