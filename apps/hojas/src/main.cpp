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
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-hojas"));
    QCoreApplication::setApplicationVersion(QStringLiteral(HOJAS_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Hojas"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-hojas"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandHojas/wonderland-hojas.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("La hoja de cálculo de Wonderland."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archivo"), QStringLiteral("Hoja de cálculo que abrir (.xlsx, .ods, .csv, .tsv)."), QStringLiteral("[archivo…]"));
    const QCommandLineOption newOption(QStringLiteral("new"), QStringLiteral("Empieza con una plantilla: blank, budget, invoice, tasks o schedule."), QStringLiteral("plantilla"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QStringLiteral("auto (como el escritorio), light o dark."), QStringLiteral("modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QStringLiteral("Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QStringLiteral("Con --shot: acciones previas, separadas por «;»."), QStringLiteral("acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QStringLiteral("tamaño"));
    parser.addOptions({newOption, appearanceOption, shotOption, doOption, sizeOption});
    parser.process(app);

    const bool testing = parser.isSet(shotOption);
    App hojas(App::defaultDataDir(), testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        hojas.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        hojas.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size(1180, 760);
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        hojas.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    // A window holds one document: the first file opens here, each of the
    // others in a Hojas of its own.
    QStringList files;
    for (const QString &argument : parser.positionalArguments()) {
        const QUrl url = QUrl::fromUserInput(argument, QDir::currentPath(), QUrl::AssumeLocalFile);
        files << (url.isLocalFile() ? url.toLocalFile() : argument);
    }
    QString problem;
    if (!files.isEmpty()) {
        problem = hojas.open(files.at(0));
        for (int i = 1; i < files.size(); ++i) {
            hojas.launch({files.at(i)});
        }
    } else if (parser.isSet(newOption)) {
        hojas.book()->create(parser.value(newOption));
    }
    hojas.setStartProblem(problem);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandHojas", "Main");

    return app.exec();
}
