#include "palettebutton.h"
#include "datasingleton.h"
#include <QMouseEvent>
#include <QPainter>
#include <zenoui/style/zenostyle.h>

PaletteButton::PaletteButton(const QColor &color, QWidget* parent) 
    : QToolButton(parent)
    , mColor(color)
{
    setFixedSize(ZenoStyle::dpiScaledSize(QSize(24, 24)));
    setCheckable(true);
}

void PaletteButton::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
        DataSingleton::Instance()->setPrimaryColor(mColor);
    else if(event->button() == Qt::RightButton)
        DataSingleton::Instance()->setSecondaryColor(mColor);

    QToolButton::mousePressEvent(event);
}

void PaletteButton::updateState() 
{
    setChecked(false);
    update();
}

void PaletteButton::paintEvent(QPaintEvent* evt)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QBrush(mColor));
    painter.drawEllipse(this->rect().center(), rect().width()/2 - 6, rect().height()/2 - 6);
    if (this->isChecked())
    {
        QPen pen(QColor(Qt::white), 2);
        painter.setPen(pen);
        painter.setBrush(QBrush());
        painter.drawEllipse(this->rect().center(), rect().width() / 2 - 2, rect().height() / 2 - 2);
    }
}
