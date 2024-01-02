#include "zoomslider.h"
#include <zenoui/style/zenostyle.h>

static const int BUTTON_STEP = 10;

ZoomSlider::ZoomSlider(QWidget *parent) :
    QWidget(parent)
{
    QHBoxLayout* pLayout = new QHBoxLayout(this);
    pLayout->setMargin(0);
    pLayout->setSpacing(0);
    m_pZoomInBtn = new ZToolButton(ZToolButton::Opt_HasIcon, QIcon(":/icons/zoom_in.svg"), ZenoStyle::dpiScaledSize(QSize(16, 16)), tr("Zoom In"), this);
    m_pZoomOutBtn = new ZToolButton(ZToolButton::Opt_HasIcon, QIcon(":/icons/zoom_out.svg"), ZenoStyle::dpiScaledSize(QSize(16, 16)), tr("Zoom Out"), this);
    QSize btnSize = ZenoStyle::dpiScaledSize(QSize(20, 20));
    m_pZoomInBtn->setFixedSize(btnSize);
    m_pZoomOutBtn->setFixedSize(btnSize);
    m_pZoomSlider = new QSlider(Qt::Horizontal, this);
    m_pZoomSlider->setRange(1, 400);
    m_pZoomSlider->setSingleStep(5);
    m_pPercentEdit = new ZLineEdit(this);
    m_pPercentEdit->setFixedWidth(55);
    m_pPercentEdit->setValidator(new QIntValidator(m_pPercentEdit));
    m_pPercentEdit->setAlignment(Qt::AlignCenter);
    m_pPercentLabel = new QLabel("%", this);
    m_pPercentLabel->setFixedWidth(40);

    pLayout->addWidget(m_pZoomOutBtn);
    pLayout->addWidget(m_pZoomSlider);
    pLayout->addWidget(m_pZoomInBtn);
    pLayout->addWidget(m_pPercentEdit);
    pLayout->addWidget(m_pPercentLabel);

    connect(m_pZoomInBtn, &ZToolButton::clicked, this, &ZoomSlider::on_increaseButton_clicked);
    connect(m_pZoomOutBtn, &ZToolButton::clicked, this, &ZoomSlider::on_decreaseButton_clicked);
    connect(m_pZoomSlider, &QSlider::valueChanged, this, &ZoomSlider::on_horizontalSlider_valueChanged);
    connect(m_pPercentEdit, &ZLineEdit::editingFinished, this, &ZoomSlider::on_precent_valueChanged);

    m_pZoomSlider->setValue(100);
}

ZoomSlider::~ZoomSlider()
{
}

QSlider *ZoomSlider::slider()
{
    return m_pZoomSlider;
}

void ZoomSlider::on_horizontalSlider_valueChanged(int value)
{
    m_pPercentEdit->setText(QString::number(value));
}

void ZoomSlider::on_increaseButton_clicked()
{
    auto value = m_pZoomSlider->value();
    value += BUTTON_STEP;
    m_pZoomSlider->setValue(value);
}

void ZoomSlider::on_decreaseButton_clicked()
{
    auto value = m_pZoomSlider->value();
    value -= BUTTON_STEP;
    m_pZoomSlider->setValue(value);
}

void ZoomSlider::on_precent_valueChanged()
{
    auto value = m_pPercentEdit->text().toInt();
    m_pZoomSlider->setValue(value);
}
