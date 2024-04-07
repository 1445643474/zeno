#ifndef BREVIARYITEM_H
#define BREVIARYITEM_H

#include <QtWidgets>

class BreviaryDlg : public QDialog
{
    Q_OBJECT
public:
    BreviaryDlg(QWidget* parent = nullptr, Qt::WindowFlags f = Qt::WindowFlags());
    void setView(QGraphicsView* pView);
    ~BreviaryDlg();
protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* evt) override;

public slots:
    void updateRectSize();
    void sceneChangedSlot(const QList<QRectF>& region);
private:
    void updatePixmap();
    bool isDragArea();
private:
    QPixmap m_pixmap;
    QRectF m_rect;
    QPoint m_movePos;
    bool m_bMove;
    QGraphicsView* m_pView;
    qreal m_scale;
    QPoint m_bottomRight;
};
#endif // BREVIARYITEM_H
