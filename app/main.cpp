#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include <QPalette>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("neodigi");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("KC3SMW");
    app.setOrganizationDomain("kc3smw.radio");

    // Force Fusion — bypasses GTK platform integration that overrides QSS
    app.setStyle(QStyleFactory::create("Fusion"));

    // Dark palette as fallback for any widget the QSS doesn't reach
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(0x1e, 0x1e, 0x1e));
    dark.setColor(QPalette::WindowText,      QColor(0xd4, 0xd4, 0xd4));
    dark.setColor(QPalette::Base,            QColor(0x2d, 0x2d, 0x2d));
    dark.setColor(QPalette::AlternateBase,   QColor(0x25, 0x25, 0x26));
    dark.setColor(QPalette::ToolTipBase,     QColor(0x25, 0x25, 0x26));
    dark.setColor(QPalette::ToolTipText,     QColor(0xd4, 0xd4, 0xd4));
    dark.setColor(QPalette::Text,            QColor(0xd4, 0xd4, 0xd4));
    dark.setColor(QPalette::Button,          QColor(0x2d, 0x2d, 0x2d));
    dark.setColor(QPalette::ButtonText,      QColor(0xd4, 0xd4, 0xd4));
    dark.setColor(QPalette::BrightText,      QColor(0x4e, 0xc9, 0xb0));
    dark.setColor(QPalette::Link,            QColor(0x9c, 0xdc, 0xfe));
    dark.setColor(QPalette::Highlight,       QColor(0x3a, 0x5f, 0x7a));
    dark.setColor(QPalette::HighlightedText, QColor(0xd4, 0xd4, 0xd4));
    dark.setColor(QPalette::Mid,             QColor(0x3c, 0x3c, 0x3c));
    dark.setColor(QPalette::Dark,            QColor(0x18, 0x18, 0x18));
    dark.setColor(QPalette::Shadow,          QColor(0x0a, 0x0a, 0x0a));
    dark.setColor(QPalette::PlaceholderText, QColor(0x6a, 0x6a, 0x6a));
    app.setPalette(dark);

    // Stylesheet applied after palette — QSS overrides palette rules
    QFile qss(":/style.qss");
    if (qss.open(QFile::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    app.setWindowIcon(QIcon(":/neodigiIcon.png"));

    MainWindow window;
    window.show();
    window.raise();
    window.activateWindow();
    return app.exec();
}
