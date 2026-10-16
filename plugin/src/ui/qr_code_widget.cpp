#include "qr_code_widget.hpp"
#include "qrcodegen.hpp"

#include <QPainter>
#include <QPaintEvent>

namespace pobs {

QrCodeWidget::QrCodeWidget(QWidget *parent)
    : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedSize(130, 130);
}

void QrCodeWidget::set_text(const QString &text) {
    if (text_ == text) return;
    text_ = text;
    update_qr_image();
    update();
}

void QrCodeWidget::update_qr_image() {
    if (text_.isEmpty()) {
        qr_image_ = QImage();
        return;
    }

    try {
        const auto qr = qrcodegen::QrCode::encodeText(
            text_.toUtf8().constData(),
            qrcodegen::QrCode::Ecc::MEDIUM
        );

        int size = qr.getSize();
        int border = 2;
        int img_size = size + border * 2;

        qr_image_ = QImage(img_size, img_size, QImage::Format_ARGB32);
        qr_image_.fill(Qt::white);

        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (qr.getModule(x, y)) {
                    qr_image_.setPixelColor(x + border, y + border, Qt::black);
                }
            }
        }
    } catch (...) {
        qr_image_ = QImage();
    }
}

void QrCodeWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Draw background card
    QRect r = rect().adjusted(4, 4, -4, -4);
    painter.setPen(QPen(QColor(220, 220, 220), 1));
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(r, 8, 8);

    if (qr_image_.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(r, Qt::AlignCenter, "Generating QR...");
        return;
    }

    // Scale QR image cleanly (nearest neighbor to keep sharp pixels)
    int avail_w = r.width() - 20;
    int avail_h = r.height() - 20;
    int target_dim = qMin(avail_w, avail_h);
    if (target_dim > 0) {
        int x = r.left() + (r.width() - target_dim) / 2;
        int y = r.top() + (r.height() - target_dim) / 2;
        QRect qr_target(x, y, target_dim, target_dim);

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(qr_target, qr_image_);
    }
}

QSize QrCodeWidget::sizeHint() const {
    return QSize(130, 130);
}

} // namespace pobs
