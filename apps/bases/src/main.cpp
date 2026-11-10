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
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-bases"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BASES_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Bases"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-bases"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandBases/wonderland-bases.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Las bases de datos de Wonderland: tablas, consultas, formularios e informes sobre SQLite."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivo"), QStringLiteral("La base de datos SQLite que abrir."), QStringLiteral("[archivo]"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QStringLiteral("auto (como el escritorio), light o dark."), QStringLiteral("modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QStringLiteral("Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QStringLiteral("Con --shot: acciones previas, separadas por «;»."), QStringLiteral("acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QStringLiteral("tamaño"));
    const QCommandLineOption newOption(QStringLiteral("new"), QStringLiteral("Crea una base de datos nueva desde una plantilla (o «blank») y la abre."), QStringLiteral("plantilla"));
    parser.addOptions({appearanceOption, shotOption, doOption, sizeOption, newOption});
    parser.process(app);

    QList<QUrl> urls;
    for (const QString &argument : parser.positionalArguments()) {
        urls << QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
    }

    // Every window is its own process with its own file: SQLite arbitrates
    // between two that open the same one.
    const bool testing = parser.isSet(shotOption);
    App bases(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        bases.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        bases.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1180, 740);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        bases.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    if (parser.isSet(newOption)) {
        const QString id = parser.value(newOption);
        const bool blank = id.isEmpty() || id == QLatin1String("blank");
        QString name = QStringLiteral("Sin título");
        for (const QVariant &entry : bases.templates()) {
            if (!blank && entry.toMap().value(QStringLiteral("id")).toString() == id) {
                name = entry.toMap().value(QStringLiteral("name")).toString();
            }
        }
        bases.createPath(bases.freePath(name), blank ? QString() : id);
    } else {
        bases.open(urls);
    }
    engine.loadFromModule("WonderlandBases", "Main");

    return app.exec();
}
