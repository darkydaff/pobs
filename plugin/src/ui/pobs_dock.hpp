#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QStackedWidget>
#include <QLineEdit>
#include <QVector>
#include <QVBoxLayout>
#include <QFrame>
#include "qr_code_widget.hpp"
#include "../pobs_service.hpp"

namespace pobs {

struct KnownDevice {
    QString name;
    QString ip;
    QString last_port;
};

class PobsDock : public QDockWidget {
    Q_OBJECT

public:
    explicit PobsDock(QWidget *parent = nullptr);
    ~PobsDock() override;

private slots:
    void on_front_camera_clicked();
    void on_back_camera_clicked();
    void on_torch_clicked();
    void on_resolution_changed(int index);
    void on_fps_changed(int index);
    void on_zoom_slider_changed(int value);
    void on_stream_pause_clicked();

    void on_connect_known_clicked();
    void on_pair_new_clicked();
    void on_switch_to_pair_mode();
    void on_switch_to_known_mode();
    void on_known_device_selected(int index);
    void on_adb_header_clicked();

private:
    void setup_ui();
    void setup_adb_card(QWidget *parent, QVBoxLayout *parent_layout);
    void load_known_devices();
    void save_known_devices();
    void add_or_update_known_device(const QString &name, const QString &ip, const QString &port);
    void update_status(const CompanionStatus &status);
    void update_adb_ui(bool connected, const std::string &summary);
    void update_camera_buttons_style(const std::string &active_facing);
    void update_torch_button_style(bool active);
    void update_pause_button_style(bool streaming_active);

    // Header Status
    QLabel *status_dot_label_ = nullptr;
    QLabel *device_title_ = nullptr;
    QPushButton *adb_header_btn_ = nullptr;
    QLabel *battery_icon_label_ = nullptr;
    QLabel *battery_text_label_ = nullptr;

    // View stack: QR/ADB View vs Controls View
    QStackedWidget *view_stack_ = nullptr;
    QWidget *qr_view_ = nullptr;
    QWidget *controls_view_ = nullptr;
    QrCodeWidget *qr_widget_ = nullptr;
    QLabel *qr_hint_label_ = nullptr;
    QLabel *ip_pill_label_ = nullptr;

    // ADB Controls & Memory
    QFrame *adb_card_ = nullptr;
    QLabel *adb_status_badge_ = nullptr;
    QLabel *adb_status_msg_ = nullptr;
    QStackedWidget *adb_mode_stack_ = nullptr;

    // Mode 0: Known Device Reconnect
    QWidget *known_mode_widget_ = nullptr;
    QComboBox *known_device_combo_ = nullptr;
    QLineEdit *known_ip_edit_ = nullptr;
    QLineEdit *known_port_edit_ = nullptr;
    QPushButton *known_connect_btn_ = nullptr;
    QPushButton *pair_new_link_btn_ = nullptr;

    // Mode 1: Pair New Device
    QWidget *pair_mode_widget_ = nullptr;
    QLineEdit *new_name_edit_ = nullptr;
    QLineEdit *new_ip_edit_ = nullptr;
    QLineEdit *new_pair_port_edit_ = nullptr;
    QLineEdit *new_pin_edit_ = nullptr;
    QLineEdit *new_connect_port_edit_ = nullptr;
    QPushButton *new_pair_btn_ = nullptr;
    QPushButton *cancel_pair_btn_ = nullptr;

    // Mode 2: Connected Summary
    QWidget *connected_mode_widget_ = nullptr;
    QLabel *connected_summary_label_ = nullptr;
    QPushButton *reconnect_toggle_btn_ = nullptr;

    QVector<KnownDevice> known_devices_;

    // Format Controls
    QComboBox *res_combo_ = nullptr;
    QComboBox *fps_combo_ = nullptr;
    bool updating_combos_ = false;

    // Zoom Control
    QSlider *zoom_slider_ = nullptr;
    QLabel *zoom_val_label_ = nullptr;
    bool updating_zoom_ = false;

    // Camera Switcher & Actions
    QPushButton *front_cam_btn_ = nullptr;
    QPushButton *back_cam_btn_ = nullptr;
    QPushButton *torch_btn_ = nullptr;
    QPushButton *stream_pause_btn_ = nullptr;

    QLabel *stream_info_label_ = nullptr;

    std::string current_facing_ = "front";
    bool torch_active_ = false;
    bool is_streaming_active_ = true;
    bool is_adb_connected_ = false;
};

void register_pobs_dock();

} // namespace pobs
