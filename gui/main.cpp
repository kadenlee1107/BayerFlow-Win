#include <QApplication>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("BayerFlow");
    app.setOrganizationName("BayerFlow");

    MainWindow win;
    win.show();

    return app.exec();
}
