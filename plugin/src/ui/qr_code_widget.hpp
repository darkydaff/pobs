#pragma once

#include <QWidget>
#include <QString>
#include <QImage>

namespace pobs {

class QrCodeWidget : public QWidget {
    Q_OBJECT
public:
    explicit QrCodeWidget(QWidget *parent = nullptr);

    void set_text(const QString &text);
    QString text() const { return text_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    void update_qr_image();

    QString text_;
    QImage qr_image_;
};

} // namespace pobs
