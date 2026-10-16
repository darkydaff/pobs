#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include "net/pobs_ws_server.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    pobs::PobsWebSocketServer server;
    if (!server.start(8765)) {
        std::cerr << "Failed to start server on 8765" << std::endl;
        return 1;
    }

    std::cout << "TEST_WS_LIVE: Listening on 0.0.0.0:8765 (LAN: "
              << pobs::PobsWebSocketServer::get_local_lan_ip().toStdString() << ")" << std::endl;

    QObject::connect(&server, &pobs::PobsWebSocketServer::client_connected, [&](const QString &peer) {
        std::cout << "TEST_WS_LIVE: Phone connected from " << peer.toStdString() << "!" << std::endl;
    });

    QObject::connect(&server, &pobs::PobsWebSocketServer::message_received, [&](const QString &msg) {
        std::cout << "TEST_WS_LIVE RECEIVED: " << msg.toStdString() << std::endl;
        std::cout << "TEST_WS_LIVE: Sending set_camera -> back to phone..." << std::endl;
        server.broadcast_text("{\"type\":\"set_camera\",\"facing\":\"back\"}");
        QTimer::singleShot(1500, &app, &QCoreApplication::quit);
    });

    // Timeout safety: 25 seconds
    QTimer::singleShot(25000, &app, &QCoreApplication::quit);

    return app.exec();
}
