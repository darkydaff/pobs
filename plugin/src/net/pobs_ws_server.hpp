#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QList>
#include <QString>
#include <QByteArray>
#include <memory>

namespace pobs {

class PobsWebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit PobsWebSocketServer(QObject *parent = nullptr);
    ~PobsWebSocketServer() override;

    bool start(quint16 port = 8765);
    void stop();

    quint16 port() const { return port_; }
    bool is_client_connected() const;
    QString connected_client_address() const;

    void broadcast_text(const QString &text);
    void send_text(QTcpSocket *client, const QString &text);

    static QString get_local_lan_ip();

signals:
    void client_connected(const QString &peer_address);
    void client_disconnected(const QString &peer_address);
    void message_received(const QString &message);

private slots:
    void on_new_connection();
    void on_socket_ready_read();
    void on_socket_disconnected();

private:
    struct ClientSession {
        QTcpSocket *socket = nullptr;
        bool handshake_complete = false;
        QByteArray buffer;
    };

    void process_handshake(ClientSession &session);
    void process_websocket_frames(ClientSession &session);
    static QByteArray create_text_frame(const QString &text);

    QTcpServer *tcp_server_ = nullptr;
    quint16 port_ = 8765;
    QList<ClientSession> clients_;
};

} // namespace pobs
