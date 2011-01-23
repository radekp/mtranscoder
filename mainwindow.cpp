#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
        QMainWindow(parent),
        ui(new Ui::MainWindow),
        queueFd(-1),
        queuePipe(),
        queue(),
        currFile(),
        process(this),
        settings("radekp", "mtranscoder"),
        profiles()
{
    ui->setupUi(this);

    connect(&process, SIGNAL(readyRead()), this, SLOT(processReadyRead()));
    connect(&process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));

    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList profiles = settings.childGroups();
    if(profiles.count() == 0)
    {
        settings.beginGroup("profileLowQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec libx264 -vpre fast -b 512k");
        settings.setValue("hq", false);
        settings.endGroup();

        settings.beginGroup("profileHighQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec libx264 -vpre medium -b 4000k");
        settings.setValue("hq", true);
        settings.endGroup();

        sync();
        profiles = settings.allKeys();
    }

    for(int i = profiles.count() - 1; i >= 0 && profiles.count() > 0 ; )
    {
        QString name = profiles.at(i);
        if(name.indexOf("profile") != 0)
        {
            profiles.removeAt(i);
            continue;
        }
        ui->cbProfile->addItem(name);
        i--;
    }

    QTimer::singleShot(10, this, SLOT(init()));
}

MainWindow::~MainWindow()
{
    delete ui;
    if(queueFd >= 0)
    {
        ::close(queueFd);
        queueFd = -1;
    }
}

void MainWindow::log(QString text)
{
    ui->tbLog->append(text);
}

void MainWindow::init()
{
    bool ok = QDir::home().exists(".mtranscoder/lq") &&
              QDir::home().exists(".mtranscoder/hq");

    if(!ok)
    {
        ok = QDir::home().mkpath(".mtranscoder/hq") &&
             QDir::home().mkpath(".mtranscoder/lq");

        if(!ok)
        {
            log("Failed to initialize work dir ~/.mtranscoder");
            return;
        }
    }

    QString queuePathStr = QDir::homePath() + "/.mtranscoder_queue";
    const char *queuePath = queuePathStr.toLatin1().constData();
    log("Opening queue at " + queuePathStr);

    QFile::remove(queuePathStr);
    if(mkfifo(queuePath, S_IRWXU) < 0)
    {
        log("mkfifo failed");
        perror("mkfifo failed");
        return;
    }

    int fd = open(queuePath, O_RDONLY|O_NONBLOCK);
    if(fd < 0)
    {
        log("Failed to open queue");
        return;
    }

    queuePipe.open(fd, QFile::ReadOnly | QFile::Text);

    log("Init ok, listening for transcoding requests");
    queueReadReady();
}

void MainWindow::updateView()
{
    ui->lwQueue->clear();
    for(int i = 0; i < queue.count(); i++)
    {
        ui->lwQueue->addItem(queue[i]);
    }
}

void MainWindow::startProcess()
{
    if(queue.count() == 0)
    {
        return;
    }
    QString filename = queue.at(0);
    QString profile = ui->cbProfile->currentText();
    settings.beginGroup(profile);
    QString params = settings.value("params").toString();
    bool hq = settings.value("hq").toBool();

    QStringList args = params.split(' ');
    args.prepend(filename);
    args.prepend("-i");

    if(hq)
    {
        currFile = QDir::homePath() + "/.mtranscoder/hq/" + filename + ".part";
    }
    else
    {
        currFile = QDir::homePath() + "/.mtranscoder/lq/" + filename + ".part";
    }
    args << currFile;

    QString outDir = currFile.left(currFile.lastIndexOf('/'));
    QDir::root().mkpath(outDir);
    QFile::remove(currFile);

    QString cmd("ffmpeg ");
    for(int i = 0; i < args.length(); i++)
    {
        cmd.append(args.at(i));
        cmd.append(' ');
    }
    log(cmd);

    process.start("ffmpeg", args);
}

void MainWindow::processReadyRead()
{
    QString txt = process.readAll().trimmed();
    log(txt);

    // Search for percents - e.g. (34%)
    int index = txt.indexOf("%)");
    if(index < 5)
    {
        return;
    }
    int startIndex = txt.indexOf('(', index - 5) + 1;
    if(startIndex <= 0)
    {
        return;
    }
    QString numStr = txt.mid(startIndex, index - startIndex);
    ui->pbEnc->setValue(numStr.toInt());
}

void MainWindow::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    log(QString("Encoding finished, exitCode=%1 exitStatus=%2").arg(exitCode).arg(exitStatus));

    if(exitCode == 0 && exitStatus == QProcess::NormalExit)
    {
        QString dst = currFile.left(currFile.lastIndexOf(".part"));
        QFile::rename(currFile, dst);
    }

    if(queue.count() > 0)
    {
        queue.removeAt(0);
    }
    if(queue.count() > 0)
    {
        startProcess();
    }
    updateView();
}

void MainWindow::queueReadReady()
{
    QString line = queuePipe.readLine().trimmed();
    if(line.length() <= 0)
    {
        QTimer::singleShot(1000, this, SLOT(queueReadReady()));
        return;
    }

    if(queue.contains(line))
    {
        QTimer::singleShot(1000, this, SLOT(queueReadReady()));
        log("File already in queue " + line);
        return;
    }
    queue.append(line);
    log("Queing " + line);
    updateView();

    if(process.state() == QProcess::NotRunning)
    {
        startProcess();
    }

    QTimer::singleShot(100, this, SLOT(queueReadReady()));
}

void MainWindow::on_cbProfile_currentIndexChanged(int index)
{

}

void MainWindow::on_bEdit_clicked()
{
    QMessageBox::information(this, "Profiles", "Please edit them manually in file " + settings.fileName());
}
