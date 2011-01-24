#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QDebug>
#include <QProcess>
#include <QSettings>
#include <QMessageBox>
#include <QScrollBar>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

namespace Ui {
    class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    int queueFd;
    QFile queuePipe;
    QStringList queue;
    QProcess process;
    QString currFile;
    QSettings settings;
    QStringList profiles;
    QString lastLog;

    void log(QString text);
    void updateView();
    void startProcess();

private slots:
    void on_bEdit_clicked();
    void on_cbProfile_currentIndexChanged(int index);
    void init();
    void queueReadReady();
    void processReadyRead();
    void processFinished(int, QProcess::ExitStatus);
};

#endif // MAINWINDOW_H
