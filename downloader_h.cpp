#ifndef DOWNLOADER_H
#define DOWNLOADER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QUrl>
#include <QMutex>

class Downloader : public QObject {
    Q_OBJECT

public:
    explicit Downloader(QNetworkAccessManager *manager, const QString &url, QObject *parent = nullptr);
    void startDownload();
    void pauseDownload();
    void resumeDownload();
    void createProgressFile();
    void updateProgressFile(qint64 bytesReceived, qint64 bytesTotal);

    // Getter for downloadedBytes
    qint64 getDownloadedBytes() const { return downloadedBytes; }

signals:
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void pauseResumeStatusChanged(bool paused);

private slots:
    void onDownloadFinished();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);

private:
    QNetworkAccessManager *networkManager;
    QString downloadUrl;
    QNetworkReply *reply;
    QFile *file;
    QFile *progressFile;
    qint64 downloadedBytes;
    QMutex mutex;
    bool paused;
};

#endif // DOWNLOADER_H

Downloader.cpp
#include "downloader.h"
#include <QDir>
#include <QTextStream>

Downloader::Downloader(QNetworkAccessManager *manager, const QString &url, QObject *parent)
    : QObject(parent), networkManager(manager), downloadUrl(url), reply(nullptr), file(nullptr),
      progressFile(nullptr), downloadedBytes(0), paused(false) {}

void Downloader::startDownload() {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    QUrl url(downloadUrl);
    QString filePath = QDir::homePath() + "/qt_downloads/" + url.fileName();
    file = new QFile(filePath);

    if (!file || !file->open(QIODevice::WriteOnly)) { 
        emit downloadFailed("Failed to open file for writing.");
        return;
    }

    // Check if progress file already exists before creating it
    QDir progressDir(QDir::homePath() + "/progress");
    if (!progressDir.exists()) {
        progressDir.mkpath(".");
    }

    QString progressFilePath = progressDir.filePath(QUrl(downloadUrl).fileName() + ".progress");
    if (!QFile::exists(progressFilePath)) {
        createProgressFile();  // Create the progress file if it doesn't exist
    }

    downloadedBytes = file->size();

    QNetworkRequest request(url);
    request.setRawHeader("Range", "bytes=" + QByteArray::number(downloadedBytes) + "-");
    reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, &Downloader::onDownloadFinished);
    connect(reply, &QNetworkReply::downloadProgress, this, &Downloader::onDownloadProgress);
}

void Downloader::pauseDownload() {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    if (!paused && reply) {
        paused = true;
        disconnect(reply, &QNetworkReply::downloadProgress, this, &Downloader::onDownloadProgress);
        disconnect(reply, &QNetworkReply::finished, this, &Downloader::onDownloadFinished);
        reply->abort();

        qint64 totalBytes = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();

        if (progressFile && progressFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(progressFile);
            stream << "Download URL: " << downloadUrl << "\n";
            stream << "Downloaded: " << downloadedBytes << " / " << totalBytes << "\n";
            stream << "Status: paused\n";  // Mark as paused
            progressFile->close();
        }

        emit pauseResumeStatusChanged(true);
    }
}

void Downloader::resumeDownload() {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    if (paused) {
        paused = false;

        connect(reply, &QNetworkReply::downloadProgress, this, &Downloader::onDownloadProgress);
        connect(reply, &QNetworkReply::finished, this, &Downloader::onDownloadFinished);
        startDownload();
        emit pauseResumeStatusChanged(false);
    }
}

void Downloader::onDownloadFinished() {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    if (reply->error() == QNetworkReply::NoError) {
        if (file->open(QIODevice::WriteOnly)) {
            file->write(reply->readAll());
            file->close();
        }

        if (progressFile && progressFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(progressFile);
            stream << "Download URL: " << downloadUrl << "\n";
            stream << "Downloaded: " << downloadedBytes << " / " << downloadedBytes << "\n";
            stream << "Status: completed\n";  // Mark as completed
            progressFile->close();

            QFile::remove(progressFile->fileName());
            delete progressFile;
            progressFile = nullptr;
        }

        emit downloadFinished(file->fileName());
    } else {
        emit downloadFailed(reply->errorString());
    }
    reply->deleteLater();
}

