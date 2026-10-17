#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <map>
#include <optional>

namespace pobs {

enum class TransportType {
    UNKNOWN,
    USB,
    WIRELESS
};

enum class DeviceState {
    DISCONNECTED,
    UNAUTHORIZED,
    OFFLINE,
    CONNECTED
};

struct AdbDevice {
    std::string endpoint;       // e.g., "192.168.1.89:45587" or "38251JEHN12832"
    std::string hardware_serial;// from getprop ro.serialno / ro.boot.serialno
    std::string model;          // e.g. "Pixel 7a"
    TransportType transport = TransportType::UNKNOWN;
    DeviceState state = DeviceState::DISCONNECTED;
    bool is_preferred = false;
};

struct LogicalDevice {
    std::string hardware_serial;
    std::string model;
    std::string wireless_endpoint;
    std::string usb_endpoint;
    bool wireless_available = false;
    bool usb_available = false;
    TransportType active_transport = TransportType::UNKNOWN;
    DeviceState state = DeviceState::DISCONNECTED;
    bool tunnels_active = false;

    std::string get_active_endpoint() const {
        if (active_transport == TransportType::WIRELESS && wireless_available) {
            return wireless_endpoint;
        }
        if (active_transport == TransportType::USB && usb_available) {
            return usb_endpoint;
        }
        if (wireless_available) return wireless_endpoint;
        if (usb_available) return usb_endpoint;
        return "";
    }
};

class AdbManager {
public:
    using DeviceListCallback = std::function<void(const std::vector<LogicalDevice>& devices)>;
    using LogCallback = std::function<void(const std::string& message)>;

    AdbManager();
    ~AdbManager();

    void set_callbacks(DeviceListCallback on_devices_updated, LogCallback on_log);

    // Asynchronous discovery triggers
    void start_background_monitoring(int interval_seconds = 3);
    void stop_background_monitoring();
    void trigger_refresh();

    // Synchronous calls (run on worker thread or caller thread)
    std::vector<AdbDevice> scan_raw_devices();
    std::vector<LogicalDevice> get_logical_devices();
    std::optional<LogicalDevice> get_preferred_or_first_device();

    // Pairing and connection
    bool pair(const std::string& host_port, const std::string& code, std::string& out_msg);
    bool connect_wireless(const std::string& host_port, std::string& out_msg);
    bool disconnect(const std::string& endpoint, std::string& out_msg);

    bool is_connected() const;
    std::string get_active_summary() const;

    // Port reversal & verification for companion WebSocket (8765) and Video stream (8766)
    bool reverse_port(const std::string& endpoint, int remote_port, int local_port);
    bool verify_tunnels(const std::string& endpoint, bool& port_8765, bool& port_8766);
    bool ensure_tunnels(const std::string& endpoint);
    void invalidate_tunnel_cache();

    // Auto-discovery of connect port (after pairing or for known device)
    std::string try_discover_connect_port(const std::string& ip);

    // Query hardware serial for deduplication
    std::string query_hardware_serial(const std::string& endpoint);
    std::string query_model_name(const std::string& endpoint);

private:
    void log(const std::string& msg);
    void monitor_loop();
    void update_logical_devices(const std::vector<AdbDevice>& raw_devices);

    mutable std::mutex mutex_;
    std::vector<LogicalDevice> logical_devices_;
    std::map<std::string, std::string> serial_cache_; // endpoint -> hardware_serial
    std::string last_verified_tunnel_endpoint_;

    std::atomic<bool> monitoring_{false};
    std::thread monitor_thread_;
    int interval_seconds_{3};

    DeviceListCallback on_devices_updated_;
    LogCallback on_log_;
};

} // namespace pobs
