#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include "companion/companion_config.hpp"
#include "adb/adb_manager.hpp"
#include "scrcpy/scrcpy_manager.hpp"
#include "common/process_util.hpp"
#include "net/pobs_ws_server.hpp"

void test_companion_parser() {
    std::cout << "[TEST] Running test_companion_parser..." << std::endl;

    std::string sample_output =
        "Row: 0 config={\"version\":1,\"battery\":{\"level\":79,\"isCharging\":false},"
        "\"selectedConfig\":{\"cameraId\":\"1\",\"facing\":\"front\",\"size\":\"1920x1080\",\"fps\":60},"
        "\"scrcpyCommandPreview\":\"scrcpy --video-source=camera\","
        "\"cameraCapabilities\":[{\"id\":\"0\",\"facing\":\"back\",\"supportedSizes\":[\"1920x1080\"],\"supportedFps\":[60,30]},"
        "{\"id\":\"1\",\"facing\":\"front\",\"supportedSizes\":[\"1920x1080\"],\"supportedFps\":[60,30]}]}, json={...}";

    pobs::CompanionConfig config;
    std::string error;
    bool ok = pobs::parse_companion_query_output(sample_output, config, error);
    assert(ok && "Failed to parse valid companion output");
    assert(config.version == 1);
    assert(config.battery.level == 79);
    assert(!config.battery.is_charging);
    assert(config.selected_config.camera_id == "1");
    assert(config.selected_config.facing == "front");
    assert(config.selected_config.size == "1920x1080");
    assert(config.selected_config.fps == 60);
    assert(config.camera_capabilities.size() == 2);
    assert(config.camera_capabilities[0].id == "0");
    assert(config.camera_capabilities[1].facing == "front");

    // Test malformed JSON
    pobs::CompanionConfig bad_config;
    std::string bad_output = "Row: 0 config={invalid_json...}";
    bool bad_ok = pobs::parse_companion_query_output(bad_output, bad_config, error);
    assert(!bad_ok);
    assert(!error.empty());

    // Test missing version
    pobs::CompanionConfig no_ver_config;
    std::string no_ver = "Row: 0 config={\"selectedConfig\":{\"size\":\"1920x1080\",\"fps\":60}}";
    bool no_ver_ok = pobs::parse_companion_query_output(no_ver, no_ver_config, error);
    assert(!no_ver_ok);

    std::cout << "[TEST] test_companion_parser PASSED!" << std::endl;
}

void test_adb_query_live() {
    std::cout << "[TEST] Running live ADB query test..." << std::endl;
    pobs::AdbManager adb;
    auto devices = adb.scan_raw_devices();
    std::cout << "[TEST] Discovered " << devices.size() << " attached ADB devices:" << std::endl;
    for (const auto& d : devices) {
        std::cout << "  - Endpoint: " << d.endpoint
                  << " | Transport: " << (d.transport == pobs::TransportType::WIRELESS ? "Wi-Fi" : "USB")
                  << " | State: " << (d.state == pobs::DeviceState::CONNECTED ? "Connected" : "Other")
                  << " | Model: " << d.model << std::endl;
    }

    if (!devices.empty() && devices[0].state == pobs::DeviceState::CONNECTED) {
        std::string hw_serial = adb.query_hardware_serial(devices[0].endpoint);
        std::cout << "[TEST] Hardware serial for " << devices[0].endpoint << ": " << hw_serial << std::endl;
        assert(!hw_serial.empty());

        pobs::CompanionConfig config;
        std::string err;
        bool ok = pobs::query_companion_config_via_adb(devices[0].endpoint, config, err);
        if (ok) {
            std::cout << "[TEST] Successfully queried live companion config from phone!" << std::endl;
            std::cout << "  - Battery: " << config.battery.level << "%" << std::endl;
            std::cout << "  - Camera: " << config.selected_config.facing << " @ "
                      << config.selected_config.size << " " << config.selected_config.fps << "fps" << std::endl;
        } else {
            std::cout << "[TEST] Companion query note: " << err << std::endl;
        }
    }

    std::cout << "[TEST] test_adb_query_live PASSED!" << std::endl;
}

void test_v4l2_sink_verifier() {
    std::cout << "[TEST] Running test_v4l2_sink_verifier..." << std::endl;
    std::string err;
    bool ok_bad = pobs::ScrcpyManager::verify_v4l2_sink("/dev/non_existent_video99", err);
    assert(!ok_bad);
    assert(!err.empty());
    std::cout << "  - Handled non-existent device correctly: " << err << std::endl;
    std::cout << "[TEST] test_v4l2_sink_verifier PASSED!" << std::endl;
}

void test_websocket_server() {
    std::cout << "[TEST] Running test_websocket_server..." << std::endl;
    pobs::PobsWebSocketServer server;
    bool started = server.start(8769);
    assert(started && "Failed to bind WebSocket server on port 8769");

    QString lan_ip = pobs::PobsWebSocketServer::get_local_lan_ip();
    std::cout << "  - Detected host LAN IP: " << lan_ip.toStdString() << std::endl;
    assert(!lan_ip.isEmpty());

    server.stop();
    std::cout << "[TEST] test_websocket_server PASSED!" << std::endl;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    std::cout << "=== Running POBS Plugin Test Suite ===" << std::endl;
    test_companion_parser();
    test_v4l2_sink_verifier();
    test_websocket_server();
    test_adb_query_live();
    std::cout << "=== All Tests Passed Successfully! ===" << std::endl;
    return 0;
}