void Downloader::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    QMutexLocker locker(&mutex);  // Ensure thread safety

    downloadedBytes = bytesReceived;  // Update the total downloaded 
    if (file->open(QIODevice::WriteOnly)) {
        file->write(reply->readAll());
        file->close();
    }

    if (bytesTotal > 0) {  // Prevent division by zero
        emit downloadProgress(downloadedBytes, bytesTotal);  // Emit progress signal
    } else {
        emit downloadProgress(downloadedBytes, 1);  // Use a placeholder value if total size isn't available
    }

    updateProgressFile(downloadedBytes, bytesTotal);  // Update the progress file with current status
}

void Downloader::createProgressFile() {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    QDir progressDir(QDir::homePath() + "/progress");
    if (!progressDir.exists()) {
        progressDir.mkpath(".");
    }

    QString progressFilePath = progressDir.filePath(QUrl(downloadUrl).fileName() + ".progress");
    progressFile = new QFile(progressFilePath);
    if (progressFile->open(QIODevice::WriteOnly)) {
        QTextStream stream(progressFile);
        stream << "Download URL: " << downloadUrl << "\n";
        stream << "Downloaded: 0\n";
        stream << "Status: in-progress\n";  // Set the status as in-progress
        progressFile->close();
    }
}

void Downloader::updateProgressFile(qint64 bytesReceived, qint64 bytesTotal) {
    QMutexLocker locker(&mutex);  // Ensure thread safety
    if (progressFile && progressFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(progressFile);
        stream << "Download URL: " << downloadUrl << "\n";
        stream << "Downloaded: " << bytesReceived << " / " << bytesTotal << "\n";
        stream << "Status: in-progress\n";  // Update status to in-progress
        progressFile->close();
    }
}
Downloadthread.h
#ifndef DOWNLOADTHREAD_H
#define DOWNLOADTHREAD_H

#include <QThread>
#include <QNetworkAccessManager>
#include <QString>
#include "downloader.h"

class DownloadThread : public QThread {
    Q_OBJECT

public:
    explicit DownloadThread(QNetworkAccessManager *manager, const QString &url, QObject *parent = nullptr);
    void run() override;
    void pauseDownload();
    void resumeDownload();

signals:
    void downloadFinished(const QString &filePath);
    void downloadFailed(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void pauseResumeStatusChanged(bool paused);

private:
    QNetworkAccessManager *networkManager;
    QString downloadUrl;
    Downloader *downloader;
};

#endif // DOWNLOADTHREAD_H

Downloadthread.cpp
#include "downloadthread.h"

DownloadThread::DownloadThread(QNetworkAccessManager *manager, const QString &url, QObject *parent)
    : QThread(parent), networkManager(manager), downloadUrl(url), downloader(nullptr) {}

void DownloadThread::run() {
    downloader = new Downloader(networkManager, downloadUrl);
    connect(downloader, &Downloader::downloadFinished, this, &DownloadThread::downloadFinished);
    connect(downloader, &Downloader::downloadFailed, this, &DownloadThread::downloadFailed);
    connect(downloader, &Downloader::downloadProgress, this, &DownloadThread::downloadProgress);
    connect(downloader, &Downloader::pauseResumeStatusChanged, this, &DownloadThread::pauseResumeStatusChanged);

    downloader->startDownload();
    exec();
}

void DownloadThread::pauseDownload() {
    if (downloader) {
        downloader->pauseDownload();
    }
}

void DownloadThread::resumeDownload() {
    if (downloader) {
        downloader->resumeDownload();
    }
}


Main.cpp
#include "downloadthread.h"
#include <QApplication>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QProgressBar>
#include <QLineEdit>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QMutexLocker>
#include <QStringList>

void startDownload(const QString &url, QVBoxLayout *layout, QNetworkAccessManager *networkManager, QWidget *window) {
    QVBoxLayout *downloadLayout = new QVBoxLayout();
    QLabel *urlLabel = new QLabel(url, window);
    QProgressBar *progressBar = new QProgressBar(window);
    QPushButton *pauseResumeButton = new QPushButton("Pause", window);

    DownloadThread *downloadThread = new DownloadThread(networkManager, url, window);

    progressBar->setRange(0, 100);
    progressBar->setValue(0);

    downloadLayout->addWidget(urlLabel);
    downloadLayout->addWidget(progressBar);
    downloadLayout->addWidget(pauseResumeButton);

    layout->addLayout(downloadLayout);

    QObject::connect(downloadThread, &DownloadThread::downloadProgress, [&](qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal == 0) {
        // Handle indeterminate state
        progressBar->setValue(0);  // Default to 0, or use progressBar->setRange(0, 0) for a "busy" indicator
    } else {
        // Normal progress calculation
        progressBar->setValue(static_cast<int>((bytesReceived * 100) / bytesTotal));
    }
});

