#include "app.h"
#include "providers.h"
#include "thumbnails.h"

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
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-textos"));
    QCoreApplication::setApplicationVersion(QStringLiteral(TEXTOS_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Textos"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-textos"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandTextos/wonderland-textos.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("El procesador de textos de Wonderland."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivos"), QStringLiteral("Documentos que abrir, cada uno en su ventana."), QStringLiteral("[archivos…]"));
    const QCommandLineOption newOption(QStringLiteral("new"), QStringLiteral("Empieza con un documento en blanco."));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QStringLiteral("auto (como el escritorio), light o dark."), QStringLiteral("modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QStringLiteral("Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QStringLiteral("Con --shot: acciones previas, separadas por «;»."), QStringLiteral("acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QStringLiteral("tamaño"));
    parser.addOptions({newOption, appearanceOption, shotOption, doOption, sizeOption});
    parser.process(app);

    QStringList files;
    for (const QString &argument : parser.positionalArguments()) {
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        files << (url.isLocalFile() ? url.toLocalFile() : argument);
    }

    const bool testing = parser.isSet(shotOption);
    App textos(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        textos.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        textos.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1180, 760);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        textos.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    // One document a window, one window a process: the first file is this
    // one's, the others get a Textos each.
    if (!files.isEmpty()) {
        textos.open(files.first());
        for (int i = 1; i < files.size(); ++i) {
            textos.openWindow(files.at(i));
        }
    } else if (parser.isSet(newOption)) {
        textos.start(QStringLiteral("blank"));
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.addImageProvider(QStringLiteral("template"), new TemplateProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandTextos", "Main");

    return app.exec();
}
