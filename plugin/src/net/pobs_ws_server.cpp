#include "pobs_ws_server.hpp"

#include <QCryptographicHash>
#include <QNetworkInterface>
#include <QDebug>
#include <endian.h>

namespace pobs {

static const char WS_MAGIC_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

PobsWebSocketServer::PobsWebSocketServer(QObject *parent)
    : QObject(parent), tcp_server_(new QTcpServer(this)) {
    connect(tcp_server_, &QTcpServer::newConnection, this, &PobsWebSocketServer::on_new_connection);
}

PobsWebSocketServer::~PobsWebSocketServer() {
    stop();
}

bool PobsWebSocketServer::start(quint16 port) {
    stop();
    port_ = port;
    if (!tcp_server_->listen(QHostAddress::AnyIPv4, port_)) {
        return false;
    }
    return true;
}

void PobsWebSocketServer::stop() {
    if (tcp_server_ && tcp_server_->isListening()) {
        tcp_server_->close();
    }
    for (auto &session : clients_) {
        if (session.socket) {
            session.socket->disconnect(this);
            session.socket->close();
            session.socket->deleteLater();
        }
    }
    clients_.clear();
}

bool PobsWebSocketServer::is_client_connected() const {
    for (const auto &session : clients_) {
        if (session.handshake_complete && session.socket && session.socket->state() == QAbstractSocket::ConnectedState) {
            return true;
        }
    }
    return false;
}

QString PobsWebSocketServer::connected_client_address() const {
    for (const auto &session : clients_) {
        if (session.handshake_complete && session.socket) {
            return session.socket->peerAddress().toString();
        }
    }
    return QString();
}

void PobsWebSocketServer::on_new_connection() {
    while (tcp_server_->hasPendingConnections()) {
        QTcpSocket *socket = tcp_server_->nextPendingConnection();
        if (!socket) continue;

        ClientSession session;
        session.socket = socket;
        session.handshake_complete = false;
        clients_.append(session);

        qDebug() << "[WS SERVER] Incoming TCP connection from:" << socket->peerAddress().toString();

        connect(socket, &QTcpSocket::readyRead, this, &PobsWebSocketServer::on_socket_ready_read);
        connect(socket, &QTcpSocket::disconnected, this, &PobsWebSocketServer::on_socket_disconnected);
    }
}

void PobsWebSocketServer::on_socket_disconnected() {
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;

    QString peer = socket->peerAddress().toString();
    for (int i = 0; i < clients_.size(); ++i) {
        if (clients_[i].socket == socket) {
            bool was_complete = clients_[i].handshake_complete;
            clients_.removeAt(i);
            if (was_complete) {
                emit client_disconnected(peer);
            }
            break;
        }
    }
    socket->deleteLater();
}

void PobsWebSocketServer::on_socket_ready_read() {
    auto *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket) return;

    ClientSession *session = nullptr;
    for (auto &s : clients_) {
        if (s.socket == socket) {
            session = &s;
            break;
        }
    }
    if (!session) return;

    session->buffer.append(socket->readAll());

    if (!session->handshake_complete) {
        process_handshake(*session);
    } else {
        process_websocket_frames(*session);
    }
}

void PobsWebSocketServer::process_handshake(ClientSession &session) {
    int header_end = session.buffer.indexOf("\r\n\r\n");
    if (header_end == -1) {
        return; // Wait for complete HTTP headers
    }

    QByteArray request = session.buffer.left(header_end);
    session.buffer.remove(0, header_end + 4);

    QList<QByteArray> lines = request.split('\n');
    QByteArray sec_key;

    for (const auto &line : lines) {
        QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("Sec-WebSocket-Key:") || trimmed.startsWith("sec-websocket-key:")) {
            int colon = trimmed.indexOf(':');
            if (colon != -1) {
                sec_key = trimmed.mid(colon + 1).trimmed();
            }
        }
    }

    if (sec_key.isEmpty()) {
        session.socket->close();
        return;
    }

    QByteArray accept_key = QCryptographicHash::hash(sec_key + WS_MAGIC_GUID, QCryptographicHash::Sha1).toBase64();

    QByteArray response = "HTTP/1.1 101 Switching Protocols\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: " + accept_key + "\r\n\r\n";

    session.socket->write(response);
    session.socket->flush();
    session.handshake_complete = true;

    emit client_connected(session.socket->peerAddress().toString());

    // Process any remaining bytes as WebSocket frames
    if (!session.buffer.isEmpty()) {
        process_websocket_frames(session);
    }
}