    QObject::connect(downloadThread, &DownloadThread::downloadFinished, [&](const QString &fileName) {
    urlLabel->setText("Downloaded: " + fileName);
    progressBar->setValue(100);
    pauseResumeButton->setDisabled(true);

    // Manual termination of the thread
    downloadThread->quit();
    downloadThread->wait();
    downloadThread->deleteLater();  // Use Qt's deferred deletion to clean up safely
});

// Slot to handle start download button click
void onStartDownloadButtonClicked(QLineEdit *urlInput, QVBoxLayout *layout, QNetworkAccessManager *networkManager, QWidget *window) {
    QString inputUrls = urlInput->text();  // Get comma-separated URLs
    QStringList urls = inputUrls.split(",", QString::SkipEmptyParts);  // Split into list of URLs

    for (const QString &url : urls) {
        startDownload(url.trimmed(), layout, networkManager, window);  // Start download for each URL
    }
}

// Function to load unfinished downloads (optional)
void loadUnfinishedDownloads(QVBoxLayout *layout, QWidget *window, QNetworkAccessManager *networkManager) {
    QDir progressDir(QDir::homePath() + "/progress");
    if (progressDir.exists()) {
        QStringList progressFiles = progressDir.entryList(QDir::Files);
        for (const QString &progressFileName : progressFiles) {
            QFile progressFile(progressDir.filePath(progressFileName));
            if (progressFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream stream(&progressFile);
                QString url;
                qint64 downloadedBytes = 0;
                QString status;  // Add a variable to track the status

                while (!stream.atEnd()) {
                    QString line = stream.readLine();
                    if (line.startsWith("Download URL:")) {
                        url = line.section(":", 1).trimmed();
                    } else if (line.startsWith("Downloaded:")) {
                        downloadedBytes = line.section(":", 1).trimmed().toLongLong();
                    } else if (line.startsWith("Status:")) {  // New line for status
                        status = line.section(":", 1).trimmed();
                    }
                }

                if (!url.isEmpty() && status != "in-progress") {  // Only resume if not in-progress
                    startDownload(url, layout, networkManager, window);
                }
            }
        }
    }
}
int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    QWidget window;
    QVBoxLayout *layout = new QVBoxLayout(&window);
    QNetworkAccessManager *networkManager = new QNetworkAccessManager(&window);

    window.setWindowTitle("Download Manager");
    window.setLayout(layout);
    window.resize(400, 300);

    QLineEdit *urlInput = new QLineEdit(&window);
    QPushButton *startDownloadButton = new QPushButton("Start Download", &window);

    layout->addWidget(urlInput);
    layout->addWidget(startDownloadButton);

    QObject::connect(startDownloadButton, &QPushButton::clicked, [&]() {
        onStartDownloadButtonClicked(urlInput, layout, networkManager, &window);
    });

    loadUnfinishedDownloads(layout, &window, networkManager);

    window.show();

    return a.exec();
}
