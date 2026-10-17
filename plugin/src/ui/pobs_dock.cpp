#include "pobs_dock.hpp"
#include "pobs_icons.hpp"
#include "../pobs_service.hpp"
#include "../net/pobs_ws_server.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QMetaObject>
#include <QSettings>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QRegularExpression>
#include <thread>
#include <obs-frontend-api.h>

namespace pobs {

class CompactStackedWidget : public QStackedWidget {
public:
    explicit CompactStackedWidget(QWidget *parent = nullptr) : QStackedWidget(parent) {
        connect(this, &QStackedWidget::currentChanged, this, [this](int) {
            updateGeometry();
            adjustSize();
        });
    }

    QSize sizeHint() const override {
        if (currentWidget()) {
            return currentWidget()->sizeHint();
        }
        return QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override {
        if (currentWidget()) {
            return currentWidget()->minimumSizeHint();
        }
        return QStackedWidget::minimumSizeHint();
    }
};

PobsDock::PobsDock(QWidget *parent)
    : QDockWidget("POBS Camera", parent) {
    setup_ui();

    PobsService::instance().set_status_callback([this](const CompanionStatus &status) {
        QMetaObject::invokeMethod(this, [this, status]() {
            update_status(status);
        }, Qt::QueuedConnection);
    });

    PobsService::instance().set_adb_status_callback([this](const AdbConnectionInfo &info) {
        QMetaObject::invokeMethod(this, [this, info]() {
            update_adb_ui(info);
        }, Qt::QueuedConnection);
    });

    load_known_devices();
    update_adb_ui(PobsService::instance().get_adb_info());
    update_status(PobsService::instance().get_status());
}

PobsDock::~PobsDock() {
    PobsService::instance().set_adb_status_callback(nullptr);
    PobsService::instance().set_status_callback(nullptr);
}

void PobsDock::setup_ui() {
    QWidget *main_widget = new QWidget(this);
    QVBoxLayout *main_layout = new QVBoxLayout(main_widget);
    main_layout->setContentsMargins(8, 8, 8, 8);
    main_layout->setSpacing(8);

    // --- Header Status Bar ---
    QFrame *header_card = new QFrame(main_widget);
    header_card->setObjectName("header_card");
    header_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    header_card->setFixedHeight(38);
    header_card->setStyleSheet(
        "#header_card {"
        "  background-color: #18181b;"
        "  border: 1px solid #27272a;"
        "  border-radius: 8px;"
        "  padding: 4px 8px;"
        "}");

    QHBoxLayout *header_layout = new QHBoxLayout(header_card);
    header_layout->setContentsMargins(6, 2, 6, 2);
    header_layout->setSpacing(6);

    status_dot_label_ = new QLabel(header_card);
    status_dot_label_->setPixmap(PobsIcons::status_dot(false, 8));
    status_dot_label_->setFixedSize(10, 10);

    device_title_ = new QLabel("Standby", header_card);
    device_title_->setStyleSheet("font-weight: 600; font-size: 13px; color: #f4f4f5;");

    adb_header_btn_ = new QPushButton("ADB Checking...", header_card);
    adb_header_btn_->setCursor(Qt::PointingHandCursor);
    adb_header_btn_->setStyleSheet(
        "background-color: #27272a;"
        "color: #a1a1aa;"
        "border: 1px solid #3f3f46;"
        "border-radius: 4px;"
        "padding: 2px 8px;"
        "font-size: 11px;"
        "font-weight: 600;");
    connect(adb_header_btn_, &QPushButton::clicked, this, &PobsDock::on_adb_header_clicked);

    battery_icon_label_ = new QLabel(header_card);
    battery_icon_label_->setFixedSize(20, 20);

    battery_text_label_ = new QLabel("", header_card);
    battery_text_label_->setStyleSheet("color: #a1a1aa; font-size: 11px; font-weight: 500;");

    header_layout->addWidget(status_dot_label_);
    header_layout->addWidget(device_title_);
    header_layout->addStretch();
    header_layout->addWidget(adb_header_btn_);
    header_layout->addWidget(battery_icon_label_);
    header_layout->addWidget(battery_text_label_);

    main_layout->addWidget(header_card);

    // --- View Stack (QR/ADB vs Connected) ---
    view_stack_ = new CompactStackedWidget(main_widget);

    // 1. QR & ADB Pairing Hub
    qr_view_ = new QWidget(view_stack_);
    QVBoxLayout *qr_layout = new QVBoxLayout(qr_view_);
    qr_layout->setContentsMargins(0, 4, 0, 4);
    qr_layout->setSpacing(8);

    // USB Auto-Detect Banner Card
    usb_banner_card_ = new QFrame(qr_view_);
    usb_banner_card_->setObjectName("usb_banner");
    usb_banner_card_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    usb_banner_card_->setStyleSheet(
        "#usb_banner {"
        "  background-color: rgba(34, 197, 94, 0.12);"
        "  border: 1px solid rgba(34, 197, 94, 0.35);"
        "  border-radius: 8px;"
        "  padding: 8px 12px;"
        "}");
    QHBoxLayout *usb_banner_layout = new QHBoxLayout(usb_banner_card_);
    usb_banner_layout->setContentsMargins(6, 6, 6, 6);
    usb_banner_layout->setSpacing(8);

    usb_banner_icon_ = new QLabel("⚡", usb_banner_card_);
    usb_banner_icon_->setStyleSheet("font-size: 16px;");

    usb_banner_text_ = new QLabel(usb_banner_card_);
    usb_banner_text_->setWordWrap(true);
    usb_banner_text_->setStyleSheet("font-size: 11px; color: #22c55e; font-weight: 500;");

    usb_banner_layout->addWidget(usb_banner_icon_);
    usb_banner_layout->addWidget(usb_banner_text_, 1);
    usb_banner_card_->hide(); // Hidden until USB detected

    qr_layout->addWidget(usb_banner_card_);

    setup_adb_card(qr_view_, qr_layout);

    // Collapsible Direct LAN Fallback (for non-ADB users)
    lan_toggle_btn_ = new QPushButton("▼ Advanced: Direct LAN Wi-Fi (No ADB)", qr_view_);
    lan_toggle_btn_->setStyleSheet("background: transparent; color: #71717a; font-size: 10px; border: none; text-align: left; padding: 4px;");
    lan_toggle_btn_->setCursor(Qt::PointingHandCursor);
    connect(lan_toggle_btn_, &QPushButton::clicked, this, &PobsDock::on_lan_toggle_clicked);
    qr_layout->addWidget(lan_toggle_btn_);

    lan_fallback_widget_ = new QWidget(qr_view_);
    QVBoxLayout *lan_layout = new QVBoxLayout(lan_fallback_widget_);
    lan_layout->setContentsMargins(0, 2, 0, 4);
    lan_layout->setSpacing(4);

    QString lan_ip = PobsWebSocketServer::get_local_lan_ip();
    QString qr_url = QString("pobs://%1:8765").arg(lan_ip);

    qr_widget_ = new QrCodeWidget(lan_fallback_widget_);
    qr_widget_->set_text(qr_url);
    qr_widget_->setFixedSize(130, 130);

    qr_hint_label_ = new QLabel("Direct Wi-Fi fallback. Open Companion Settings to enter LAN IP:", lan_fallback_widget_);
    qr_hint_label_->setAlignment(Qt::AlignCenter);
    qr_hint_label_->setStyleSheet("color: #71717a; font-size: 10px;");

    ip_pill_label_ = new QLabel(QString("ws://%1:8765").arg(lan_ip), lan_fallback_widget_);
    ip_pill_label_->setAlignment(Qt::AlignCenter);
    ip_pill_label_->setStyleSheet(
        "background-color: #27272a;"
        "color: #a1a1aa;"
        "border-radius: 4px;"
        "padding: 2px 6px;"
        "font-family: monospace;"
        "font-size: 10px;");

    lan_layout->addWidget(qr_widget_, 0, Qt::AlignCenter);
    lan_layout->addWidget(qr_hint_label_, 0, Qt::AlignCenter);
    lan_layout->addWidget(ip_pill_label_, 0, Qt::AlignCenter);

    lan_fallback_widget_->hide(); // Hidden by default
    qr_layout->addWidget(lan_fallback_widget_);
    qr_layout->addStretch();

    // 2. Controls View
    controls_view_ = new QWidget(view_stack_);
    QVBoxLayout *controls_layout = new QVBoxLayout(controls_view_);
    controls_layout->setContentsMargins(0, 0, 0, 0);
    controls_layout->setSpacing(8);

    // Format Controls Card (Resolution & FPS & Orientation)
    QFrame *format_card = new QFrame(controls_view_);
    format_card->setObjectName("format_card");
    format_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    format_card->setFixedHeight(106);
    format_card->setStyleSheet(
        "#format_card {"
        "  background-color: #18181b;"
        "  border: 1px solid #27272a;"
        "  border-radius: 8px;"
        "  padding: 4px 8px;"
        "}");

    QGridLayout *format_grid = new QGridLayout(format_card);
    format_grid->setContentsMargins(6, 4, 6, 6);
    format_grid->setHorizontalSpacing(8);
    format_grid->setVerticalSpacing(3);

    QLabel *res_title = new QLabel("RESOLUTION", format_card);
    res_title->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;");
    QLabel *fps_title = new QLabel("FRAME RATE", format_card);
    fps_title->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;");

    res_combo_ = new QComboBox(format_card);
    res_combo_->addItem(PobsIcons::display(), "1080p (1920x1080)", "1920x1080");
    res_combo_->addItem(PobsIcons::display(), "720p (1280x720)", "1280x720");
    res_combo_->addItem(PobsIcons::display(), "4K (3840x2160)", "3840x2160");

    fps_combo_ = new QComboBox(format_card);
    fps_combo_->addItem("60 FPS", 60);
    fps_combo_->addItem("30 FPS", 30);

    QString combo_style =
        "QComboBox {"
        "  background-color: #27272a;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 6px;"
        "  color: #f4f4f5;"
        "  padding: 4px 8px;"
        "  font-weight: 500;"
        "  font-size: 11px;"
        "}"
        "QComboBox:hover {"
        "  border: 1px solid #52525b;"
        "}"
        "QComboBox::drop-down {"
        "  subcontrol-origin: padding;"
        "  subcontrol-position: top right;"
        "  width: 16px;"
        "  border-left: none;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: #18181b;"
        "  border: 1px solid #27272a;"
        "  selection-background-color: #2563eb;"
        "  color: #f4f4f5;"
        "  border-radius: 6px;"
        "}";

    res_combo_->setStyleSheet(combo_style);
    fps_combo_->setStyleSheet(combo_style);

    connect(res_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PobsDock::on_resolution_changed);
    connect(fps_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PobsDock::on_fps_changed);

    QLabel *orient_title = new QLabel("ORIENTATION MODE", format_card);
    orient_title->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; letter-spacing: 0.5px; margin-top: 2px;");

    orientation_combo_ = new QComboBox(format_card);
    orientation_combo_->addItem("Auto (Gyroscope)", "auto");
    orientation_combo_->addItem("Vertical (Portrait)", "vertical");
    orientation_combo_->addItem("Horizontal (Landscape)", "horizontal");
    orientation_combo_->setStyleSheet(combo_style);

    connect(orientation_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PobsDock::on_orientation_changed);

    format_grid->addWidget(res_title, 0, 0);
    format_grid->addWidget(fps_title, 0, 1);
    format_grid->addWidget(res_combo_, 1, 0);
    format_grid->addWidget(fps_combo_, 1, 1);
    format_grid->addWidget(orient_title, 2, 0, 1, 2);
    format_grid->addWidget(orientation_combo_, 3, 0, 1, 2);

    controls_layout->addWidget(format_card);

    // Zoom Controls Card
    QFrame *zoom_card = new QFrame(controls_view_);
    zoom_card->setObjectName("zoom_card");
    zoom_card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    zoom_card->setFixedHeight(58);
    zoom_card->setStyleSheet(
        "#zoom_card {"
        "  background-color: #18181b;"
        "  border: 1px solid #27272a;"
        "  border-radius: 8px;"
        "  padding: 4px 8px;"
        "}");

    QVBoxLayout *zoom_layout = new QVBoxLayout(zoom_card);
    zoom_layout->setContentsMargins(6, 4, 6, 6);
    zoom_layout->setSpacing(4);

    QHBoxLayout *zoom_header = new QHBoxLayout();
    QLabel *zoom_title = new QLabel("DIGITAL ZOOM", zoom_card);
    zoom_title->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;");

    zoom_val_label_ = new QLabel("1.0x", zoom_card);
    zoom_val_label_->setStyleSheet("color: #60a5fa; font-size: 11px; font-weight: 700; font-family: monospace;");

    zoom_header->addWidget(zoom_title);
    zoom_header->addStretch();
    zoom_header->addWidget(zoom_val_label_);
    zoom_layout->addLayout(zoom_header);

    zoom_slider_ = new QSlider(Qt::Horizontal, zoom_card);
    zoom_slider_->setRange(10, 50); // 1.0x to 5.0x
    zoom_slider_->setValue(10);
    zoom_slider_->setStyleSheet(
        "QSlider::groove:horizontal {"
        "  border: none;"
        "  height: 4px;"
        "  background: #27272a;"
        "  border-radius: 2px;"
        "}"
        "QSlider::sub-page:horizontal {"
        "  background: #2563eb;"
        "  border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  background: #f4f4f5;"
        "  border: none;"
        "  width: 14px;"
        "  height: 14px;"
        "  margin: -5px 0;"
        "  border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover {"
        "  background: #ffffff;"
        "  transform: scale(1.1);"
        "}");
    connect(zoom_slider_, &QSlider::valueChanged, this, &PobsDock::on_zoom_slider_changed);
    zoom_layout->addWidget(zoom_slider_);

    controls_layout->addWidget(zoom_card);

    // Camera Switcher Row
    QHBoxLayout *cam_layout = new QHBoxLayout();
    cam_layout->setSpacing(8);

    front_cam_btn_ = new QPushButton(controls_view_);
    front_cam_btn_->setText("Front Camera");
    front_cam_btn_->setIcon(PobsIcons::camera_front());
    front_cam_btn_->setIconSize(QSize(18, 18));
    front_cam_btn_->setCheckable(true);
    front_cam_btn_->setFixedHeight(34);
    front_cam_btn_->setCursor(Qt::PointingHandCursor);

    back_cam_btn_ = new QPushButton(controls_view_);
    back_cam_btn_->setText("Back Camera");
    back_cam_btn_->setIcon(PobsIcons::camera_back());
    back_cam_btn_->setIconSize(QSize(18, 18));
    back_cam_btn_->setCheckable(true);
    back_cam_btn_->setFixedHeight(34);
    back_cam_btn_->setCursor(Qt::PointingHandCursor);

    connect(front_cam_btn_, &QPushButton::clicked, this, &PobsDock::on_front_camera_clicked);
    connect(back_cam_btn_, &QPushButton::clicked, this, &PobsDock::on_back_camera_clicked);

    cam_layout->addWidget(front_cam_btn_);
    cam_layout->addWidget(back_cam_btn_);
    controls_layout->addLayout(cam_layout);

    // Utility Actions Row: Torch & Stream Pause/Resume
    QHBoxLayout *util_layout = new QHBoxLayout();
    util_layout->setSpacing(8);

    torch_btn_ = new QPushButton(controls_view_);
    torch_btn_->setText("Key Light");
    torch_btn_->setIcon(PobsIcons::torch(false));
    torch_btn_->setIconSize(QSize(18, 18));
    torch_btn_->setCheckable(true);
    torch_btn_->setFixedHeight(34);
    torch_btn_->setCursor(Qt::PointingHandCursor);
    connect(torch_btn_, &QPushButton::clicked, this, &PobsDock::on_torch_clicked);
    update_torch_button_style(false);

    stream_pause_btn_ = new QPushButton(controls_view_);
    stream_pause_btn_->setFixedHeight(34);
    stream_pause_btn_->setCursor(Qt::PointingHandCursor);
    connect(stream_pause_btn_, &QPushButton::clicked, this, &PobsDock::on_stream_pause_clicked);
    update_pause_button_style(true);

    util_layout->addWidget(torch_btn_);
    util_layout->addWidget(stream_pause_btn_);
    controls_layout->addLayout(util_layout);

    // Stream telemetry info pill
    stream_info_label_ = new QLabel("Ultra-Low Latency Hardware Stream", controls_view_);
    stream_info_label_->setAlignment(Qt::AlignCenter);
    stream_info_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    stream_info_label_->setFixedHeight(28);
    stream_info_label_->setStyleSheet(
        "color: #22c55e;"
        "font-size: 11px;"
        "font-weight: 600;"
        "padding: 4px 8px;"
        "background-color: rgba(34, 197, 94, 0.1);"
        "border: 1px solid rgba(34, 197, 94, 0.2);"
        "border-radius: 6px;");
    controls_layout->addWidget(stream_info_label_);

    controls_layout->addStretch();

    view_stack_->addWidget(qr_view_);
    view_stack_->addWidget(controls_view_);
    main_layout->addWidget(view_stack_);

    main_layout->addStretch();
    setWidget(main_widget);
}

void PobsDock::setup_adb_card(QWidget *parent, QVBoxLayout *parent_layout) {
    adb_card_ = new QFrame(parent);
    adb_card_->setObjectName("adb_card");
    adb_card_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    adb_card_->setStyleSheet(
        "#adb_card {"
        "  background-color: #18181b;"
        "  border: 1px solid #27272a;"
        "  border-radius: 8px;"
        "  padding: 10px;"
        "}"
        "QLineEdit {"
        "  background-color: #27272a;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 6px;"
        "  color: #f4f4f5;"
        "  padding: 6px 10px;"
        "  font-size: 12px;"
        "}"
        "QLineEdit:focus {"
        "  border: 1px solid #3b82f6;"
        "}"
        "QComboBox {"
        "  background-color: #27272a;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 6px;"
        "  color: #f4f4f5;"
        "  padding: 5px 8px;"
        "  font-size: 12px;"
        "}");

    QVBoxLayout *card_layout = new QVBoxLayout(adb_card_);
    card_layout->setContentsMargins(6, 6, 6, 6);
    card_layout->setSpacing(8);

    // Header of card: title + status badge
    QHBoxLayout *header = new QHBoxLayout();
    QLabel *card_title = new QLabel("WIRELESS ADB AUTO-TUNNEL", adb_card_);
    card_title->setStyleSheet("color: #71717a; font-size: 10px; font-weight: 700; letter-spacing: 0.5px;");

    adb_status_badge_ = new QLabel("CHECKING...", adb_card_);
    adb_status_badge_->setStyleSheet("font-size: 10px; font-weight: 700; color: #a1a1aa; padding: 2px 6px; border-radius: 4px; background: #27272a;");

    header->addWidget(card_title);
    header->addStretch();
    header->addWidget(adb_status_badge_);
    card_layout->addLayout(header);

    // Live feedback label
    adb_status_msg_ = new QLabel("Checking connection...", adb_card_);
    adb_status_msg_->setWordWrap(true);
    adb_status_msg_->setStyleSheet("font-size: 11px; color: #a1a1aa;");
    card_layout->addWidget(adb_status_msg_);

    // Mode Stack
    adb_mode_stack_ = new CompactStackedWidget(adb_card_);

    // --- Mode 0: Known Device Reconnect ---
    known_mode_widget_ = new QWidget(adb_mode_stack_);
    QVBoxLayout *known_layout = new QVBoxLayout(known_mode_widget_);
    known_layout->setContentsMargins(0, 4, 0, 0);
    known_layout->setSpacing(6);

    QLabel *known_desc = new QLabel("Device is remembered. Enter new wireless debugging port:", known_mode_widget_);
    known_desc->setStyleSheet("color: #d4d4d8; font-size: 11px;");
    known_layout->addWidget(known_desc);

    known_device_combo_ = new QComboBox(known_mode_widget_);
    connect(known_device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PobsDock::on_known_device_selected);
    known_layout->addWidget(known_device_combo_);

    QHBoxLayout *known_fields = new QHBoxLayout();
    known_ip_edit_ = new QLineEdit(known_mode_widget_);
    known_ip_edit_->setPlaceholderText("IP (e.g. 192.168.1.89)");

    known_port_edit_ = new QLineEdit(known_mode_widget_);
    known_port_edit_->setPlaceholderText("Port (e.g. 44517)");

    known_fields->addWidget(known_ip_edit_, 3);
    known_fields->addWidget(known_port_edit_, 2);
    known_layout->addLayout(known_fields);

    QHBoxLayout *known_btn_row = new QHBoxLayout();
    known_connect_btn_ = new QPushButton("⚡ Connect ADB", known_mode_widget_);
    known_connect_btn_->setStyleSheet("background-color: #2563eb; color: #ffffff; font-weight: 600; padding: 6px 14px; border-radius: 6px; border: none;");
    known_connect_btn_->setCursor(Qt::PointingHandCursor);
    connect(known_connect_btn_, &QPushButton::clicked, this, &PobsDock::on_connect_known_clicked);

    pair_new_link_btn_ = new QPushButton("+ Pair New Device", known_mode_widget_);
    pair_new_link_btn_->setStyleSheet("background: transparent; color: #60a5fa; font-size: 11px; text-decoration: underline; border: none;");
    pair_new_link_btn_->setCursor(Qt::PointingHandCursor);
    connect(pair_new_link_btn_, &QPushButton::clicked, this, &PobsDock::on_switch_to_pair_mode);

    known_btn_row->addWidget(known_connect_btn_);
    known_btn_row->addStretch();
    known_btn_row->addWidget(pair_new_link_btn_);
    known_layout->addLayout(known_btn_row);

    // --- Mode 1: Step 1: Pair New Device (Code & Pair Port) ---
    pair_mode_widget_ = new QWidget(adb_mode_stack_);
    QVBoxLayout *pair_layout = new QVBoxLayout(pair_mode_widget_);
    pair_layout->setContentsMargins(0, 4, 0, 0);
    pair_layout->setSpacing(6);

    QLabel *step1_title = new QLabel("Step 1 of 2: Wi-Fi Pairing Code", pair_mode_widget_);
    step1_title->setStyleSheet("color: #60a5fa; font-weight: 700; font-size: 11px;");
    pair_layout->addWidget(step1_title);

    QLabel *pair_desc = new QLabel("On phone: Developer options > Wireless debugging > 'Pair device with pairing code':", pair_mode_widget_);
    pair_desc->setStyleSheet("color: #a1a1aa; font-size: 10px;");
    pair_desc->setWordWrap(true);
    pair_layout->addWidget(pair_desc);

    new_name_edit_ = new QLineEdit(pair_mode_widget_);
    new_name_edit_->setPlaceholderText("Device Name (e.g. Pixel 7a)");
    pair_layout->addWidget(new_name_edit_);

    QHBoxLayout *pair_row1 = new QHBoxLayout();
    new_ip_edit_ = new QLineEdit(pair_mode_widget_);
    new_ip_edit_->setPlaceholderText("Phone IP (e.g. 192.168.1.89)");

    new_pair_port_edit_ = new QLineEdit(pair_mode_widget_);
    new_pair_port_edit_->setPlaceholderText("Pairing Port (e.g. 37123)");

    pair_row1->addWidget(new_ip_edit_, 3);
    pair_row1->addWidget(new_pair_port_edit_, 2);
    pair_layout->addLayout(pair_row1);

    new_pin_edit_ = new QLineEdit(pair_mode_widget_);
    new_pin_edit_->setPlaceholderText("6-digit Code from pop-up (e.g. 123456)");
    pair_layout->addWidget(new_pin_edit_);

    QHBoxLayout *pair_btn_row = new QHBoxLayout();
    new_pair_btn_ = new QPushButton("Pair Device", pair_mode_widget_);
    new_pair_btn_->setStyleSheet("background-color: #2563eb; color: #ffffff; font-weight: 600; padding: 6px 14px; border-radius: 6px; border: none;");
    new_pair_btn_->setCursor(Qt::PointingHandCursor);
    connect(new_pair_btn_, &QPushButton::clicked, this, &PobsDock::on_pair_step1_clicked);

    cancel_pair_btn_ = new QPushButton("Cancel", pair_mode_widget_);
    cancel_pair_btn_->setStyleSheet("background: #27272a; color: #a1a1aa; padding: 6px 12px; border-radius: 6px; border: none;");
    cancel_pair_btn_->setCursor(Qt::PointingHandCursor);
    connect(cancel_pair_btn_, &QPushButton::clicked, this, &PobsDock::on_switch_to_known_mode);

    pair_btn_row->addWidget(new_pair_btn_);
    pair_btn_row->addWidget(cancel_pair_btn_);
    pair_layout->addLayout(pair_btn_row);

    // --- Mode 2: Step 2: Connect Port (Shown if auto-discovery couldn't resolve port) ---
    step2_mode_widget_ = new QWidget(adb_mode_stack_);
    QVBoxLayout *step2_layout = new QVBoxLayout(step2_mode_widget_);
    step2_layout->setContentsMargins(0, 4, 0, 0);
    step2_layout->setSpacing(6);

    QLabel *step2_title = new QLabel("Step 2 of 2: Enter Connect Port", step2_mode_widget_);
    step2_title->setStyleSheet("color: #22c55e; font-weight: 700; font-size: 11px;");
    step2_layout->addWidget(step2_title);

    step2_ip_label_ = new QLabel(step2_mode_widget_);
    step2_ip_label_->setStyleSheet("color: #d4d4d8; font-size: 11px;");
    step2_layout->addWidget(step2_ip_label_);

    QLabel *step2_desc = new QLabel("Pairing succeeded! Look at the 'IP address & Port' on your phone's Wireless debugging screen:", step2_mode_widget_);
    step2_desc->setStyleSheet("color: #a1a1aa; font-size: 10px;");
    step2_desc->setWordWrap(true);
    step2_layout->addWidget(step2_desc);

    step2_connect_port_edit_ = new QLineEdit(step2_mode_widget_);
    step2_connect_port_edit_->setPlaceholderText("Connect Port (e.g. 46325)");
    step2_layout->addWidget(step2_connect_port_edit_);

    QHBoxLayout *step2_btn_row = new QHBoxLayout();
    step2_connect_btn_ = new QPushButton("⚡ Connect Device", step2_mode_widget_);
    step2_connect_btn_->setStyleSheet("background-color: #2563eb; color: #ffffff; font-weight: 600; padding: 6px 14px; border-radius: 6px; border: none;");
    step2_connect_btn_->setCursor(Qt::PointingHandCursor);
    connect(step2_connect_btn_, &QPushButton::clicked, this, &PobsDock::on_pair_step2_connect_clicked);

    step2_cancel_btn_ = new QPushButton("Back", step2_mode_widget_);
    step2_cancel_btn_->setStyleSheet("background: #27272a; color: #a1a1aa; padding: 6px 12px; border-radius: 6px; border: none;");
    step2_cancel_btn_->setCursor(Qt::PointingHandCursor);
    connect(step2_cancel_btn_, &QPushButton::clicked, this, &PobsDock::on_switch_to_pair_mode);

    step2_btn_row->addWidget(step2_connect_btn_);
    step2_btn_row->addWidget(step2_cancel_btn_);
    step2_layout->addLayout(step2_btn_row);

    // --- Mode 3: Connected Device Summary ---
    connected_mode_widget_ = new QWidget(adb_mode_stack_);
    QVBoxLayout *conn_layout = new QVBoxLayout(connected_mode_widget_);
    conn_layout->setContentsMargins(0, 4, 0, 0);
    conn_layout->setSpacing(6);

    connected_summary_label_ = new QLabel(connected_mode_widget_);
    connected_summary_label_->setStyleSheet("color: #22c55e; font-size: 11px; font-weight: 500;");
    conn_layout->addWidget(connected_summary_label_);

    reconnect_toggle_btn_ = new QPushButton("Change Device / Port", connected_mode_widget_);
    reconnect_toggle_btn_->setStyleSheet("background: #27272a; color: #d4d4d8; font-size: 11px; padding: 5px 10px; border-radius: 5px; border: 1px solid #3f3f46;");
    reconnect_toggle_btn_->setCursor(Qt::PointingHandCursor);
    connect(reconnect_toggle_btn_, &QPushButton::clicked, this, &PobsDock::on_switch_to_known_mode);
    conn_layout->addWidget(reconnect_toggle_btn_);

    adb_mode_stack_->addWidget(known_mode_widget_);      // 0
    adb_mode_stack_->addWidget(pair_mode_widget_);       // 1
    adb_mode_stack_->addWidget(step2_mode_widget_);      // 2
    adb_mode_stack_->addWidget(connected_mode_widget_);  // 3
    card_layout->addWidget(adb_mode_stack_);

    parent_layout->addWidget(adb_card_);
}

void PobsDock::load_known_devices() {
    known_devices_.clear();
    QSettings settings("pobs", "pobs-plugin");
    QString json_str = settings.value("adb/known_devices").toString();
    if (!json_str.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(json_str.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const auto &val : arr) {
                QJsonObject obj = val.toObject();
                KnownDevice dev;
                dev.name = obj.value("name").toString();
                dev.ip = obj.value("ip").toString();
                dev.last_port = obj.value("last_port").toString();
                if (!dev.ip.isEmpty()) {
                    known_devices_.append(dev);
                }
            }
        }
    }

