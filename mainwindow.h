#pragma once

#include <QMainWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QFile>
#include <QProgressDialog>
#include <QSet>
#include <memory>
#include <unordered_map>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace AppConstants {

/// @brief Size of each file chunk in bytes.
static constexpr qint64 CHUNK_SIZE = 64 * 1024;

/// @brief UDP port used for peer discovery.
static constexpr quint16 DISCOVERY_PORT = 45454;

/// @brief TCP port used for file transfer.
static constexpr quint16 TRANSFER_PORT = 45455;

/// @brief Discovery request magic string.
const QByteArray DISCOVER_REQUEST  = "DISCOVER_REQUEST_V1";

/// @brief Discovery response magic string.
const QByteArray DISCOVER_RESPONSE = "DISCOVER_RESPONSE_V1";

/// @brief File transfer request magic string.
const QByteArray TRANSFER_REQUEST = "TRANSFER_REQUEST_V1";

/// @brief File transfer accept magic string.
const QByteArray TRANSFER_ACCEPT = "TRANSFER_ACCEPT_V1";

/// @brief File transfer reject magic string.
const QByteArray TRANSFER_REJECT = "TRANSFER_REJECT_V1";

/// @brief Acknowledgement for successful transfer.
const QByteArray ACK_SUCCESS = "OK";

/// @brief Acknowledgement for hash mismatch.
const QByteArray ACK_HASH_ERROR = "HASH_ERROR";
}

/// @brief Columns in the peer list widget.
enum class PeerColumn : int {
    Hostname = 0,
    IP       = 1,
    OS       = 2,
    MAC      = 3,
    Ping     = 4
};

/// @brief Current state of an outgoing send operation.
enum class SendState {
    AwaitingResponse,  /// Waiting for receiver to accept or reject.
    Sending            /// Actively sending file data.
};

/// @brief Holds all information for an outgoing file transfer.
struct SendJob {
    QTcpSocket* socket = nullptr;       /// TCP socket for this transfer.
    QFile* file = nullptr;              /// File being sent.
    QProgressDialog* progress = nullptr;/// Progress dialog shown to user.
    qint64 totalBytes = 0;              /// Total size of the file.
    qint64 bytesWritten = 0;            /// Number of bytes already sent.
    QString expectedHash;               /// SHA-256 hash of the file.
    QString filePath;                   /// Full path to the file.
    SendState state = SendState::AwaitingResponse; /// Current send state.
};

/// @brief Current state of an incoming receive operation.
enum class ReceiveState {
    AwaitingRequest,    /// Waiting for initial transfer request packet.
    AwaitingFileHeader, /// Waiting for file header (name, size, hash).
    ReceivingFileBody   /// Actively receiving file data.
};

/// @brief Holds all information for an incoming file transfer.
struct ReceiveJob {
    ReceiveState state = ReceiveState::AwaitingRequest; /// Receive state machine.
    QByteArray buffer;                                  /// Buffer for incoming TCP data.
    bool headerParsed         = false;                  /// Whether the file header has been parsed.
    quint32 headerBytesRemaining = 0;                   /// Bytes left to read for header.
    QString fileName;                                   /// Name of the incoming file.
    qint64  fileBytesRemaining   = 0;                   /// Bytes left to write for file body.
    qint64  totalBytes   = 0;                           /// Total size of the file.
    QString expectedHash;                               /// SHA-256 hash from header.
    QFile* file = nullptr;                              /// File being written.
    QProgressDialog* progress = nullptr;                /// Progress dialog shown to user.
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:

    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow();

private slots:
    /** @name Discovery slots */

    /// @brief Sends a UDP broadcast to discover peers.
    void on_btnDiscover_clicked();

    /// @brief Handles incoming UDP datagrams for discovery.
    void onUdpReadyRead();

    /** @name Sending slots */

    /// @brief Opens file selection dialog.
    void on_btnSelectFile_clicked();

    /// @brief Initiates a transfer to the selected peer.
    void on_btnSend_clicked();

    /// @brief Called when TCP connection is established for send.
    void onSendConnected();

    /**
     * @brief Called as bytes are written to the socket.
     * @param bytes Number of bytes just written.
     */
    void onSendBytesWritten(qint64 bytes);

    /// @brief Reads control messages (accept/reject/ack) from receiver.
    void onSendReadyRead();

    /**
     *  @brief Handles any socket errors during sending.
     *  @param error The socket error encountered.
     */
    void onSendError(QAbstractSocket::SocketError error);

    /** @name Receiving slots */

    /// @brief Called when a new incoming TCP connection is detected.
    void onNewConnection();

    /// @brief Reads incoming data on the transfer socket.
    void onReceiveReadyRead();

    /** @name UI actions */

    /// @brief Opens the system file explorer at the downloads folder.
    void on_btnOpenReceivedFolder_clicked();

private:
    Ui::MainWindow *ui;

    // Discovery members
    QUdpSocket*   discoverSocket = nullptr; /// UDP socket for discovery.
    QStringList   localIps;                 /// Local IPv4 addresses to ignore self.
    QSet<QString> discoveredMacs;           /// MACs already seen so duplicates.

    // Receiving server
    QTcpServer*   fileServer = nullptr;     /// TCP server listening for incoming transfers.

    // State for send operations
    QString selectedFilePath;               /// Path of the file chosen to send.
    QString lastSentFileName;               /// Filename of last completed send.
    qint64  lastSentFileSize = 0;         /// Size of last completed send.

    // Active transfer jobs
    std::unordered_map<QTcpSocket*, std::unique_ptr<SendJob>> sendJobs;      /// Outgoing transfers.
    std::unordered_map<QTcpSocket*, std::unique_ptr<ReceiveJob>> receiveJobs;   /// Incoming transfers.

    /**
     * @brief Sends the initial transfer request packet.
     * @param job Pointer to the SendJob for which to send the request.
     */
    void sendTransferRequest(SendJob* job);

    /**
     * @brief Creates a SendJob and connects socket for transfer request.
     * @param peerIp IP address of the peer to send to.
     * @param filePath Local path of the file to send.
     */
    void startFileSend(const QString& peerIp, const QString& filePath);

    /**
     * @brief Writes the file header and first data chunk to the socket.
     * @param job SendJob containing file and socket info.
     */
    void writeHeaderAndFirstChunk(SendJob* job);

    /**
     * @brief Sends the next chunk of file data.
     * @param job SendJob containing file and socket info.
     */
    void sendNextChunk(SendJob* job);

    /**
     * @brief Advances the receive state machine, parsing header or body.
     * @param sock Socket on which data arrived.
     * @param job ReceiveJob tracking state and buffer.
     */
    void processReceive(QTcpSocket* sock, ReceiveJob* job);

    /**
     * @brief Computes the SHA-256 hash of a file.
     * @param filePath Path to the file to hash.
     * @return Hexadecimal representation of the SHA-256 digest.
     */
    QString calculateFileHash(const QString& filePath) const;
};
