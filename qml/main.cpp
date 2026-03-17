#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QIcon>
#include <QQmlError>
#include "Backend.h"
#include "ImageProvider.h"

int main(int argc, char *argv[])
{
    /* Use Universal dark theme for built-in controls (SpinBox, ComboBox) */
    qputenv("QT_QUICK_CONTROLS_STYLE", "Universal");
    qputenv("QT_QUICK_CONTROLS_UNIVERSAL_THEME", "Dark");
    qputenv("QT_QUICK_CONTROLS_UNIVERSAL_ACCENT", "#e87a20");
    qputenv("QT_QUICK_CONTROLS_UNIVERSAL_FOREGROUND", "#d4d4d4");
    qputenv("QT_QUICK_CONTROLS_UNIVERSAL_BACKGROUND", "#1e1e1e");

    /* Force console output for debugging */
    freopen("C:\\Users\\kaden\\bf_debug.txt", "w", stderr);
    fprintf(stderr, "BF_DBG: main() start\n"); fflush(stderr);

    QGuiApplication app(argc, argv);
    app.setApplicationName("BayerFlow");
    app.setOrganizationName("BayerFlow");
    app.setWindowIcon(QIcon("C:/Users/kaden/BayerFlow-Win/qml/icon.png"));

    Backend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.addImageProvider("preview", new PreviewImageProvider(&backend));
    engine.addImportPath("C:/Users/kaden/BayerFlow-Win/qml");
    /* Also search for Qt QML modules next to the exe (windeployqt puts them in build/qml/) */
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");

    /* Enable QML warnings to stderr */
    qputenv("QML_IMPORT_TRACE", "1");

    fprintf(stderr, "BF_DBG: loading QML...\n"); fflush(stderr);

    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError> &warnings) {
        for (const auto &w : warnings)
            fprintf(stderr, "QML WARNING: %s\n", w.toString().toUtf8().constData());
        fflush(stderr);
    });

    engine.load(QUrl::fromLocalFile("C:/Users/kaden/BayerFlow-Win/qml/main.qml"));
    fprintf(stderr, "BF_DBG: QML loaded, rootObjects=%d\n", (int)engine.rootObjects().size()); fflush(stderr);

    if (engine.rootObjects().isEmpty()) {
        fprintf(stderr, "BF_DBG: No root objects - QML failed to load!\n"); fflush(stderr);
        return -1;
    }

    return app.exec();
}
