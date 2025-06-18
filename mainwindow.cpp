#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QFileDialog>
#include <QNetworkDatagram>
#include <QDataStream>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QNetworkInterface>
#include <QSysInfo>
#include <QHostInfo>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QUrl>
#include <QDesktopServices>
#include <QtEndian>

static QElapsedTimer discoveryTimer;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Collect local IP
    for (const auto& addr : QNetworkInterface::allAddresses()) {
        if (addr.protocol()==QAbstractSocket::IPv4Protocol && !addr.isLoopback())
            localIps << addr.toString();
    }

    // UDP Discover
    discoverSocket = new QUdpSocket(this);
    discoverSocket->bind(QHostAddress::AnyIPv4,
                         AppConstants::DISCOVERY_PORT,
                         QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(discoverSocket, &QUdpSocket::readyRead,
            this, &MainWindow::onUdpReadyRead);

    // TCP Server (Receiving)
    fileServer = new QTcpServer(this);
    if (!fileServer->listen(QHostAddress::AnyIPv4, AppConstants::TRANSFER_PORT)) {
        statusBar()->showMessage(
            tr("Port Failed to Open: %1")
                .arg(fileServer->errorString())
            );
    } else {
        statusBar()->showMessage(
            tr("Listening: %1:%2")
                .arg(fileServer->serverAddress().toString())
                .arg(fileServer->serverPort())
            );
    }
    connect(fileServer, &QTcpServer::newConnection,
            this, &MainWindow::onNewConnection);

    QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    ui->labelReceivedFolder->setText(tr("Directory: %1").arg(dl));
}

MainWindow::~MainWindow() {
    delete ui;
}

// --- DISCOVER ---

// -----------------------------------------------------------------------------
// User clicked "Discover": clear list, reset state, broadcast discovery ping.
// -----------------------------------------------------------------------------
void MainWindow::on_btnDiscover_clicked() {
    ui->treePeers->clear();
    discoveredMacs.clear();
    discoveryTimer.start();
    const QByteArray msg = AppConstants::DISCOVER_REQUEST;

    // Global broadcast
    discoverSocket->writeDatagram(msg, QHostAddress::Broadcast, AppConstants::DISCOVERY_PORT);
    for (auto& iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) ||
            (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (auto& entry : iface.addressEntries()) {
            auto b = entry.broadcast();
            if (!b.isNull() && b != QHostAddress::Any)
                discoverSocket->writeDatagram(msg, b, AppConstants::DISCOVERY_PORT);
        }
    }
}

// -----------------------------------------------------------------------------
// Handle incoming discovery datagrams: respond or record peers.
// -----------------------------------------------------------------------------
void MainWindow::onUdpReadyRead() {
    while (discoverSocket->hasPendingDatagrams()) {
        auto dg = discoverSocket->receiveDatagram();
        auto msg = dg.data();
        auto peer = dg.senderAddress();

        if (msg == AppConstants::DISCOVER_REQUEST) {
            QString hostname = QHostInfo::localHostName();
            QString os = QSysInfo::prettyProductName();
            QString mac;
            for (auto& iface : QNetworkInterface::allInterfaces()) {
                if ((iface.flags() & QNetworkInterface::IsUp) &&
                    !(iface.flags() & QNetworkInterface::IsLoopBack))
                {
                    mac = iface.hardwareAddress();
                    break;
                }
            }
            QByteArray resp = AppConstants::DISCOVER_RESPONSE + '|' +
                              hostname.toUtf8() + '|' +
                              os.toUtf8()       + '|' +
                              mac.toUtf8();
            discoverSocket->writeDatagram(resp, peer, AppConstants::DISCOVERY_PORT);
        }
        else if (msg.startsWith(AppConstants::DISCOVER_RESPONSE)) {
            auto parts = msg.split('|');
            if (parts.size()!=4) continue;
            QString hostname = QString::fromUtf8(parts[1]);
            QString os = QString::fromUtf8(parts[2]);
            QString mac = QString::fromUtf8(parts[3]);
            QString ip = peer.toString();
            if (localIps.contains(ip) || discoveredMacs.contains(mac)) continue;
            discoveredMacs.insert(mac);

            qint64 ping = discoveryTimer.elapsed();
            ui->treePeers->addTopLevelItem(
                new QTreeWidgetItem({hostname, ip, os, mac, QString::number(ping)}));
        }
    }
}

// --- SENDING ---

// -----------------------------------------------------------------------------
// User selected a file: store path and update UI.
// -----------------------------------------------------------------------------
void MainWindow::on_btnSelectFile_clicked() {
    QString f = QFileDialog::getOpenFileName(
        this, "Select File to Send", {}, "All Files (*)"
        );
    if (f.isEmpty()) return;
    selectedFilePath = f;
    ui->leFilePath->setText(f);
}

