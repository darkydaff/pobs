#pragma once

#include <QIcon>
#include <QPixmap>
#include <QColor>

namespace pobs {

class PobsIcons {
public:
    static QIcon camera_front(const QColor &color = QColor("#D4D4D8"), int size = 24);
    static QIcon camera_back(const QColor &color = QColor("#D4D4D8"), int size = 24);
    static QIcon torch(bool active, const QColor &color = QColor("#D4D4D8"), int size = 24);
    static QIcon battery(int level, bool charging, const QColor &color = QColor("#A1A1AA"), int size = 20);
    static QIcon display(const QColor &color = QColor("#A1A1AA"), int size = 20);
    static QIcon framerate(const QColor &color = QColor("#A1A1AA"), int size = 20);
    static QIcon zoom(const QColor &color = QColor("#D4D4D8"), int size = 20);
    static QIcon play(const QColor &color = QColor("#D4D4D8"), int size = 20);
    static QIcon pause(const QColor &color = QColor("#D4D4D8"), int size = 20);
    static QPixmap status_dot(bool connected, int size = 10);
};

} // namespace pobs
