#include "pobs_service.hpp"
#include "net/pobs_ws_server.hpp"
#include "adb/adb_manager.hpp"

#include <obs-module.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <unistd.h>
#include <array>
#include <sstream>
#include <iostream>

namespace pobs {

PobsService& PobsService::instance() {
    static PobsService s_instance;
    return s_instance;
}

PobsService::PobsService()
    : ws_server_(std::make_unique<PobsWebSocketServer>()),
      adb_manager_(std::make_unique<AdbManager>()) {

    if (ws_server_->start(8765)) {
        blog(LOG_INFO, "[POBS] Control WebSocket active on port 8765 (LAN IP: %s)",
             PobsWebSocketServer::get_local_lan_ip().toUtf8().constData());
    } else {
        blog(LOG_WARNING, "[POBS] Failed to bind WebSocket server on port 8765");
    }

    adb_manager_->set_callbacks([this](const std::vector<LogicalDevice>& devices) {
        bool connected = false;
        std::string summary = "No device";
        for (const auto& dev : devices) {
            if (dev.state == DeviceState::CONNECTED) {
                connected = true;
                std::string ep = dev.get_active_endpoint();
                summary = dev.model.empty() ? ep : (dev.model + " (" + ep + ")");
                adb_manager_->reverse_port(ep, 8765, 8765);
                adb_manager_->reverse_port(ep, 8766, 8766);
                break;
            }
        }

        AdbStatusCallback cb;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_adb_connected_ = connected;
            adb_summary_ = summary;
            cb = adb_cb_;
        }
        if (cb) {
            cb(connected, summary);
        }
    }, [](const std::string& msg) {
        blog(LOG_INFO, "[POBS ADB] %s", msg.c_str());
    });
    adb_manager_->start_background_monitoring(3);

    QObject::connect(ws_server_.get(), &PobsWebSocketServer::client_connected, [this](const QString &peer) {
        blog(LOG_INFO, "[POBS] Companion app connected from %s", peer.toUtf8().constData());
        StatusCallback cb;
        CompanionStatus copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.is_connected = true;
            copy = status_;
            cb = status_cb_;
        }
        if (cb) cb(copy);
    });

    QObject::connect(ws_server_.get(), &PobsWebSocketServer::client_disconnected, [this](const QString &peer) {
        blog(LOG_INFO, "[POBS] Companion app disconnected (%s)", peer.toUtf8().constData());
        StatusCallback cb;
        CompanionStatus copy;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.is_connected = false;
            copy = status_;
            cb = status_cb_;
        }
        if (cb) cb(copy);
    });

    QObject::connect(ws_server_.get(), &PobsWebSocketServer::message_received, [this](const QString &msg) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(msg.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return;
        }

        QJsonObject root = doc.object();
        QString type = root.value("type").toString();

        StatusCallback cb;
        CompanionStatus copy;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (type == "hello") {
                status_.device_model = root.value("device_model").toString("Pixel").toStdString();
                status_.device_ip = root.value("device_ip").toString().toStdString();
                status_.active_camera = root.value("active_camera").toString("front").toStdString();
                status_.active_resolution = root.value("active_resolution").toString("1920x1080").toStdString();
                status_.active_fps = root.value("active_fps").toInt(60);
                if (root.contains("battery")) {
                    QJsonObject bat = root.value("battery").toObject();
                    status_.battery_level = bat.value("level").toInt(-1);
                    status_.is_charging = bat.value("is_charging").toBool(false);
                }
                status_.is_connected = true;
                blog(LOG_INFO, "[POBS] Handshake: %s (camera: %s, %s @ %d fps)",
                     status_.device_model.c_str(), status_.active_camera.c_str(),
                     status_.active_resolution.c_str(), status_.active_fps);
            } else if (type == "battery_update") {
                status_.battery_level = root.value("level").toInt(-1);
                status_.is_charging = root.value("is_charging").toBool(false);
            } else if (type == "camera_switched") {
                status_.active_camera = root.value("facing").toString().toStdString();
                blog(LOG_INFO, "[POBS] Camera switched in-app to: %s", status_.active_camera.c_str());
            } else if (type == "torch_switched") {
                status_.torch_enabled = root.value("enabled").toBool(false);
                blog(LOG_INFO, "[POBS] Torch state updated: %d", status_.torch_enabled);
            } else if (type == "format_updated") {
                status_.active_resolution = root.value("resolution").toString().toStdString();
                status_.active_fps = root.value("fps").toInt(60);
                blog(LOG_INFO, "[POBS] Format updated: %s @ %d fps", status_.active_resolution.c_str(), status_.active_fps);
            } else if (type == "zoom_updated") {
                status_.zoom_ratio = (float)root.value("ratio").toDouble(1.0);
            } else if (type == "stream_state_updated") {
                status_.is_streaming_active = root.value("enabled").toBool(true);
            }
            copy = status_;
            cb = status_cb_;
        }

        if (cb) cb(copy);
    });
}

