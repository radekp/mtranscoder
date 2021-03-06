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
        settings("mtranscoder", "mtranscoder"),
        profiles()
{
    ui->setupUi(this);

    connect(&process, SIGNAL(readyRead()), this, SLOT(processReadyRead()));
    connect(&process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));

    process.setProcessChannelMode(QProcess::MergedChannels);

    QStringList profiles = settings.childGroups();
    if(profiles.count() == 0)
    {
        settings.beginGroup("profileH264LowQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec libx264 -vpre fast -b 512k");
        settings.setValue("hq", false);
        settings.endGroup();

        settings.beginGroup("profileH264HighQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec libx264 -vpre medium -b 4000k");
        settings.setValue("hq", true);
        settings.endGroup();

        settings.beginGroup("profileMpeg2LowQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec mpeg2video -b 512k");
        settings.setValue("hq", false);
        settings.endGroup();

        settings.beginGroup("profileMpeg2HighQuality");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec mpeg2video -b 4000k");
        settings.setValue("hq", false);
        settings.endGroup();

        settings.beginGroup("profileMpeg2HighQualitySrc5x2Dst640x360");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec mpeg2video -b 4000k -vf pad=640:360:0:52:black");
        settings.setValue("hq", false);
        settings.endGroup();

        settings.beginGroup("profileMpeg2HighQualitySrc12x5Dst576x324");
        settings.setValue("params", "-f mpegts -acodec ac3 -ac 2 -ar 44100 -vcodec mpeg2video -b 4000k -vf pad=576:324:0:42:black");
        settings.setValue("hq", false);
        settings.endGroup();

        sync();
        profiles = settings.childGroups();
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
    if(queueFd >= 0)
    {
        ::close(queueFd);
        queueFd = -1;
    }
    if(process.state() != QProcess::NotRunning)
    {
        process.terminate();
        process.waitForFinished(1000);
    }
    if(process.state() != QProcess::NotRunning)
    {
        process.kill();
    }
    delete ui;
}

void MainWindow::log(QString text)
{
    ui->tbLog->append(text);
    QScrollBar *sb = ui->tbLog->verticalScrollBar();
    sb->setValue(sb->maximum());

    lastLog.append(text);
    lastLog.append("\n");
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
    settings.endGroup();

    QString dst;
    if(hq)
    {
        dst = QDir::homePath() + "/.mtranscoder/hq/" + filename;
    }
    else
    {
        dst = QDir::homePath() + "/.mtranscoder/lq/" + filename;
    }
    if(QFile::exists(dst))
    {
        log(dst + " is already encoded in current quality");
        queue.removeAt(0);
        updateView();
        return;
    }

    QStringList args = params.split(' ');
    args.prepend(filename);
    args.prepend("-i");

    currFile = dst + ".mpg.part";
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
}

void MainWindow::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    log(QString("Encoding finished, exitCode=%1 exitStatus=%2").arg(exitCode).arg(exitStatus));

    if(exitCode == 0 && exitStatus == QProcess::NormalExit)
    {
        QString dst = currFile.left(currFile.lastIndexOf(".part"));
        log(currFile + "->" + dst);
        QFile::remove(dst);
        QFile::rename(currFile, dst);
    }
    ui->tbLog->setPlainText(lastLog);
    lastLog = "";

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