// -----------------------------------------------------------------------------
// User clicked Send: find selected peer and kick off transfer request.
// -----------------------------------------------------------------------------
void MainWindow::on_btnSend_clicked() {
    auto *it = ui->treePeers->currentItem();
    if (!it || selectedFilePath.isEmpty()) return;
    startFileSend(it->text(static_cast<int>(PeerColumn::IP)), selectedFilePath);
}


// -----------------------------------------------------------------------------
// Initiate TCP connection for a new SendJob (does NOT yet send file).
// -----------------------------------------------------------------------------
void MainWindow::startFileSend(const QString& peerIp, const QString& filePath) {
    QFileInfo fi(filePath);
    lastSentFileName = fi.fileName();
    lastSentFileSize = fi.size();

    auto job = std::make_unique<SendJob>();
    job->filePath = filePath;
    job->totalBytes = fi.size();
    job->socket = new QTcpSocket(this);
    job->progress = new QProgressDialog(
        tr("Connecting…"), tr("Cancel"), 0, 0, this
        );
    job->progress->setWindowModality(Qt::WindowModal);
    job->progress->show();

    connect(job->socket, &QTcpSocket::connected, this, &MainWindow::onSendConnected);
    connect(job->socket, &QTcpSocket::bytesWritten, this, &MainWindow::onSendBytesWritten);
    connect(job->socket, &QTcpSocket::readyRead, this, &MainWindow::onSendReadyRead);
    connect(job->socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &MainWindow::onSendError
            );

    job->socket->connectToHost(peerIp, AppConstants::TRANSFER_PORT);
    sendJobs[job->socket] = std::move(job);
}

// -----------------------------------------------------------------------------
// Once connected, send our TRANSFER_REQUEST with filename + size.
// -----------------------------------------------------------------------------
void MainWindow::onSendConnected() {
    auto sock = qobject_cast<QTcpSocket*>(sender());
    auto it = sendJobs.find(sock);
    if (it == sendJobs.end()) return;

    it->second->progress->setLabelText(tr("Sending transfer request…"));
    sendTransferRequest(it->second.get());
}

// -----------------------------------------------------------------------------
// Sends an initial transfer request to the connected peer.
// -----------------------------------------------------------------------------
void MainWindow::sendTransferRequest(SendJob* job) {
    QByteArray buf;
    QDataStream ds(&buf, QIODevice::WriteOnly);
    QFileInfo fi(job->filePath);
    ds << AppConstants::TRANSFER_REQUEST
       << QHostInfo::localHostName()
       << fi.fileName()
       << fi.size();
    job->socket->write(buf);
    job->socket->flush();
}

// -----------------------------------------------------------------------------
// Handle all responses from the receiver: ACCEPT/REJECT or final ACK.
// -----------------------------------------------------------------------------
void MainWindow::onSendReadyRead() {
    auto sock = qobject_cast<QTcpSocket*>(sender());
    auto it = sendJobs.find(sock);
    if (it == sendJobs.end()) return;
    auto& job = *it->second;

    QByteArray resp = sock->readAll();
    if (resp == AppConstants::TRANSFER_ACCEPT) {
        // Transfer phase
        job.state = SendState::Sending;
        job.progress->setLabelText(tr("Sending the file…"));
        job.progress->setRange(0, 100);
        job.progress->setValue(0);

        job.file = new QFile(job.filePath, this);
        if (!job.file->open(QIODevice::ReadOnly)) {
            statusBar()->showMessage(tr("File could not be opened: %1").arg(job.filePath));
            sendJobs.erase(sock);
            sock->close();
            return;
        }
        job.expectedHash = calculateFileHash(job.filePath);
        writeHeaderAndFirstChunk(&job);
    }
    else if (resp == AppConstants::TRANSFER_REJECT) {
        // Return resources
        job.progress->close();
        sock->disconnect();
        sendJobs.erase(sock);
        sock->disconnectFromHost();
        sock->deleteLater();
        // Notify user
        QMessageBox::information(this, tr("Rejected"),
                                 tr("The receiver refused the file transfer."));
        return;
    }
    else if (resp == AppConstants::ACK_SUCCESS || resp == AppConstants::ACK_HASH_ERROR) {
        // Return resources
        job.progress->close();
        disconnect(sock,
                   QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
                   this, &MainWindow::onSendError);
        // Add to list
        ui->listSent->addItem(
            tr("%1 (%2 byte)").arg(lastSentFileName).arg(lastSentFileSize));
        sendJobs.erase(sock);
        sock->disconnectFromHost();
        sock->deleteLater();
        // Notify user
        if (resp == AppConstants::ACK_SUCCESS) {
            QMessageBox::information(this, tr("File Transfer Completed"),
                                     tr("'%1' sent successfully.").arg(lastSentFileName));
        } else {
            QMessageBox::warning(this, tr("File Transfer Error"),
                                 tr("The receiver has failed hash verification."));
        }
    }
}

