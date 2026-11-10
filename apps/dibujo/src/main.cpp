#include "app.h"
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
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-dibujo"));
    QCoreApplication::setApplicationVersion(QStringLiteral(DIBUJO_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Dibujo"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-dibujo"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandDibujo/wonderland-dibujo.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("El dibujo de Wonderland: vectores y diagramas en un lienzo sin fin."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivo"), QStringLiteral("Un dibujo .wdib o un SVG que abrir."), QStringLiteral("[archivo]"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QStringLiteral("auto (como el escritorio), light o dark."), QStringLiteral("modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QStringLiteral("Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QStringLiteral("Con --shot: acciones previas, separadas por «;»."), QStringLiteral("acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QStringLiteral("tamaño"));
    parser.addOptions({appearanceOption, shotOption, doOption, sizeOption});
    parser.process(app);

    const bool testing = parser.isSet(shotOption);
    App dibujo(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        dibujo.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        dibujo.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1200, 760);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        dibujo.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    // One drawing per window, one window per process: the first file is this
    // one's, and each of the rest gets a process of its own.
    const QStringList files = parser.positionalArguments();
    for (int i = 0; i < files.size(); ++i) {
        const QUrl url = QUrl::fromUserInput(files.at(i), QDir::currentPath(), QUrl::AssumeLocalFile);
        if (i == 0) {
            dibujo.openPath(url.toLocalFile());
        } else if (!testing) {
            dibujo.openInNewWindow(url.toLocalFile());
        }
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.addImageProvider(QStringLiteral("thumb"), new ThumbProvider(&dibujo));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandDibujo", "Main");
    return app.exec();
}
