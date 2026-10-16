#include "pobs_icons.hpp"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace pobs {

static QPixmap create_base_pixmap(int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

static void setup_painter(QPainter &p, const QColor &color, double pen_width = 2.0) {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPen pen(color, pen_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
}

QIcon PobsIcons::camera_front(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Camera outline (Rune camera style)
    QPainterPath path;
    path.moveTo(3, 8);
    path.lineTo(7, 8);
    path.lineTo(9, 5);
    path.lineTo(15, 5);
    path.lineTo(17, 8);
    path.lineTo(21, 8);
    path.arcTo(19, 8, 4, 4, 90, -90);
    path.lineTo(23, 18);
    path.arcTo(19, 16, 4, 4, 0, -90);
    path.lineTo(3, 20);
    path.arcTo(1, 16, 4, 4, 270, -90);
    path.lineTo(1, 10);
    path.arcTo(1, 8, 4, 4, 180, -90);
    path.closeSubpath();
    p.drawPath(path);

    // Front lens aperture
    p.drawEllipse(QPointF(12, 13.5), 3.5, 3.5);

    // Front user/lens indicator dot
    p.setBrush(color);
    p.drawEllipse(QPointF(12, 13.5), 1.2, 1.2);

    return QIcon(pm);
}

QIcon PobsIcons::camera_back(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Camera outline
    QPainterPath path;
    path.moveTo(3, 8);
    path.lineTo(7, 8);
    path.lineTo(9, 5);
    path.lineTo(15, 5);
    path.lineTo(17, 8);
    path.lineTo(21, 8);
    path.arcTo(19, 8, 4, 4, 90, -90);
    path.lineTo(23, 18);
    path.arcTo(19, 16, 4, 4, 0, -90);
    path.lineTo(3, 20);
    path.arcTo(1, 16, 4, 4, 270, -90);
    path.lineTo(1, 10);
    path.arcTo(1, 8, 4, 4, 180, -90);
    path.closeSubpath();
    p.drawPath(path);

    // Studio lens with double ring
    p.drawEllipse(QPointF(12, 13.5), 4.5, 4.5);
    p.drawEllipse(QPointF(12, 13.5), 2.2, 2.2);

    return QIcon(pm);
}

QIcon PobsIcons::torch(bool active, const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    QColor use_color = active ? QColor("#F59E0B") : color;
    setup_painter(p, use_color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Flashlight body (Rune flashlight style)
    QPainterPath body;
    body.moveTo(18, 5);
    body.lineTo(15, 2);
    body.lineTo(9, 2);
    body.lineTo(6, 5);
    body.lineTo(9, 9);
    body.lineTo(9, 20);
    body.arcTo(9, 19, 6, 4, 180, 180);
    body.lineTo(15, 9);
    body.closeSubpath();
    p.drawPath(body);

    p.drawLine(QPointF(6, 5), QPointF(18, 5));
    p.drawLine(QPointF(12, 11), QPointF(12, 14));

    if (active) {
        // Rays emitting from top
        p.drawLine(QPointF(12, 0.5), QPointF(12, -1.5));
        p.drawLine(QPointF(6, 1.5), QPointF(4, 0));
        p.drawLine(QPointF(18, 1.5), QPointF(20, 0));
    }

    return QIcon(pm);
}

QIcon PobsIcons::battery(int level, bool charging, const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.6);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Battery shell: rect + small terminal cap
    p.drawRoundedRect(QRectF(2, 6, 17, 12), 2.5, 2.5);
    p.drawLine(QPointF(21, 10), QPointF(21, 14));

    if (charging) {
        // Lightning bolt icon
        p.setBrush(QColor("#22C55E"));
        p.setPen(Qt::NoPen);
        QPainterPath bolt;
        bolt.moveTo(11, 7);
        bolt.lineTo(6, 13);
        bolt.lineTo(10.5, 13);
        bolt.lineTo(9, 17.5);
        bolt.lineTo(15, 11.5);
        bolt.lineTo(11.5, 11.5);
        bolt.closeSubpath();
        p.drawPath(bolt);
    } else if (level >= 0) {
        // Fill level bar
        int clamped = std::clamp(level, 0, 100);
        double fill_width = (13.0 * clamped) / 100.0;
        QColor fill_color = (clamped > 20) ? color : QColor("#EF4444");
        p.setBrush(fill_color);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(4, 8, std::max(2.0, fill_width), 8), 1.5, 1.5);
    }

    return QIcon(pm);
}

QIcon PobsIcons::display(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Monitor display outline
    p.drawRoundedRect(QRectF(2, 3, 20, 13), 2.5, 2.5);
    p.drawLine(QPointF(12, 16), QPointF(12, 20));
    p.drawLine(QPointF(8, 20), QPointF(16, 20));

    return QIcon(pm);
}

QIcon PobsIcons::framerate(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Speedometer / Gauge
    p.drawArc(QRectF(3, 4, 18, 18), 35 * 16, 215 * 16);
    p.drawLine(QPointF(12, 13), QPointF(16.5, 8.5));
    p.setBrush(color);
    p.drawEllipse(QPointF(12, 13), 1.8, 1.8);

    return QIcon(pm);
}

QIcon PobsIcons::zoom(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    setup_painter(p, color, 1.8);

    double scale = size / 24.0;
    p.scale(scale, scale);

    // Magnifying glass lens
    p.drawEllipse(QPointF(11, 11), 6.5, 6.5);
    // Handle
    p.drawLine(QPointF(16, 16), QPointF(21, 21));

    return QIcon(pm);
}

QIcon PobsIcons::play(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    double scale = size / 24.0;
    p.scale(scale, scale);

    QPainterPath triangle;
    triangle.moveTo(7, 5);
    triangle.lineTo(19, 12);
    triangle.lineTo(7, 19);
    triangle.closeSubpath();
    p.drawPath(triangle);

    return QIcon(pm);
}

QIcon PobsIcons::pause(const QColor &color, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(color);
    p.setPen(Qt::NoPen);

    double scale = size / 24.0;
    p.scale(scale, scale);

    p.drawRoundedRect(QRectF(6, 5, 3.5, 14), 1.2, 1.2);
    p.drawRoundedRect(QRectF(14.5, 5, 3.5, 14), 1.2, 1.2);

    return QIcon(pm);
}

QPixmap PobsIcons::status_dot(bool connected, int size) {
    QPixmap pm = create_base_pixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor fill = connected ? QColor("#22C55E") : QColor("#71717A");
    p.setBrush(fill);
    p.setPen(Qt::NoPen);
    p.drawEllipse(0, 0, size, size);

    return pm;
}

} // namespace pobs