// -----------------------------------------------------------------------------
// During active sending, write next chunk when bytesWritten signal fires.
// -----------------------------------------------------------------------------
void MainWindow::onSendBytesWritten(qint64 bytes) {
    auto sock = qobject_cast<QTcpSocket*>(sender());
    auto it = sendJobs.find(sock);
    if (it==sendJobs.end()) return;
    auto& job = *it->second;
    if (job.state != SendState::Sending) return;

    job.bytesWritten += bytes;
    job.progress->setValue(int(100 * job.bytesWritten / job.totalBytes));
    if (job.bytesWritten < job.totalBytes) {
        sendNextChunk(&job);
    }
}

// -----------------------------------------------------------------------------
// Write header + first chunk of the file.
// -----------------------------------------------------------------------------
void MainWindow::writeHeaderAndFirstChunk(SendJob* job) {
    QByteArray header;
    QDataStream ds(&header, QIODevice::WriteOnly);
    ds << QFileInfo(job->filePath).fileName()
       << quint64(job->totalBytes)
       << job->expectedHash;
    quint32 hlen = qToLittleEndian(header.size());

    job->socket->write(reinterpret_cast<const char*>(&hlen), sizeof(hlen));
    job->socket->write(header);
    sendNextChunk(job);
}

// -----------------------------------------------------------------------------
// Read next block from disk and write to socket.
// -----------------------------------------------------------------------------
void MainWindow::sendNextChunk(SendJob* job) {
    qint64 rem = job->totalBytes - job->bytesWritten;
    if (rem <= 0) return;
    auto chunk = job->file->read(qMin(AppConstants::CHUNK_SIZE, rem));
    job->socket->write(chunk);
}

// -----------------------------------------------------------------------------
// Handle any socket errors during sending.
// -----------------------------------------------------------------------------
void MainWindow::onSendError(QAbstractSocket::SocketError error) {
    auto sock = qobject_cast<QTcpSocket*>(sender());
    if (error == QAbstractSocket::RemoteHostClosedError && sendJobs.count(sock)==0) {
        sock->deleteLater();
        return;
    }
    if (auto it = sendJobs.find(sock); it!=sendJobs.end()) {
        it->second->progress->close();
        sendJobs.erase(it);
    }
    sock->deleteLater();
    statusBar()->showMessage(tr("File Transfer Error: %1").arg(sock->errorString()));
}

// --- RECEIVING ---

// -----------------------------------------------------------------------------
// New incoming TCP connection: allocate ReceiveJob and start reading.
// -----------------------------------------------------------------------------
void MainWindow::onNewConnection() {
    auto sock = fileServer->nextPendingConnection();
    auto job = std::make_unique<ReceiveJob>();
    connect(sock, &QTcpSocket::readyRead, this, &MainWindow::onReceiveReadyRead);
    connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
    receiveJobs[sock] = std::move(job);
}

// -----------------------------------------------------------------------------
// Incoming data on an active receive socket: append buffer and dispatch.
// -----------------------------------------------------------------------------
void MainWindow::onReceiveReadyRead() {
    auto sock = qobject_cast<QTcpSocket*>(sender());
    auto it = receiveJobs.find(sock);
    if (it == receiveJobs.end()) return;
    auto& job = *it->second;

    job.buffer.append(sock->readAll());
    processReceive(sock, &job);
}


// -----------------------------------------------------------------------------
// Open the Downloads folder in OS file browser.
// -----------------------------------------------------------------------------
void MainWindow::on_btnOpenReceivedFolder_clicked() {
    QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dl));
}

