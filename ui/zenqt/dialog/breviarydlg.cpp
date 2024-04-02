#include "breviarydlg.h"
#include <QDebug>
#include "style/zenostyle.h"

BreviaryDlg::BreviaryDlg(QWidget* parent, Qt::WindowFlags f)
    :QDialog(parent, f)
    , m_bMove(false)
    , m_pView(nullptr)
    , m_scale(0.0)
{
    setAttribute(Qt::WA_DeleteOnClose);
    this->resize(ZenoStyle::dpiScaledSize(QSize(300, 200)));
    this->setMinimumSize(ZenoStyle::dpiScaledSize(QSize(100, 100)));
    setWindowFlag(Qt::FramelessWindowHint);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Background, QColor(34, 34, 34));
    setWindowOpacity(0.8);
    setPalette(palette);
}

void BreviaryDlg::setView(QGraphicsView* pView)
{
    if (m_pView == pView)
        return;
    m_pView = pView;
    m_pView->installEventFilter(this);
    updatePixmap();
    updateRectSize();
    connect(m_pView, SIGNAL(zoomed(qreal)), this, SLOT(updateRectSize()), Qt::UniqueConnection);
    connect(m_pView, SIGNAL(moveSignal()), this, SLOT(updateRectSize()), Qt::UniqueConnection);
    const auto pScene = m_pView->scene();
    if (!pScene)
        return;
    connect(pScene, &QGraphicsScene::changed, this, [=](const QList<QRectF>& region) {
        if (region.isEmpty())
            return;
        updatePixmap();
        updateRectSize();
    }, Qt::UniqueConnection);
}

BreviaryDlg::~BreviaryDlg()
{
}

void BreviaryDlg::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
}

void BreviaryDlg::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    int y = (this->height() - m_pixmap.height()) / 2;
    int x = (this->width() - m_pixmap.width()) / 2;
    if (!m_pixmap.isNull())
    {
        painter.drawPixmap(QPoint(x, y), m_pixmap);
        QPen pen(QColor("#000"));
        pen.setWidthF(ZenoStyle::dpiScaled(2));
        painter.setPen(pen);
        painter.drawRect(QRect(QPoint(x, y), m_pixmap.size()));
    }
    if (m_rect.isValid())
    {
        QPen pen(QColor(89, 93, 99));
        pen.setWidthF(ZenoStyle::dpiScaled(2));
        painter.setPen(pen);
        QRectF rect = m_rect.adjusted(x, y, x, y);
        painter.drawRect(rect);
    }
    painter.drawRect(this->rect());
}

void BreviaryDlg::mousePressEvent(QMouseEvent* event)
{
    m_movePos = event->pos();
}

void BreviaryDlg::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_bMove)
    {
        m_bMove = false;
    }
    else
    {
        QRectF pixmapRect(QPointF(0, 0), m_pixmap.size());
        if (!this->rect().contains(m_rect.toRect()))
        {
            auto offset = pixmapRect.center() - m_rect.center();
            m_rect = QRectF(m_rect.topLeft() + offset, m_rect.size());
            offset /= m_scale;
            m_pView->translate(-offset.x(), -offset.y());
            update();
        }
    }
    
    m_movePos = QPoint();
}

void BreviaryDlg::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_movePos.isNull())
    {
        auto offset = (event->pos() - m_movePos) ;
        m_movePos = event->pos();
        m_bMove = true;
        m_rect = QRectF(m_rect.topLeft() + offset, m_rect.size());
        offset /= m_scale;
        m_pView->translate(-offset.x(), -offset.y());
        update();
    }
}

bool BreviaryDlg::eventFilter(QObject* obj, QEvent* evt)
{
    if (obj == m_pView && evt->type() == QEvent::Resize)
    {
        updateRectSize();
        QPoint pos = m_pView->geometry().bottomRight() - QPoint(size().width(), size().height());
        pos = m_pView->mapToGlobal(pos);
        this->move(pos);
    }
    return QDialog::eventFilter(obj, evt);
}

void BreviaryDlg::updatePixmap()
{
    if (!m_pView)
        return;
    const auto pScene = m_pView->scene();
    if (!pScene)
        return;
    QSizeF size = this->size();
    QSizeF cSize = pScene->itemsBoundingRect().size();
    qreal scaleX = size.width() / cSize.width();
    qreal scaleY = size.height() / cSize.height();
    m_scale = qMin(scaleX, scaleY);
    size = cSize * m_scale;
    QPixmap pixmap(size.width(), size.height());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    m_pView->scene()->render(&painter);
    QString str;
    pixmap.save(str, "png");
    m_pixmap = pixmap;
    update();
}

void BreviaryDlg::updateRectSize()
{
    QPointF sPos = m_pView->scene()->sceneRect().topLeft();
    QPointF pos = m_pView->mapToScene(0, 0);
    QSizeF size = m_pView->viewport()->size();
    QPointF bottomRight = m_pView->mapToScene(QPoint(size.width(), size.height()));
    size = QSizeF(bottomRight.x() - pos.x(), bottomRight.y() - pos.y()) * m_scale;
    pos = (pos - sPos) * m_scale;
    m_rect = QRectF(pos, size);
    update();
}
