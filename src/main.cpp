#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Notepatra");
    app.setOrganizationName("Notepatra");

    MainWindow window;

    // Open files from command line
    for (int i = 1; i < argc; i++) {
        QString path = QString::fromUtf8(argv[i]);
        if (QFileInfo(path).isFile())
            window.openFile(QFileInfo(path).absoluteFilePath());
    }

    window.show();
    return app.exec();
}