PobsService::~PobsService() {
    if (adb_manager_) {
        adb_manager_->stop_background_monitoring();
    }
}

void PobsService::set_status_callback(StatusCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_cb_ = std::move(cb);
}

CompanionStatus PobsService::get_status() {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void PobsService::switch_camera(const std::string& facing) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active_camera = facing;
    }
    if (ws_server_) {
        QString msg = QString("{\"type\":\"set_camera\",\"facing\":\"%1\"}").arg(QString::fromStdString(facing));
        ws_server_->broadcast_text(msg);
    }
}

void PobsService::toggle_torch(bool enable) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.torch_enabled = enable;
    }
    if (ws_server_) {
        QString msg = QString("{\"type\":\"set_torch\",\"enabled\":%1}").arg(enable ? "true" : "false");
        ws_server_->broadcast_text(msg);
    }
}

void PobsService::set_format(const std::string& resolution, int fps) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.active_resolution = resolution;
        status_.active_fps = fps;
    }
    if (ws_server_) {
        QString msg = QString("{\"type\":\"set_format\",\"resolution\":\"%1\",\"fps\":%2}")
            .arg(QString::fromStdString(resolution))
            .arg(fps);
        ws_server_->broadcast_text(msg);
    }
}

void PobsService::set_zoom(float ratio) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.zoom_ratio = ratio;
    }
    if (ws_server_) {
        QString msg = QString("{\"type\":\"set_zoom\",\"ratio\":%1}").arg(ratio, 0, 'f', 2);
        ws_server_->broadcast_text(msg);
    }
}

void PobsService::set_streaming(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.is_streaming_active = enabled;
    }
    if (ws_server_) {
        QString msg = QString("{\"type\":\"set_streaming\",\"enabled\":%1}").arg(enabled ? "true" : "false");
        ws_server_->broadcast_text(msg);
    }
}

PobsWebSocketServer& PobsService::ws() {
    return *ws_server_;
}

AdbManager& PobsService::adb() {
    return *adb_manager_;
}

bool PobsService::is_adb_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_adb_connected_;
}

std::string PobsService::get_adb_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adb_summary_;
}

void PobsService::set_adb_status_callback(AdbStatusCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    adb_cb_ = std::move(cb);
}

bool PobsService::connect_adb(const std::string& host_port, std::string& out_msg) {
    if (!adb_manager_) {
        out_msg = "ADB manager not initialized";
        return false;
    }
    return adb_manager_->connect_wireless(host_port, out_msg);
}

bool PobsService::pair_adb(const std::string& host_port, const std::string& code, std::string& out_msg) {
    if (!adb_manager_) {
        out_msg = "ADB manager not initialized";
        return false;
    }
    return adb_manager_->pair(host_port, code, out_msg);
}

} // namespace pobs

extern "C" bool pobs_is_stream_active(void) {
    return pobs::PobsService::instance().get_status().is_streaming_active;
}

extern "C" void pobs_set_stream_active(bool active) {
    pobs::PobsService::instance().set_streaming(active);
}
