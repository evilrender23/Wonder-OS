#include "app.h"
#include "bluetooth.h"
#include "bluezbackend.h"
#include "fakebackend.h"
#include "providers.h"

#include <QCommandLineParser>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("wonderland"));
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-bluetooth"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BLUETOOTH_VERSION));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Bluetooth"));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-bluetooth"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandBluetooth/wonderland-bluetooth.svg")));

    // The texts are written in Spanish; any other language gets the English ones.
    QTranslator translator;
    if (QLocale().language() != QLocale::Spanish
        && translator.load(QStringLiteral(":/i18n/wonderland-bluetooth_en.qm"))) {
        QCoreApplication::installTranslator(&translator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "El gestor de Bluetooth de Wonder OS."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QCoreApplication::translate("main", "auto (como el escritorio), light o dark."), QCoreApplication::translate("main", "modo"), QStringLiteral("auto"));
    const QCommandLineOption demoOption(QStringLiteral("demo"), QCoreApplication::translate("main", "Un Bluetooth de mentira con dispositivos de ejemplo: no toca el del equipo."));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QCoreApplication::translate("main", "Guarda una imagen de la ventana en este archivo y sale (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption doOption(QStringLiteral("do"), QCoreApplication::translate("main", "Con --shot: acciones previas, separadas por «;»."), QCoreApplication::translate("main", "acciones"));
    const QCommandLineOption sizeOption(QStringLiteral("size"), QCoreApplication::translate("main", "Con --shot: el tamaño de la ventana, como ANCHOxALTO."), QCoreApplication::translate("main", "tamaño"));
    parser.addOptions({appearanceOption, demoOption, shotOption, doOption, sizeOption});
    parser.process(app);

    const bool testing = parser.isSet(shotOption);
    const bool demo = parser.isSet(demoOption);
    App desktop(testing ? QFileInfo(parser.value(shotOption)).absoluteFilePath() : QString());
    if (!testing && !demo && !desktop.claim()) {
        return 0;
    }

    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        desktop.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        desktop.setAppearance(App::Appearance::Dark);
    }
    if (testing) {
        QSize size = desktop.testSize();
        const QStringList parts = parser.value(sizeOption).split(QLatin1Char('x'));
        if (parts.size() == 2 && parts.at(0).toInt() > 0 && parts.at(1).toInt() > 0) {
            size = QSize(parts.at(0).toInt(), parts.at(1).toInt());
        }
        desktop.setTest(parser.value(doOption).split(QLatin1Char(';'), Qt::SkipEmptyParts), size);
    }

    FakeBackend *fake = nullptr;
    Backend *backend = nullptr;
    if (demo) {
        fake = new FakeBackend(&app);
        fake->loadSample();
        backend = fake;
    } else {
        backend = new BluezBackend(&app);
    }
    Bluetooth bluetooth(backend);

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("symbol"), new SymbolProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &desktop);
    engine.rootContext()->setContextProperty(QStringLiteral("Bt"), &bluetooth);
    engine.rootContext()->setContextProperty(QStringLiteral("Fake"), fake);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandBluetooth", "Main");
    return app.exec();
}