void PobsWebSocketServer::process_websocket_frames(ClientSession &session) {
    while (session.buffer.size() >= 2) {
        const uint8_t *data = reinterpret_cast<const uint8_t *>(session.buffer.constData());
        uint8_t b1 = data[0];
        uint8_t b2 = data[1];

        uint8_t opcode = b1 & 0x0F;
        bool is_masked = (b2 & 0x80) != 0;
        uint64_t payload_len = b2 & 0x7F;

        int offset = 2;
        if (payload_len == 126) {
            if (session.buffer.size() < offset + 2) return;
            payload_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
            offset += 2;
        } else if (payload_len == 127) {
            if (session.buffer.size() < offset + 8) return;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | data[offset + i];
            }
            offset += 8;
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (is_masked) {
            if (session.buffer.size() < offset + 4) return;
            for (int i = 0; i < 4; ++i) mask[i] = data[offset + i];
            offset += 4;
        }

        if (static_cast<uint64_t>(session.buffer.size()) < offset + payload_len) {
            return; // Wait for full frame
        }

        QByteArray payload(payload_len, '\0');
        char *payload_raw = payload.data();
        const char *src = session.buffer.constData() + offset;

        if (is_masked) {
            for (uint64_t i = 0; i < payload_len; ++i) {
                payload_raw[i] = src[i] ^ mask[i % 4];
            }
        } else {
            memcpy(payload_raw, src, payload_len);
        }

        // Consume frame
        session.buffer.remove(0, offset + payload_len);

        if (opcode == 0x1) { // Text frame
            QString msg = QString::fromUtf8(payload);
            emit message_received(msg);
        } else if (opcode == 0x8) { // Close
            session.socket->close();
            return;
        } else if (opcode == 0x9) { // Ping -> Pong
            QByteArray pong;
            pong.append(static_cast<char>(0x8A)); // FIN + Pong
            pong.append(static_cast<char>(0x00));
            session.socket->write(pong);
            session.socket->flush();
        }
    }
}

QByteArray PobsWebSocketServer::create_text_frame(const QString &text) {
    QByteArray payload = text.toUtf8();
    QByteArray frame;
    frame.append(static_cast<char>(0x81)); // FIN + Text frame

    uint64_t len = payload.size();
    if (len <= 125) {
        frame.append(static_cast<char>(len));
    } else if (len <= 65535) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    } else {
        frame.append(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.append(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.append(payload);
    return frame;
}

void PobsWebSocketServer::broadcast_text(const QString &text) {
    QByteArray frame = create_text_frame(text);
    for (auto &session : clients_) {
        if (session.handshake_complete && session.socket && session.socket->state() == QAbstractSocket::ConnectedState) {
            session.socket->write(frame);
            session.socket->flush();
        }
    }
}

void PobsWebSocketServer::send_text(QTcpSocket *client, const QString &text) {
    if (!client || client->state() != QAbstractSocket::ConnectedState) return;
    QByteArray frame = create_text_frame(text);
    client->write(frame);
    client->flush();
}

QString PobsWebSocketServer::get_local_lan_ip() {
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto &iface : interfaces) {
        // Skip loopback, virtual bridges, and down interfaces
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !iface.flags().testFlag(QNetworkInterface::IsUp) ||
            iface.name().startsWith("docker") ||
            iface.name().startsWith("br-") ||
            iface.name().startsWith("tun")) {
            continue;
        }

        const auto entries = iface.addressEntries();
        for (const auto &entry : entries) {
            const auto ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol && !ip.isLoopback()) {
                return ip.toString();
            }
        }
    }
    return "127.0.0.1";
}

} // namespace pobs