// -----------------------------------------------------------------------------
// State machine: handle request, header, body, and completion phases.
// -----------------------------------------------------------------------------
void MainWindow::processReceive(QTcpSocket* sock, ReceiveJob* job) {
    // TRANSFER REQUEST ---
    if (job->state == ReceiveState::AwaitingRequest) {
        QDataStream stream(&job->buffer, QIODevice::ReadOnly);
        stream.startTransaction();

        QByteArray requestType;
        QString senderHostname, fileName;
        qint64 fileSize;
        stream >> requestType >> senderHostname >> fileName >> fileSize;
        if (!stream.commitTransaction())
            return;

        int pos = stream.device()->pos();
        job->buffer.remove(0, pos);

        // Ask to user
        QMessageBox dlg(QMessageBox::Question,
                        tr("File Transfer Request"),
                        tr("Sender: %1 (%2)\nFile: %3\nSize: %4 byte")
                            .arg(senderHostname, sock->peerAddress().toString())
                            .arg(fileName).arg(fileSize),
                        QMessageBox::NoButton, this);
        auto *ok = dlg.addButton(tr("Accept"), QMessageBox::AcceptRole);
        dlg.addButton(tr("Reject"), QMessageBox::RejectRole);
        dlg.exec();

        if (dlg.clickedButton() == ok) {
            sock->write(AppConstants::TRANSFER_ACCEPT);
            sock->flush();
            job->state = ReceiveState::AwaitingFileHeader;
        } else {
            sock->write(AppConstants::TRANSFER_REJECT);
            sock->flush();
            receiveJobs.erase(sock);
            sock->close();
            return;
        }
    }

    // --- HEADER ---
    if (job->state == ReceiveState::AwaitingFileHeader) {
        if (job->buffer.size() < (int)sizeof(quint32)) return;
        auto ptr = reinterpret_cast<const uchar*>(job->buffer.constData());
        quint32 hlen = qFromLittleEndian<quint32>(ptr);
        if (job->buffer.size() < (int)sizeof(quint32) + hlen) return;

        job->buffer.remove(0, sizeof(quint32));
        QByteArray hdr = job->buffer.left(hlen);
        job->buffer.remove(0, hlen);

        QDataStream ds(&hdr, QIODevice::ReadOnly);
        quint64 fileSize;
        ds >> job->fileName >> fileSize >> job->expectedHash;
        job->fileBytesRemaining = qint64(fileSize);
        job->totalBytes = qint64(fileSize);

        QString dl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dl.isEmpty()) dl = QDir::homePath();
        QDir().mkpath(dl);

        job->file = new QFile(dl + QDir::separator() + job->fileName, this);
        if (!job->file->open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, tr("Error"), tr("File could not be opened: %1").arg(job->fileName));
            receiveJobs.erase(sock);
            sock->close();
            return;
        }
        job->progress = new QProgressDialog(tr("Downloading…"), tr("Cancel"), 0, 100, this);
        job->progress->setWindowModality(Qt::WindowModal);
        job->state = ReceiveState::ReceivingFileBody;
    }

    // --- BODY ---
    if (job->state == ReceiveState::ReceivingFileBody && job->fileBytesRemaining>0 && !job->buffer.isEmpty()) {
        qint64 toWrite = qMin(qint64(job->buffer.size()), job->fileBytesRemaining);
        job->file->write(job->buffer.constData(), toWrite);
        job->buffer.remove(0, toWrite);
        job->fileBytesRemaining -= toWrite;
        if (job->progress) {
            job->progress->setValue(int(100 * (job->totalBytes - job->fileBytesRemaining) / job->totalBytes));
        }
    }

    // --- DONE ---
    if (job->state == ReceiveState::ReceivingFileBody && job->fileBytesRemaining == 0) {
        if (job->progress) {
            job->progress->setValue(job->progress->maximum());
            job->progress->close();
        }
        if (job->file) job->file->close();

        QString savePath = job->file->fileName();
        bool ok = (calculateFileHash(savePath) == job->expectedHash);

        // Send ACK
        sock->write(ok ? AppConstants::ACK_SUCCESS : AppConstants::ACK_HASH_ERROR);
        sock->flush();

        // Add to Received List
        qint64 receivedSize = QFileInfo(savePath).size();
        ui->listReceived->addItem(
            tr("%1 (%2 byte) %3")
                .arg(job->fileName)
                .arg(receivedSize)
                .arg(ok ? "✅" : "❌")
            );

        QString nameCopy = job->fileName;
        qint64 sizeCopy = QFileInfo(savePath).size();
        QString integrity = ok ? tr("Confirmed") : tr("Error");

        // Return resources
        receiveJobs.erase(sock);
        sock->disconnectFromHost();
        sock->deleteLater();

        // Notify user
        QMessageBox::information(this, tr("Download Completed"),
                                 tr("File: %1\nSize: %2 byte\nSaved: %3\nIntegrity: %4")
                                     .arg(nameCopy)
                                     .arg(sizeCopy)
                                     .arg(savePath)
                                     .arg(integrity));
    }
}

// -----------------------------------------------------------------------------
// Compute SHA256 hash for given file (used for integrity check).
// -----------------------------------------------------------------------------
QString MainWindow::calculateFileHash(const QString& filePath) const {
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd())
            hasher.addData(f.read(AppConstants::CHUNK_SIZE));
    }
    return hasher.result().toHex();
}



