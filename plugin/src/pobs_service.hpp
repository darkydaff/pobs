#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

namespace pobs {

class PobsWebSocketServer;
class AdbManager;

struct CompanionStatus {
    std::string device_model = "Disconnected";
    std::string device_ip;
    int battery_level = -1;
    bool is_charging = false;
    std::string active_camera = "front";
    std::string active_resolution = "1920x1080";
    int active_fps = 60;
    bool torch_enabled = false;
    float zoom_ratio = 1.0f;
    bool is_streaming_active = true;
    bool is_connected = false;
};

class PobsService {
public:
    static PobsService& instance();

    PobsService();
    ~PobsService();

    using StatusCallback = std::function<void(const CompanionStatus&)>;
    void set_status_callback(StatusCallback cb);

    using AdbStatusCallback = std::function<void(bool connected, const std::string& summary)>;
    void set_adb_status_callback(AdbStatusCallback cb);

    CompanionStatus get_status();
    void switch_camera(const std::string& facing);
    void toggle_torch(bool enable);
    void set_format(const std::string& resolution, int fps);
    void set_zoom(float ratio);
    void set_streaming(bool enabled);

    PobsWebSocketServer& ws();
    AdbManager& adb();

    bool is_adb_connected() const;
    std::string get_adb_summary() const;
    bool connect_adb(const std::string& host_port, std::string& out_msg);
    bool pair_adb(const std::string& host_port, const std::string& code, std::string& out_msg);

private:
    std::unique_ptr<PobsWebSocketServer> ws_server_;
    std::unique_ptr<AdbManager> adb_manager_;

    mutable std::mutex mutex_;
    CompanionStatus status_;
    StatusCallback status_cb_;

    bool is_adb_connected_ = false;
    std::string adb_summary_ = "Disconnected";
    AdbStatusCallback adb_cb_;
};

} // namespace pobs

#ifdef __cplusplus
extern "C" {
#endif

bool pobs_is_stream_active(void);
void pobs_set_stream_active(bool active);

#ifdef __cplusplus
}
#endif
