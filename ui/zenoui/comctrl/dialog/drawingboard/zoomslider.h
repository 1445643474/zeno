#ifndef ZOOMSLIDER_H
#define ZOOMSLIDER_H

#include <QtWidgets>
#include <zenoui/comctrl/ztoolbutton.h>
#include <zenoui/comctrl/zlineedit.h>

class ZoomSlider : public QWidget
{
    Q_OBJECT

public:
    explicit ZoomSlider(QWidget *parent = 0);
    ~ZoomSlider();
    QSlider* slider();

private slots:
    void on_horizontalSlider_valueChanged(int value);

    void on_increaseButton_clicked();

    void on_decreaseButton_clicked();

    void on_precent_valueChanged();

private:
    ZToolButton* m_pZoomOutBtn;
    ZToolButton* m_pZoomInBtn;
    QSlider* m_pZoomSlider;
    ZLineEdit* m_pPercentEdit;
    QLabel* m_pPercentLabel;
};

#endif // ZOOMSLIDER_H
