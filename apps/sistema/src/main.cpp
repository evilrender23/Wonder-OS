#include "app.h"
#include "appearance.h"
#include "config.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

static QString defaultConfigPath()
{
    QString home = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (home.isEmpty()) {
        home = QDir::homePath() + QStringLiteral("/.config");
    }
    return home + QStringLiteral("/driftwm/config.toml");
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("wonderland"));
    QCoreApplication::setApplicationName(QStringLiteral("wonderland-sistema"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SISTEMA_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("wonderland-sistema"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/WonderlandSistema/wonderland-sistema.svg")));

    // Los textos están escritos en español; cualquier otro idioma recibe el inglés.
    QTranslator translator;
    if (QLocale().language() != QLocale::Spanish
        && translator.load(QStringLiteral(":/i18n/wonderland-sistema_en.qm"))) {
        QCoreApplication::installTranslator(&translator);
    }
    QGuiApplication::setApplicationDisplayName(QCoreApplication::translate("main", "Sistema"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Los ajustes del sistema de Wonderlands OS."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption configOption(QStringLiteral("config"), QCoreApplication::translate("main", "El config.toml que se edita (por defecto, el del escritorio)."), QCoreApplication::translate("main", "archivo"));
    const QCommandLineOption pageOption(QStringLiteral("page"), QCoreApplication::translate("main", "Abrir en este apartado (p. ej. atajos, teclado, ventanas)."), QCoreApplication::translate("main", "apartado"));
    const QCommandLineOption appearanceOption(QStringLiteral("appearance"), QCoreApplication::translate("main", "auto (como el escritorio), light o dark."), QCoreApplication::translate("main", "modo"), QStringLiteral("auto"));
    const QCommandLineOption shotOption(QStringLiteral("shot"), QCoreApplication::translate("main", "Guardar una imagen de la ventana en este archivo y salir (para pruebas)."), QStringLiteral("png"));
    const QCommandLineOption lookFileOption(QStringLiteral("look-file"), QCoreApplication::translate("main", "El appearance.json que se edita (por defecto, el del escritorio)."), QCoreApplication::translate("main", "archivo"));
    const QCommandLineOption pickTintOption(QStringLiteral("pick-tint"), QCoreApplication::translate("main", "Con --page carpetas: dejar este color elegido sin aplicarlo (para pruebas)."), QStringLiteral("#rrggbb"));
    const QCommandLineOption searchOption(QStringLiteral("search"), QCoreApplication::translate("main", "Abrir con esta búsqueda hecha."), QCoreApplication::translate("main", "texto"));
    parser.addOptions({configOption, pageOption, searchOption, appearanceOption, shotOption, lookFileOption, pickTintOption});
    parser.process(app);

    App sistema;
    const QString appearance = parser.value(appearanceOption);
    if (appearance == QLatin1String("light")) {
        sistema.setAppearance(App::Appearance::Light);
    } else if (appearance == QLatin1String("dark")) {
        sistema.setAppearance(App::Appearance::Dark);
    }

    const QString path = parser.isSet(configOption) ? QFileInfo(parser.value(configOption)).absoluteFilePath() : defaultConfigPath();
    Config config(path, QStandardPaths::findExecutable(QStringLiteral("driftwm")));

    Appearance look(parser.isSet(lookFileOption) ? QFileInfo(parser.value(lookFileOption)).absoluteFilePath() : Appearance::defaultPath());

    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &sistema);
    engine.rootContext()->setContextProperty(QStringLiteral("Config"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("Appearance"), &look);
    engine.rootContext()->setContextProperty(QStringLiteral("startPage"), parser.value(pageOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startTint"), parser.value(pickTintOption));
    engine.rootContext()->setContextProperty(QStringLiteral("startSearch"), parser.value(searchOption));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        QCoreApplication::exit(2);
    }, Qt::QueuedConnection);
    engine.loadFromModule("WonderlandSistema", "Main");

    if (parser.isSet(shotOption) && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = QFileInfo(parser.value(shotOption)).absoluteFilePath();
        QTimer::singleShot(900, window, [window, file] {
            QCoreApplication::exit(window->grabWindow().save(file) ? 0 : 1);
        });
    }
    return app.exec();
}
