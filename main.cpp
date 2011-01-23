#include <sys/types.h>
#include <unistd.h>

#include <QtGui/QApplication>
#include "mainwindow.h"


int main(int argc, char *argv[])
{
    // Renice to lowest priority
    int pid = getpid();
    QString reniceCmd = QString("renice 19 %1").arg(pid);
    system(reniceCmd.toLatin1().constData());

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