    if (known_devices_.isEmpty()) {
        std::string summary = PobsService::instance().get_adb_summary();
        if (!summary.empty() && summary != "No device" && summary != "Disconnected") {
            KnownDevice dev;
            dev.name = "Pixel 7a";
            static const QRegularExpression ip_port_rx(R"((\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}):(\d+))");
            auto match = ip_port_rx.match(QString::fromStdString(summary));
            if (match.hasMatch()) {
                dev.ip = match.captured(1);
                dev.last_port = match.captured(2);
            } else {
                dev.ip = "192.168.1.89";
                dev.last_port = "44517";
            }
            known_devices_.append(dev);
            save_known_devices();
        } else {
            // Seed default Pixel 7a for quick setup
            KnownDevice dev;
            dev.name = "Pixel 7a";
            dev.ip = "192.168.1.89";
            dev.last_port = "44517";
            known_devices_.append(dev);
            save_known_devices();
        }
    }

    known_device_combo_->blockSignals(true);
    known_device_combo_->clear();
    for (const auto &dev : known_devices_) {
        known_device_combo_->addItem(QString("%1 (%2)").arg(dev.name, dev.ip));
    }
    known_device_combo_->blockSignals(false);

    if (!known_devices_.isEmpty()) {
        on_known_device_selected(0);
        adb_mode_stack_->setCurrentWidget(known_mode_widget_);
    } else {
        adb_mode_stack_->setCurrentWidget(pair_mode_widget_);
    }
}

void PobsDock::save_known_devices() {
    QJsonArray arr;
    for (const auto &dev : known_devices_) {
        QJsonObject obj;
        obj["name"] = dev.name;
        obj["ip"] = dev.ip;
        obj["last_port"] = dev.last_port;
        arr.append(obj);
    }
    QJsonDocument doc(arr);
    QSettings settings("pobs", "pobs-plugin");
    settings.setValue("adb/known_devices", QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

void PobsDock::add_or_update_known_device(const QString &name, const QString &ip, const QString &port) {
    bool found = false;
    for (auto &dev : known_devices_) {
        if (dev.ip == ip) {
            dev.name = name.isEmpty() ? dev.name : name;
            dev.last_port = port;
            found = true;
            break;
        }
    }
    if (!found) {
        KnownDevice dev;
        dev.name = name.isEmpty() ? "Android Phone" : name;
        dev.ip = ip;
        dev.last_port = port;
        known_devices_.append(dev);
    }
    save_known_devices();

    known_device_combo_->blockSignals(true);
    known_device_combo_->clear();
    for (const auto &dev : known_devices_) {
        known_device_combo_->addItem(QString("%1 (%2)").arg(dev.name, dev.ip));
    }
    known_device_combo_->setCurrentIndex(known_devices_.size() - 1);
    known_device_combo_->blockSignals(false);
}

void PobsDock::on_known_device_selected(int index) {
    if (index >= 0 && index < known_devices_.size()) {
        const auto &dev = known_devices_[index];
        known_ip_edit_->setText(dev.ip);
        known_port_edit_->setText(dev.last_port);
    }
}

void PobsDock::on_connect_known_clicked() {
    QString ip = known_ip_edit_->text().trimmed();
    QString port = known_port_edit_->text().trimmed();
    if (ip.isEmpty() || port.isEmpty()) {
        adb_status_msg_->setText("Error: IP and Port cannot be empty.");
        adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
        return;
    }

    known_connect_btn_->setEnabled(false);
    adb_status_msg_->setText(QString("Connecting to %1:%2...").arg(ip, port));
    adb_status_msg_->setStyleSheet("font-size: 11px; color: #3b82f6;");

    QString dev_name = "Device";
    if (known_device_combo_->currentIndex() >= 0 && known_device_combo_->currentIndex() < known_devices_.size()) {
        dev_name = known_devices_[known_device_combo_->currentIndex()].name;
    }

    std::string endpoint = (ip + ":" + port).toStdString();
    std::thread([this, endpoint, ip, port, dev_name]() {
        std::string out_msg;
        bool ok = PobsService::instance().connect_adb(endpoint, out_msg);
        QMetaObject::invokeMethod(this, [this, ok, out_msg, ip, port, dev_name]() {
            known_connect_btn_->setEnabled(true);
            if (ok) {
                add_or_update_known_device(dev_name, ip, port);
                adb_status_msg_->setText("Connected! Reversing ports...");
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");
                adb_mode_stack_->setCurrentWidget(connected_mode_widget_);
            } else {
                adb_status_msg_->setText("Connection failed: " + QString::fromStdString(out_msg).trimmed());
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void PobsDock::on_pair_step1_clicked() {
    QString name = new_name_edit_->text().trimmed();
    QString ip = new_ip_edit_->text().trimmed();
    QString pair_port = new_pair_port_edit_->text().trimmed();
    QString pin = new_pin_edit_->text().trimmed();

    if (ip.isEmpty() || pair_port.isEmpty() || pin.isEmpty()) {
        adb_status_msg_->setText("Error: IP, Pairing Port, and PIN are required.");
        adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
        return;
    }

    if (name.isEmpty()) name = "Android Phone";
    pending_pair_name_ = name;
    pending_pair_ip_ = ip;

    new_pair_btn_->setEnabled(false);
    adb_status_msg_->setText(QString("Pairing with %1:%2...").arg(ip, pair_port));
    adb_status_msg_->setStyleSheet("font-size: 11px; color: #3b82f6;");

    std::thread([this, name, ip, pair_port, pin]() {
        std::string pair_endpoint = (ip + ":" + pair_port).toStdString();
        std::string out_msg;

        bool pair_ok = PobsService::instance().pair_adb(pair_endpoint, pin.toStdString(), out_msg);
        if (!pair_ok) {
            QMetaObject::invokeMethod(this, [this, out_msg]() {
                new_pair_btn_->setEnabled(true);
                adb_status_msg_->setText("Pairing failed: " + QString::fromStdString(out_msg).trimmed());
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
            }, Qt::QueuedConnection);
            return;
        }

        // Pair succeeded! Attempt auto-discovery for 2 seconds
        QMetaObject::invokeMethod(this, [this]() {
            adb_status_msg_->setText("Pairing successful! Probing for connection port...");
            adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");
        }, Qt::QueuedConnection);

        std::string discovered_endpoint;
        for (int i = 0; i < 4; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            discovered_endpoint = PobsService::instance().adb().try_discover_connect_port(ip.toStdString());
            if (!discovered_endpoint.empty()) break;
        }

        if (!discovered_endpoint.empty()) {
            // Auto-discovered! Connect immediately
            std::string conn_out;
            bool conn_ok = PobsService::instance().connect_adb(discovered_endpoint, conn_out);
            QMetaObject::invokeMethod(this, [this, conn_ok, name, ip, discovered_endpoint]() {
                new_pair_btn_->setEnabled(true);
                if (conn_ok) {
                    QString port = QString::fromStdString(discovered_endpoint).section(':', 1);
                    add_or_update_known_device(name, ip, port);
                    adb_status_msg_->setText("Auto-discovered & connected! Tunnels active.");
                    adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");
                    adb_mode_stack_->setCurrentWidget(connected_mode_widget_);
                } else {
                    step2_ip_label_->setText(QString("Phone: %1 (%2)").arg(name, ip));
                    step2_connect_port_edit_->clear();
                    adb_mode_stack_->setCurrentWidget(step2_mode_widget_);
                    step2_connect_port_edit_->setFocus();
                }
            }, Qt::QueuedConnection);
        } else {
            // Transition to Step 2 for manual connect port
            QMetaObject::invokeMethod(this, [this, name, ip]() {
                new_pair_btn_->setEnabled(true);
                step2_ip_label_->setText(QString("Phone: %1 (%2)").arg(name, ip));
                step2_connect_port_edit_->clear();
                adb_status_msg_->setText("Paired! Enter the Connect Port shown on your phone:");
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #3b82f6;");
                adb_mode_stack_->setCurrentWidget(step2_mode_widget_);
                step2_connect_port_edit_->setFocus();
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void PobsDock::on_pair_step2_connect_clicked() {
    QString port = step2_connect_port_edit_->text().trimmed();
    if (port.isEmpty()) {
        adb_status_msg_->setText("Error: Connect port is required.");
        adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
        return;
    }

    step2_connect_btn_->setEnabled(false);
    adb_status_msg_->setText(QString("Connecting to %1:%2...").arg(pending_pair_ip_, port));
    adb_status_msg_->setStyleSheet("font-size: 11px; color: #3b82f6;");

    QString name = pending_pair_name_;
    QString ip = pending_pair_ip_;
    std::string endpoint = (ip + ":" + port).toStdString();

    std::thread([this, name, ip, port, endpoint]() {
        std::string conn_out;
        bool conn_ok = PobsService::instance().connect_adb(endpoint, conn_out);
        QMetaObject::invokeMethod(this, [this, conn_ok, conn_out, name, ip, port]() {
            step2_connect_btn_->setEnabled(true);
            if (conn_ok) {
                add_or_update_known_device(name, ip, port);
                adb_status_msg_->setText("Connected successfully! Tunnels active.");
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");
                adb_mode_stack_->setCurrentWidget(connected_mode_widget_);
            } else {
                adb_status_msg_->setText("Connection failed: " + QString::fromStdString(conn_out).trimmed());
                adb_status_msg_->setStyleSheet("font-size: 11px; color: #ef4444;");
            }
        }, Qt::QueuedConnection);
    }).detach();
}

void PobsDock::on_lan_toggle_clicked() {
    bool now_visible = !lan_fallback_widget_->isVisible();
    lan_fallback_widget_->setVisible(now_visible);
    if (now_visible) {
        lan_toggle_btn_->setText("▲ Hide Direct LAN Wi-Fi");
    } else {
        lan_toggle_btn_->setText("▼ Advanced: Direct LAN Wi-Fi (No ADB)");
    }
}

void PobsDock::on_switch_to_pair_mode() {
    adb_mode_stack_->setCurrentWidget(pair_mode_widget_);
    new_ip_edit_->setText(known_ip_edit_->text());
    new_pair_port_edit_->clear();
    new_pin_edit_->clear();
    new_pair_port_edit_->setFocus();
}

void PobsDock::on_switch_to_known_mode() {
    if (!known_devices_.isEmpty()) {
        adb_mode_stack_->setCurrentWidget(known_mode_widget_);
        known_port_edit_->setFocus();
    } else {
        adb_mode_stack_->setCurrentWidget(pair_mode_widget_);
    }
}

void PobsDock::on_adb_header_clicked() {
    view_stack_->setCurrentWidget(qr_view_);
    if (!known_devices_.isEmpty()) {
        adb_mode_stack_->setCurrentWidget(known_mode_widget_);
        known_port_edit_->setFocus();
        known_port_edit_->selectAll();
    } else {
        adb_mode_stack_->setCurrentWidget(pair_mode_widget_);
    }
}

void PobsDock::update_adb_ui(const AdbConnectionInfo &info) {
    is_adb_connected_ = info.is_connected && info.tunnels_active;

    if (info.transport == TransportType::USB) {
        if (info.is_connected && info.tunnels_active) {
            usb_banner_card_->setStyleSheet(
                "#usb_banner {"
                "  background-color: rgba(34, 197, 94, 0.12);"
                "  border: 1px solid rgba(34, 197, 94, 0.35);"
                "  border-radius: 8px;"
                "  padding: 8px 12px;"
                "}");
            usb_banner_icon_->setText("⚡");
            usb_banner_text_->setStyleSheet("font-size: 11px; color: #22c55e; font-weight: 500;");
            QString name = info.model.empty() ? "Pixel / Android Device" : QString::fromStdString(info.model);
            usb_banner_text_->setText(QString("<b>USB Plug-and-Play:</b> %1 • Reverse Tunnels Active").arg(name));
            usb_banner_card_->show();

            adb_header_btn_->setText("⚡ USB: Ready");
            adb_header_btn_->setStyleSheet(
                "background-color: rgba(34, 197, 94, 0.15);"
                "color: #22c55e;"
                "border: 1px solid rgba(34, 197, 94, 0.3);"
                "border-radius: 4px;"
                "padding: 2px 8px;"
                "font-size: 11px;"
                "font-weight: 600;");

            adb_status_badge_->setText("● TUNNELS ACTIVE");
            adb_status_badge_->setStyleSheet("font-size: 10px; font-weight: 700; color: #22c55e; padding: 2px 6px; border-radius: 4px; background: rgba(34, 197, 94, 0.15);");
            adb_status_msg_->setText(QString("USB Connected to %1. Ports 8765/8766 forwarded. Open companion app on phone.").arg(name));
            adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");

            connected_summary_label_->setText(QString("USB Connected: %1 (%2)\nReverse tunnels active on 127.0.0.1:8765/8766")
                .arg(name, QString::fromStdString(info.endpoint)));
            adb_mode_stack_->setCurrentWidget(connected_mode_widget_);
            return;
        } else if (info.state == DeviceState::UNAUTHORIZED) {
            usb_banner_card_->setStyleSheet(
                "#usb_banner {"
                "  background-color: rgba(245, 158, 11, 0.12);"
                "  border: 1px solid rgba(245, 158, 11, 0.35);"
                "  border-radius: 8px;"
                "  padding: 8px 12px;"
                "}");
            usb_banner_icon_->setText("⚠️");
            usb_banner_text_->setStyleSheet("font-size: 11px; color: #f59e0b; font-weight: 500;");
            usb_banner_text_->setText("<b>USB Detected:</b> Please unlock your phone and tap 'Allow USB debugging'.");
            usb_banner_card_->show();

            adb_header_btn_->setText("⚠️ USB: Unauthorized");
            adb_header_btn_->setStyleSheet(
                "background-color: rgba(245, 158, 11, 0.15);"
                "color: #f59e0b;"
                "border: 1px solid rgba(245, 158, 11, 0.3);"
                "border-radius: 4px;"
                "padding: 2px 8px;"
                "font-size: 11px;"
                "font-weight: 600;");

            adb_status_badge_->setText("● UNAUTHORIZED");
            adb_status_badge_->setStyleSheet("font-size: 10px; font-weight: 700; color: #f59e0b; padding: 2px 6px; border-radius: 4px; background: rgba(245, 158, 11, 0.15);");
            adb_status_msg_->setText("Action required: Look at your phone's screen and tap 'Always allow from this computer'.");
            adb_status_msg_->setStyleSheet("font-size: 11px; color: #f59e0b;");
            return;
        }
    }

    // Not USB
    usb_banner_card_->hide();

    if (info.is_connected && info.tunnels_active) {
        adb_header_btn_->setText("⚡ Wireless: Ready");
        adb_header_btn_->setStyleSheet(
            "background-color: rgba(34, 197, 94, 0.15);"
            "color: #22c55e;"
            "border: 1px solid rgba(34, 197, 94, 0.3);"
            "border-radius: 4px;"
            "padding: 2px 8px;"
            "font-size: 11px;"
            "font-weight: 600;");

        adb_status_badge_->setText("● TUNNELS ACTIVE");
        adb_status_badge_->setStyleSheet("font-size: 10px; font-weight: 700; color: #22c55e; padding: 2px 6px; border-radius: 4px; background: rgba(34, 197, 94, 0.15);");

        adb_status_msg_->setText(QString("Wireless ADB active (%1). Ports 8765/8766 forwarded.").arg(QString::fromStdString(info.endpoint)));
        adb_status_msg_->setStyleSheet("font-size: 11px; color: #22c55e;");

        connected_summary_label_->setText(QString("Connected to %1 (%2)\nReverse tunnels active on 127.0.0.1:8765/8766")
            .arg(QString::fromStdString(info.model), QString::fromStdString(info.endpoint)));

        if (adb_mode_stack_->currentWidget() != pair_mode_widget_ &&
            adb_mode_stack_->currentWidget() != step2_mode_widget_) {
            adb_mode_stack_->setCurrentWidget(connected_mode_widget_);
        }
    } else {
        adb_header_btn_->setText("⚠️ Connect ADB");
        adb_header_btn_->setStyleSheet(
            "background-color: rgba(239, 68, 68, 0.15);"
            "color: #ef4444;"
            "border: 1px solid rgba(239, 68, 68, 0.3);"
            "border-radius: 4px;"
            "padding: 2px 8px;"
            "font-size: 11px;"
            "font-weight: 600;");

        adb_status_badge_->setText("● DISCONNECTED");
        adb_status_badge_->setStyleSheet("font-size: 10px; font-weight: 700; color: #ef4444; padding: 2px 6px; border-radius: 4px; background: rgba(239, 68, 68, 0.15);");

        if (adb_mode_stack_->currentWidget() == connected_mode_widget_) {
            if (!known_devices_.isEmpty()) {
                adb_mode_stack_->setCurrentWidget(known_mode_widget_);
            } else {
                adb_mode_stack_->setCurrentWidget(pair_mode_widget_);
            }
        }
        adb_status_msg_->setText("ADB not connected. Plug in USB cable or enter wireless debugging port below:");
        adb_status_msg_->setStyleSheet("font-size: 11px; color: #a1a1aa;");
    }
}

void PobsDock::update_status(const CompanionStatus &status) {
    if (status.is_connected) {
        view_stack_->setCurrentWidget(controls_view_);
        status_dot_label_->setPixmap(PobsIcons::status_dot(status.is_streaming_active, 8));
        device_title_->setText(QString::fromStdString(status.device_model));

        if (status.battery_level >= 0) {
            battery_icon_label_->setPixmap(
                PobsIcons::battery(status.battery_level, status.is_charging).pixmap(18, 18));
            battery_icon_label_->show();

            QString batt_str = QString("%1%").arg(status.battery_level);
            if (status.is_charging) {
                batt_str += " (Charging)";
            }
            battery_text_label_->setText(batt_str);
            battery_text_label_->show();
        } else {
            battery_icon_label_->hide();
            battery_text_label_->hide();
        }

        QString orient_pill = (status.physical_orientation == "vertical") ? "📱 Vert" : "📺 Horiz";
        if (status.is_streaming_active) {
            stream_info_label_->setText(QString("%1 @ %2 FPS • %3 • %4")
                .arg(QString::fromStdString(status.active_resolution))
                .arg(status.active_fps)
                .arg(QString::fromStdString(status.active_camera == "front" ? "Front" : "Back"))
                .arg(orient_pill));
            stream_info_label_->setStyleSheet(
                "color: #22c55e;"
                "font-size: 11px;"
                "font-weight: 600;"
                "padding: 4px 8px;"
                "background-color: rgba(34, 197, 94, 0.1);"
                "border: 1px solid rgba(34, 197, 94, 0.2);"
                "border-radius: 6px;");
        } else {
            stream_info_label_->setText(QString("Stream Paused • %1").arg(orient_pill));
            stream_info_label_->setStyleSheet(
                "color: #f59e0b;"
                "font-size: 11px;"
                "font-weight: 600;"
                "padding: 4px 8px;"
                "background-color: rgba(245, 158, 11, 0.1);"
                "border: 1px solid rgba(245, 158, 11, 0.2);"
                "border-radius: 6px;");
        }

        updating_combos_ = true;
        for (int i = 0; i < res_combo_->count(); ++i) {
            if (res_combo_->itemData(i).toString().toStdString() == status.active_resolution) {
                res_combo_->setCurrentIndex(i);
                break;
            }
        }
        for (int i = 0; i < fps_combo_->count(); ++i) {
            if (fps_combo_->itemData(i).toInt() == status.active_fps) {
                fps_combo_->setCurrentIndex(i);
                break;
            }
        }
        updating_combos_ = false;

        if (orientation_combo_) {
            updating_orientation_ = true;
            for (int i = 0; i < orientation_combo_->count(); ++i) {
                if (orientation_combo_->itemData(i).toString().toStdString() == status.orientation_mode) {
                    orientation_combo_->setCurrentIndex(i);
                    break;
                }
            }
            updating_orientation_ = false;
        }

        updating_zoom_ = true;
        int zoom_val = (int)(status.zoom_ratio * 10.0f);
        zoom_slider_->setValue(zoom_val);
        zoom_val_label_->setText(QString("%1x").arg(status.zoom_ratio, 0, 'f', 1));
        updating_zoom_ = false;

        update_camera_buttons_style(status.active_camera);
        update_torch_button_style(status.torch_enabled);
        update_pause_button_style(status.is_streaming_active);
    } else {
        view_stack_->setCurrentWidget(qr_view_);
        status_dot_label_->setPixmap(PobsIcons::status_dot(false, 8));
        device_title_->setText("Ready to Pair");
        battery_icon_label_->hide();
        battery_text_label_->hide();

        QString lan_ip = PobsWebSocketServer::get_local_lan_ip();
        qr_widget_->set_text(QString("pobs://%1:8765").arg(lan_ip));
        ip_pill_label_->setText(QString("Direct LAN: %1:8765").arg(lan_ip));
    }
}

void PobsDock::on_orientation_changed(int index) {
    if (updating_orientation_ || index < 0) return;
    QString mode = orientation_combo_->itemData(index).toString();
    PobsService::instance().set_orientation(mode.toStdString());
}

void PobsDock::on_resolution_changed(int index) {
    if (updating_combos_ || index < 0) return;
    std::string res = res_combo_->itemData(index).toString().toStdString();
    int fps = fps_combo_->currentData().toInt();
    PobsService::instance().set_format(res, fps);
}

void PobsDock::on_fps_changed(int index) {
    if (updating_combos_ || index < 0) return;
    int fps = fps_combo_->itemData(index).toInt();
    std::string res = res_combo_->currentData().toString().toStdString();
    PobsService::instance().set_format(res, fps);
}

void PobsDock::on_zoom_slider_changed(int value) {
    if (updating_zoom_) return;
    float ratio = (float)value / 10.0f;
    zoom_val_label_->setText(QString("%1x").arg(ratio, 0, 'f', 1));
    PobsService::instance().set_zoom(ratio);
}

void PobsDock::on_stream_pause_clicked() {
    is_streaming_active_ = !is_streaming_active_;
    update_pause_button_style(is_streaming_active_);
    PobsService::instance().set_streaming(is_streaming_active_);
}

void PobsDock::on_front_camera_clicked() {
    if (current_facing_ == "front") return;
    current_facing_ = "front";
    update_camera_buttons_style(current_facing_);
    PobsService::instance().switch_camera("front");
}

void PobsDock::on_back_camera_clicked() {
    if (current_facing_ == "back") return;
    current_facing_ = "back";
    update_camera_buttons_style(current_facing_);
    PobsService::instance().switch_camera("back");
}

void PobsDock::on_torch_clicked() {
    torch_active_ = !torch_active_;
    update_torch_button_style(torch_active_);
    PobsService::instance().toggle_torch(torch_active_);
}

void PobsDock::update_camera_buttons_style(const std::string &active_facing) {
    current_facing_ = active_facing;
    bool is_front = (active_facing == "front");

    front_cam_btn_->setChecked(is_front);
    back_cam_btn_->setChecked(!is_front);

    QString active_style =
        "QPushButton {"
        "  background-color: #2563eb;"
        "  border: 1px solid #3b82f6;"
        "  border-radius: 6px;"
        "  color: #ffffff;"
        "  font-weight: 600;"
        "  font-size: 11px;"
        "  padding: 4px 8px;"
        "}";

    QString inactive_style =
        "QPushButton {"
        "  background-color: #27272a;"
        "  border: 1px solid #3f3f46;"
        "  border-radius: 6px;"
        "  color: #a1a1aa;"
        "  font-weight: 500;"
        "  font-size: 11px;"
        "  padding: 4px 8px;"
        "}"
        "QPushButton:hover {"
        "  background-color: #3f3f46;"
        "  color: #f4f4f5;"
        "}";

    front_cam_btn_->setStyleSheet(is_front ? active_style : inactive_style);
    back_cam_btn_->setStyleSheet(!is_front ? active_style : inactive_style);
}

void PobsDock::update_torch_button_style(bool active) {
    torch_active_ = active;
    torch_btn_->setChecked(active);
    torch_btn_->setIcon(PobsIcons::torch(active));

    if (active) {
        torch_btn_->setStyleSheet(
            "QPushButton {"
            "  background-color: #eab308;"
            "  border: 1px solid #facc15;"
            "  border-radius: 6px;"
            "  color: #09090b;"
            "  font-weight: 700;"
            "  font-size: 11px;"
            "  padding: 4px 8px;"
            "}");
    } else {
        torch_btn_->setStyleSheet(
            "QPushButton {"
            "  background-color: #27272a;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  color: #a1a1aa;"
            "  font-weight: 500;"
            "  font-size: 11px;"
            "  padding: 4px 8px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3f3f46;"
            "  color: #f4f4f5;"
            "}");
    }
}

void PobsDock::update_pause_button_style(bool streaming_active) {
    is_streaming_active_ = streaming_active;
    if (streaming_active) {
        stream_pause_btn_->setText("Pause Stream");
        stream_pause_btn_->setIcon(PobsIcons::pause());
        stream_pause_btn_->setIconSize(QSize(18, 18));
        stream_pause_btn_->setStyleSheet(
            "QPushButton {"
            "  background-color: #27272a;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  color: #a1a1aa;"
            "  font-weight: 500;"
            "  font-size: 11px;"
            "  padding: 4px 8px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3f3f46;"
            "  color: #f4f4f5;"
            "}");
    } else {
        stream_pause_btn_->setText("Resume Stream");
        stream_pause_btn_->setIcon(PobsIcons::play());
        stream_pause_btn_->setIconSize(QSize(18, 18));
        stream_pause_btn_->setStyleSheet(
            "QPushButton {"
            "  background-color: #22c55e;"
            "  border: 1px solid #4ade80;"
            "  border-radius: 6px;"
            "  color: #ffffff;"
            "  font-weight: 700;"
            "  font-size: 11px;"
            "  padding: 4px 8px;"
            "}");
    }
}

void register_pobs_dock() {
    PobsDock *dock = new PobsDock();
    dock->setObjectName("PobsDockWidget");
    obs_frontend_add_custom_qdock("pobs_dock", dock);
}

} // namespace pobs
