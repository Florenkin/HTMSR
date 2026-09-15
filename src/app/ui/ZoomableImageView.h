#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

namespace htmsr::app {

class ZoomableImageView final : public QWidget {
    Q_OBJECT

public:
    explicit ZoomableImageView(QWidget* parent = nullptr);

public slots:
    void setImageFile(const QString& imagePath);
    void clear();
    void fitToView();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    QPointF viewportCenter() const;
    QRectF imageRect() const;

    QImage image_;
    QString imagePath_;
    double scale_ = 1.0;
    QPointF offset_;
    QPoint lastMousePosition_;
    bool panning_ = false;
};

} // namespace htmsr::app
