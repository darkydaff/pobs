#include "adb_manager.hpp"
#include "../common/process_util.hpp"

#include <sstream>
#include <regex>
#include <chrono>

namespace pobs {

AdbManager::AdbManager() = default;

AdbManager::~AdbManager() {
    stop_background_monitoring();
}

void AdbManager::set_callbacks(DeviceListCallback on_devices_updated, LogCallback on_log) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_devices_updated_ = std::move(on_devices_updated);
    on_log_ = std::move(on_log);
}

void AdbManager::log(const std::string& msg) {
    LogCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = on_log_;
    }
    if (cb) {
        cb(msg);
    }
}

void AdbManager::start_background_monitoring(int interval_seconds) {
    if (monitoring_.load()) return;
    interval_seconds_ = interval_seconds;
    monitoring_ = true;
    monitor_thread_ = std::thread(&AdbManager::monitor_loop, this);
}

void AdbManager::stop_background_monitoring() {
    if (!monitoring_.load()) return;
    monitoring_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void AdbManager::trigger_refresh() {
    std::thread([this]() {
        auto raw = scan_raw_devices();
        update_logical_devices(raw);
    }).detach();
}

std::vector<AdbDevice> AdbManager::scan_raw_devices() {
    std::vector<AdbDevice> devices;
    ProcessResult res = run_process({"adb", "devices", "-l"}, 1500);
    if (res.exit_code != 0) {
        log("ADB error running 'adb devices -l': " + res.stderr_str);
        return devices;
    }

    std::istringstream stream(res.stdout_str);
    std::string line;
    static const std::regex ip_port_regex(R"(^\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d+$)");

    while (std::getline(stream, line)) {
        if (line.empty() || line.find("List of devices attached") != std::string::npos) {
            continue;
        }

        std::istringstream line_stream(line);
        std::string endpoint, state_str;
        if (!(line_stream >> endpoint >> state_str)) {
            continue;
        }

        AdbDevice dev;
        dev.endpoint = endpoint;

        if (std::regex_match(endpoint, ip_port_regex)) {
            dev.transport = TransportType::WIRELESS;
        } else {
            dev.transport = TransportType::USB;
        }

        if (state_str == "device") {
            dev.state = DeviceState::CONNECTED;
        } else if (state_str == "unauthorized") {
            dev.state = DeviceState::UNAUTHORIZED;
        } else if (state_str == "offline") {
            dev.state = DeviceState::OFFLINE;
        } else {
            dev.state = DeviceState::DISCONNECTED;
        }

        std::string token;
        while (line_stream >> token) {
            if (token.rfind("model:", 0) == 0) {
                dev.model = token.substr(6);
                std::replace(dev.model.begin(), dev.model.end(), '_', ' ');
            }
        }

        devices.push_back(dev);
    }

    return devices;
}

std::string AdbManager::query_hardware_serial(const std::string& endpoint) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = serial_cache_.find(endpoint);
        if (it != serial_cache_.end() && !it->second.empty()) {
            return it->second;
        }
    }

    ProcessResult res = run_process({"adb", "-s", endpoint, "shell", "getprop", "ro.serialno"}, 800);
    std::string serial;
    if (res.exit_code == 0) {
        serial = res.stdout_str;
        serial.erase(serial.find_last_not_of(" \n\r\t") + 1);
    }

    if (serial.empty()) {
        ProcessResult res2 = run_process({"adb", "-s", endpoint, "shell", "getprop", "ro.boot.serialno"}, 800);
        if (res2.exit_code == 0) {
            serial = res2.stdout_str;
            serial.erase(serial.find_last_not_of(" \n\r\t") + 1);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!serial.empty()) {
        serial_cache_[endpoint] = serial;
        return serial;
    } else {
        // Cache endpoint itself to avoid repeated query timeouts on stale endpoints
        serial_cache_[endpoint] = endpoint;
        return endpoint;
    }
}

std::string AdbManager::query_model_name(const std::string& endpoint) {
    ProcessResult res = run_process({"adb", "-s", endpoint, "shell", "getprop", "ro.product.model"}, 800);
    if (res.exit_code == 0 && !res.stdout_str.empty()) {
        std::string model = res.stdout_str;
        model.erase(model.find_last_not_of(" \n\r\t") + 1);
        return model;
    }
    return "";
}

