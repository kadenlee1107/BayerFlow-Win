#include <QApplication>
#include "MainWindow.h"

static const char *DARK_STYLE = R"(
QWidget {
    background-color: #1e1e1e;
    color: #d4d4d4;
    font-family: "Segoe UI", sans-serif;
    font-size: 13px;
}
QMainWindow {
    background-color: #1e1e1e;
}
QGroupBox {
    border: 1px solid #3c3c3c;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 16px;
    font-weight: bold;
    color: #e0e0e0;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 6px;
    color: #569cd6;
}
QLineEdit {
    background-color: #2d2d2d;
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    padding: 6px 8px;
    color: #d4d4d4;
    selection-background-color: #264f78;
}
QLineEdit:focus {
    border-color: #569cd6;
}
QPushButton {
    background-color: #2d2d2d;
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    padding: 6px 16px;
    color: #d4d4d4;
    min-height: 24px;
}
QPushButton:hover {
    background-color: #3c3c3c;
    border-color: #569cd6;
}
QPushButton:pressed {
    background-color: #264f78;
}
QPushButton:disabled {
    color: #5a5a5a;
    background-color: #252525;
    border-color: #333333;
}
QPushButton#startBtn {
    background-color: #0e639c;
    border-color: #1177bb;
    color: #ffffff;
    font-weight: bold;
    padding: 8px 28px;
}
QPushButton#startBtn:hover {
    background-color: #1177bb;
}
QPushButton#startBtn:pressed {
    background-color: #0d5689;
}
QPushButton#cancelBtn {
    background-color: #5a1d1d;
    border-color: #8b2d2d;
    color: #f0a0a0;
}
QPushButton#cancelBtn:hover {
    background-color: #8b2d2d;
    color: #ffffff;
}
QProgressBar {
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    background-color: #2d2d2d;
    text-align: center;
    color: #d4d4d4;
    min-height: 22px;
}
QProgressBar::chunk {
    background-color: #0e639c;
    border-radius: 3px;
}
QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #2d2d2d;
    border: 1px solid #3c3c3c;
    border-radius: 4px;
    padding: 4px 8px;
    color: #d4d4d4;
    min-height: 22px;
}
QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: #569cd6;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background-color: #2d2d2d;
    border: 1px solid #3c3c3c;
    selection-background-color: #264f78;
    color: #d4d4d4;
}
QLabel {
    color: #d4d4d4;
}
QLabel#statusLabel {
    color: #808080;
    font-size: 12px;
    padding: 4px 0;
}
QLabel#noiseValue {
    color: #4ec9b0;
    font-weight: bold;
    font-family: "Cascadia Mono", "Consolas", monospace;
}
)";

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("BayerFlow");
    app.setOrganizationName("BayerFlow");
    app.setStyleSheet(DARK_STYLE);

    MainWindow win;
    win.show();

    return app.exec();
}
