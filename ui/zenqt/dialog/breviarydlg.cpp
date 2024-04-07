#include "breviarydlg.h"
#include <QDebug>
#include "style/zenostyle.h"
#include "settings/zenosettingsmanager.h"

BreviaryDlg::BreviaryDlg(QWidget* parent, Qt::WindowFlags f)
    :QDialog(parent, f)
    , m_bMove(false)
    , m_pView(nullptr)
    , m_scale(0.0)
{
    setAttribute(Qt::WA_DeleteOnClose);
    this->resize(ZenoStyle::dpiScaledSize(QSize(300, 200)));
    this->setMaximumSize(ZenoStyle::dpiScaledSize(QSize(300, 300)));
    this->setMinimumSize(ZenoStyle::dpiScaledSize(QSize(100, 100)));
    setWindowFlag(Qt::FramelessWindowHint);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Background, QColor(34, 34, 34));
    setWindowOpacity(0.8);
    setPalette(palette);
    this->installEventFilter(this);
    this->setAttribute(Qt::WA_Hover, true);
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
    connect(pScene, &QGraphicsScene::changed, this, &BreviaryDlg::sceneChangedSlot, Qt::UniqueConnection);
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
    //draw drag area
    QPoint pos = this->mapFromGlobal(geometry().topLeft()) + QPoint(8, 8);
    QPolygonF points;
    points << QPoint(pos.x(), pos.y() + 8) << pos << QPoint(pos.x() + 8, pos.y());
    QPainterPath path;
    path.addPolygon(points);
    painter.drawPath(path);
}

void BreviaryDlg::mousePressEvent(QMouseEvent* event)
{
    m_movePos = event->pos();
    m_bMove = !isDragArea();
}

void BreviaryDlg::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_bMove)
    {
        m_bMove = false;
    }
    //else
    //{
    //    QRectF pixmapRect(QPointF(0, 0), m_pixmap.size());
    //    if (!this->rect().contains(m_rect.toRect()))
    //    {
    //        auto offset = pixmapRect.center() - m_rect.center();
    //        m_rect = QRectF(m_rect.topLeft() + offset, m_rect.size());
    //        offset /= m_scale;
    //        m_pView->translate(-offset.x(), -offset.y());
    //        update();
    //    }
    //}
    
    m_movePos = QPoint();
}

void BreviaryDlg::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_movePos.isNull())
    {
        if (m_bMove)
        {
            auto offset = (event->pos() - m_movePos);
            m_movePos = event->pos();
            m_bMove = true;
            m_rect = QRectF(m_rect.topLeft() + offset, m_rect.size());
            offset /= m_scale;
            m_pView->translate(-offset.x(), -offset.y());
            update();
        }
        else
        {
            if (m_bottomRight.isNull())
                m_bottomRight = geometry().bottomRight();
            QPoint evtPos = this->mapToGlobal(event->pos());
            QSize size(m_bottomRight.x() - evtPos.x(), m_bottomRight.y() - evtPos.y());
            if (size.width() > maximumWidth())
                size.setWidth(maximumWidth());
            else if (size.width() < minimumWidth())
                size.setWidth(minimumWidth());
            if (size.height() > maximumHeight())
                size.setHeight(maximumHeight());
            else if (size.height() < minimumHeight())
                size.setHeight(minimumHeight());
            QPoint pos(m_bottomRight.x() - size.width(), m_bottomRight.y() - size.height());
            this->setGeometry(QRect(pos, size));
            updatePixmap();
            updateRectSize();
        }
    }
}

bool BreviaryDlg::eventFilter(QObject* obj, QEvent* evt)
{
    if (obj == m_pView)
    {
        if (evt->type() == QEvent::Resize)
        {
            updateRectSize();
            QPoint pos = m_pView->geometry().bottomRight() - QPoint(size().width(), size().height());
            pos = m_pView->mapToGlobal(pos);
            this->move(pos);
        }
    }
    if (obj == this && evt->type() == QEvent::HoverMove)
    {
        isDragArea();
    }
    return QDialog::eventFilter(obj, evt);
}

void BreviaryDlg::sceneChangedSlot(const QList<QRectF>& region)
{
    if (region.isEmpty() || !this->isVisible())
        return;
    updatePixmap();
    updateRectSize();
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

bool BreviaryDlg::isDragArea() {
    QPoint pos = cursor().pos();
    auto rect = this->geometry();
    int diffLeft = pos.x() - rect.left();
    int diffTop = pos.y() - rect.top();
    qreal width = ZenoStyle::scaleWidth(16);

    Qt::CursorShape cursorShape(Qt::ArrowCursor);
    if (rect.contains(pos)) {
        if (diffTop < width && diffTop >= 0) {
            if (diffLeft < width && diffLeft >= 0) {
                cursorShape = Qt::SizeFDiagCursor;
            }
        }
    }
    setCursor(cursorShape);
    bool result = cursorShape == Qt::SizeFDiagCursor;
    return result;
}