void AdbManager::update_logical_devices(const std::vector<AdbDevice>& raw_devices) {
    std::map<std::string, LogicalDevice> logical_map;

    for (const auto& raw : raw_devices) {
        std::string hw_serial;
        if (raw.state == DeviceState::CONNECTED) {
            hw_serial = query_hardware_serial(raw.endpoint);
        }
        if (hw_serial.empty()) {
            hw_serial = raw.endpoint;
        }

        LogicalDevice& log_dev = logical_map[hw_serial];
        log_dev.hardware_serial = hw_serial;
        if (log_dev.model.empty()) {
            log_dev.model = raw.model.empty() ? query_model_name(raw.endpoint) : raw.model;
        }

        if (raw.transport == TransportType::WIRELESS) {
            log_dev.wireless_endpoint = raw.endpoint;
            log_dev.wireless_available = (raw.state == DeviceState::CONNECTED);
        } else if (raw.transport == TransportType::USB) {
            log_dev.usb_endpoint = raw.endpoint;
            log_dev.usb_available = (raw.state == DeviceState::CONNECTED);
        }

        if (raw.state == DeviceState::CONNECTED) {
            log_dev.state = DeviceState::CONNECTED;
        } else if (log_dev.state != DeviceState::CONNECTED) {
            log_dev.state = raw.state;
        }
    }

    std::vector<LogicalDevice> new_logical;
    DeviceListCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pair : logical_map) {
            LogicalDevice& d = pair.second;
            // Retain previous active transport if still available
            auto old_it = std::find_if(logical_devices_.begin(), logical_devices_.end(),
                [&](const LogicalDevice& old) { return old.hardware_serial == d.hardware_serial; });

            if (old_it != logical_devices_.end()) {
                if (old_it->active_transport == TransportType::WIRELESS && d.wireless_available) {
                    d.active_transport = TransportType::WIRELESS;
                } else if (old_it->active_transport == TransportType::USB && d.usb_available) {
                    d.active_transport = TransportType::USB;
                } else {
                    d.active_transport = d.wireless_available ? TransportType::WIRELESS :
                                         (d.usb_available ? TransportType::USB : TransportType::UNKNOWN);
                }
            } else {
                d.active_transport = d.wireless_available ? TransportType::WIRELESS :
                                     (d.usb_available ? TransportType::USB : TransportType::UNKNOWN);
            }
            new_logical.push_back(d);
        }

        logical_devices_ = new_logical;
        cb = on_devices_updated_;
    }

    if (cb) {
        cb(new_logical);
    }
}

std::vector<LogicalDevice> AdbManager::get_logical_devices() {
    std::lock_guard<std::mutex> lock(mutex_);
    return logical_devices_;
}

std::optional<LogicalDevice> AdbManager::get_preferred_or_first_device() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logical_devices_.empty()) {
        return std::nullopt;
    }
    return logical_devices_.front();
}

bool AdbManager::pair(const std::string& host_port, const std::string& code, std::string& out_msg) {
    if (host_port.empty() || code.empty()) {
        out_msg = "Host:port and pairing code must not be empty";
        return false;
    }

    log("Initiating ADB pairing to endpoint: " + host_port + " (pairing code omitted from log)");
    ProcessResult res = run_process({"adb", "pair", host_port, code}, 10000);
    out_msg = res.stdout_str + (res.stderr_str.empty() ? "" : (" " + res.stderr_str));

    if (res.exit_code == 0 && out_msg.find("Successfully paired") != std::string::npos) {
        log("Pairing successful for " + host_port);
        return true;
    }

    log("Pairing failed: " + out_msg);
    return false;
}

bool AdbManager::connect_wireless(const std::string& host_port, std::string& out_msg) {
    if (host_port.empty()) {
        out_msg = "Wireless host:port endpoint is empty";
        return false;
    }

    log("Connecting to wireless ADB endpoint: " + host_port);
    ProcessResult res = run_process({"adb", "connect", host_port}, 5000);
    out_msg = res.stdout_str + (res.stderr_str.empty() ? "" : (" " + res.stderr_str));

    if (res.exit_code == 0 && (out_msg.find("connected to") != std::string::npos ||
                              out_msg.find("already connected") != std::string::npos)) {
        log("Connected wirelessly to " + host_port);
        reverse_port(host_port, 8765, 8765);
        reverse_port(host_port, 8766, 8766);
        trigger_refresh();
        return true;
    }

    log("Failed to connect wirelessly: " + out_msg);
    return false;
}

bool AdbManager::is_connected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& dev : logical_devices_) {
        if (dev.state == DeviceState::CONNECTED) {
            return true;
        }
    }
    return false;
}

std::string AdbManager::get_active_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& dev : logical_devices_) {
        if (dev.state == DeviceState::CONNECTED) {
            std::string ep = dev.get_active_endpoint();
            if (!dev.model.empty()) {
                return dev.model + " (" + ep + ")";
            }
            return ep;
        }
    }
    return "";
}

bool AdbManager::reverse_port(const std::string& endpoint, int remote_port, int local_port) {
    std::vector<std::string> args = {"adb"};
    if (!endpoint.empty()) {
        args.push_back("-s");
        args.push_back(endpoint);
    }
    args.push_back("reverse");
    args.push_back("tcp:" + std::to_string(remote_port));
    args.push_back("tcp:" + std::to_string(local_port));
    ProcessResult res = run_process(args, 1000);
    return res.exit_code == 0;
}

bool AdbManager::disconnect(const std::string& endpoint, std::string& out_msg) {
    log("Disconnecting ADB endpoint: " + endpoint);
    ProcessResult res = run_process({"adb", "disconnect", endpoint}, 3000);
    out_msg = res.stdout_str;
    trigger_refresh();
    return (res.exit_code == 0);
}

void AdbManager::monitor_loop() {
    while (monitoring_.load()) {
        trigger_refresh();
        for (int i = 0; i < interval_seconds_ * 10 && monitoring_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace pobs
