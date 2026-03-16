#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
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

    QGuiApplication app(argc, argv);
    app.setApplicationName("BayerFlow");
    app.setOrganizationName("BayerFlow");

    Backend backend;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.addImageProvider("preview", new PreviewImageProvider(&backend));
    engine.addImportPath("C:/Users/kaden/BayerFlow-Win/qml");

    /* Enable QML warnings to stderr */
    qputenv("QML_IMPORT_TRACE", "1");

    engine.load(QUrl::fromLocalFile("C:/Users/kaden/BayerFlow-Win/qml/main.qml"));

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
