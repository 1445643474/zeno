#include "zenolink.h"
#include "zenonode.h"
#include "zenosubgraphscene.h"
#include "nodeeditor/gv/nodesys_common.h"
#include "control/common_id.h"
#include "nodeeditor/gv/zenosocketitem.h"
#include "style/zenostyle.h"
#include "../util/log.h"
#include "settings/zenosettingsmanager.h"
#include "nodeeditor/gv/zenosubgraphview.h"
#include "zenoapplication.h"
#include "model/graphsmanager.h"
#include "zenomainwindow.h"
#include "util/uihelper.h"


ZenoLink::ZenoLink(QGraphicsItem *parent)
    : _base(parent)
{
    connect(&ZenoSettingsManager::GetInstance(), &ZenoSettingsManager::valueChanged, this, [=](QString key) { 
        if (key == zsLinkLineShape) {
            update();
        }
    });
}

ZenoLink::~ZenoLink()
{
}

QRectF ZenoLink::boundingRect() const
{
    return shape().boundingRect();
}

QPainterPath ZenoLink::shape() const
{
    bool bCurve = ZenoSettingsManager::GetInstance().getValue(zsLinkLineShape).toBool();
    if (bCurve)
    {
        auto src = getSrcPos();
        auto dst = getDstPos();
        if (hasLastPath && src == lastSrcPos && dst == lastSrcPos)
            return lastPath;

        QPainterPath path(src);
        if (BEZIER == 0) {
            path.lineTo(dst);
        } else {
            float dist = dst.x() - src.x();
            dist = std::clamp(std::abs(dist), 40.f, 700.f) * BEZIER;
            path.cubicTo(src.x() + dist, src.y(), dst.x() - dist, dst.y(), dst.x(), dst.y());
        }

        hasLastPath = true;
        lastSrcPos = src;
        lastDstPos = dst;
        lastPath = path;
        return path;
    } 
    else 
    {
        QPainterPath path;
        path.moveTo(getSrcPos());
        path.lineTo(getDstPos());
        return path;
    }
}

int ZenoLink::type() const
{
    return Type;
}

void ZenoLink::paint(QPainter* painter, QStyleOptionGraphicsItem const* styleOptions, QWidget* widget)
{
#ifdef ZENO_NODESVIEW_OPTIM
    if (editor_factor < 0.1) {
        return;
    }
#endif
    painter->save();
    QPen pen;
    pen.setColor(isSelected() ? QColor(0xFA6400) : QColor("#4B9EF4"));
    pen.setWidthF(ZenoStyle::scaleWidth(WIDTH));
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(shape());
    painter->restore();
}


ZenoTempLink::ZenoTempLink(
        ZenoSocketItem* socketItem,
        QString nodeId,
        QPointF fixedPos,
        bool fixInput,
        zeno::LinkFunction lnkProp,
        QModelIndexList selNodes)
    : ZenoLink(nullptr)
    , m_fixedSocket(socketItem)
    , m_fixedPos(fixedPos)
    , m_floatingPos(fixedPos)
    , m_bfixInput(fixInput)
    , m_nodeId(nodeId)
    , m_adsortedSocket(nullptr)
    , m_selNodes(selNodes)
    , m_lnkProp(lnkProp)
{
}

ZenoTempLink::~ZenoTempLink()
{
}

QPointF ZenoTempLink::getSrcPos() const
{
    return m_bfixInput ? m_floatingPos : m_fixedPos;
}

QPointF ZenoTempLink::getDstPos() const
{
    return m_bfixInput ? m_fixedPos : m_floatingPos;
}

void ZenoTempLink::setOldLink(const QPersistentModelIndex& link)
{
    m_oldLink = link;
}

QPersistentModelIndex ZenoTempLink::oldLink() const
{
    return m_oldLink;
}

QModelIndexList ZenoTempLink::selNodes() const
{
    return m_selNodes;
}

void ZenoTempLink::paint(QPainter* painter, QStyleOptionGraphicsItem const* styleOptions, QWidget* widget)
{
    painter->save();
    QPen pen;
    pen.setColor(QColor("#5FD2FF"));
    pen.setWidthF(ZenoStyle::scaleWidth(WIDTH));
    if (zeno::Link_Ref == m_lnkProp)
        pen.setStyle(Qt::DashLine);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(shape());
    painter->restore();
}

void ZenoTempLink::setFloatingPos(QPointF pos)
{
    m_floatingPos = pos;
    update();
}

void ZenoTempLink::getFixedInfo(QString& nodeId, QPointF& fixedPos, bool& bFixedInput, zeno::LinkFunction& lnkProp)
{
    nodeId = m_nodeId;
    fixedPos = m_fixedPos;
    bFixedInput = m_bfixInput;
    lnkProp = m_lnkProp;
}

ZenoSocketItem* ZenoTempLink::getAdsorbedSocket() const
{
    return m_adsortedSocket;
}

ZenoSocketItem* ZenoTempLink::getFixedSocket() const
{
    return m_fixedSocket;
}

void ZenoTempLink::setAdsortedSocket(ZenoSocketItem* pSocket)
{
    if (m_adsortedSocket)
    {
        QModelIndex idx = m_adsortedSocket->paramIndex();
        PARAM_LINKS links = idx.data(ROLE_LINKS).value<PARAM_LINKS>();
        if (links.isEmpty() || (links.size() == 1 && links[0] == m_oldLink))
            m_adsortedSocket->setSockStatus(ZenoSocketItem::STATUS_TRY_DISCONN);
    }
    m_adsortedSocket = pSocket;
    if (m_adsortedSocket)
    {
        m_adsortedSocket->setSockStatus(ZenoSocketItem::STATUS_TRY_CONN);
    }
}

int ZenoTempLink::type() const
{
    return Type;
}

void ZenoTempLink::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    ZenoLink::mouseMoveEvent(event);
    m_floatingPos = this->scenePos();
}


ZenoFullLink::ZenoFullLink(const QPersistentModelIndex& idx, ZenoNode* outNode, ZenoNode* inNode)
    : ZenoLink(nullptr)
    , m_index(idx)
    , m_bLegacyLink(false)
    , m_bHover(false)
{
    setAcceptHoverEvents(true);
    setFlags(ItemIsSelectable | ItemIsFocusable);
    ZASSERT_EXIT(inNode && outNode && idx.isValid());

    const QModelIndex& inSockIdx = m_index.data(ROLE_INSOCK_IDX).toModelIndex();
    const QModelIndex& outSockIdx = m_index.data(ROLE_OUTSOCK_IDX).toModelIndex();
    setZValue(ZVALUE_LINK);
    setFlag(QGraphicsItem::ItemIsSelectable);

    if (SOCKPROP_LEGACY == inSockIdx.data(ROLE_PARAM_SOCKPROP) ||
        SOCKPROP_LEGACY == outSockIdx.data(ROLE_PARAM_SOCKPROP))
    {
        m_bLegacyLink = true;
    }

    zeno::EdgeInfo edge = m_index.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();

    QString outKey = QString::fromStdString(edge.outKey);
    QString inKey = QString::fromStdString(edge.inKey);

    m_dstPos = inNode->getSocketPos(inSockIdx, inKey);
    m_srcPos = outNode->getSocketPos(outSockIdx, outKey);
    m_lnkProp = (zeno::LinkFunction)m_index.data(ROLE_LINK_PROP).toInt();

    connect(inNode, SIGNAL(inSocketPosChanged()), this, SLOT(onInSocketPosChanged()));
    connect(outNode, SIGNAL(outSocketPosChanged()), this, SLOT(onOutSocketPosChanged()));
}

bool ZenoFullLink::isLegacyLink() const
{
    return m_bLegacyLink;
}

ZenoFullLink::~ZenoFullLink()
{
}

void ZenoFullLink::onInSocketPosChanged()
{
    if (!m_index.isValid())
        return;

    ZenoNode* pNode = qobject_cast<ZenoNode*>(sender());
    ZASSERT_EXIT(pNode);

    zeno::EdgeInfo edge = m_index.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();
    QString inKey = QString::fromStdString(edge.inKey);

    const QModelIndex& inSockIdx = m_index.data(ROLE_INSOCK_IDX).toModelIndex();

    bool bCollasped = false;
    zeno::SocketType inSockProp = zeno::NoSocket;
    getConnectedState(inSockProp, bCollasped);

    if (inSockProp == zeno::PrimarySocket) {
        setVisible(true);
        m_dstPos = pNode->getSocketPos(inSockIdx, inKey);
    }
    else {
        if (bCollasped) {
            setVisible(false);
        }
        else {
            setVisible(true);
            m_dstPos = pNode->getSocketPos(inSockIdx, inKey);
        }
    }
}

void ZenoFullLink::getConnectedState(zeno::SocketType& inSockProp, bool& inNodeCollasped)
{
    const QModelIndex& inSockIdx = m_index.data(ROLE_INSOCK_IDX).toModelIndex();
    ZASSERT_EXIT(inSockIdx.isValid());
    const QModelIndex& inNode = inSockIdx.data(ROLE_NODE_IDX).toModelIndex();
    inNodeCollasped = inNode.data(ROLE_COLLASPED).toBool();
    inSockProp = (zeno::SocketType)inSockIdx.data(ROLE_SOCKET_TYPE).toInt();
}

void ZenoFullLink::onOutSocketPosChanged()
{
    if (!m_index.isValid())
        return;

    ZenoNode* pNode = qobject_cast<ZenoNode*>(sender());
    ZASSERT_EXIT(pNode);

    zeno::EdgeInfo edge = m_index.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();
    QString outKey = QString::fromStdString(edge.outKey);

    const QModelIndex& outSockIdx = m_index.data(ROLE_OUTSOCK_IDX).toModelIndex();
    m_srcPos = pNode->getSocketPos(outSockIdx, outKey);
}

bool ZenoFullLink::isPrimLink()
{
    const QModelIndex& outSockIdx = m_index.data(ROLE_OUTSOCK_IDX).toModelIndex();
    const QModelIndex& inSockIdx = m_index.data(ROLE_INSOCK_IDX).toModelIndex();
    ZASSERT_EXIT(outSockIdx.isValid() && inSockIdx.isValid(), false);

    zeno::SocketType outprop = (zeno::SocketType)outSockIdx.data(ROLE_SOCKET_TYPE).toInt();
    zeno::SocketType inprop = (zeno::SocketType)inSockIdx.data(ROLE_SOCKET_TYPE).toInt();
    return outprop == zeno::PrimarySocket && outprop == inprop;
}

void ZenoFullLink::focusOnNode(const QModelIndex& nodeIdx)
{
    ZenoSubGraphScene* pScene = qobject_cast<ZenoSubGraphScene*>(scene());
    ZASSERT_EXIT(pScene && !pScene->views().isEmpty());
    if (_ZenoSubGraphView* pView = qobject_cast<_ZenoSubGraphView*>(pScene->views().first()))
    {
        ZASSERT_EXIT(nodeIdx.isValid());
        pView->focusOn(nodeIdx.data(ROLE_NODE_NAME).toString(), QPointF(), false);
    }
}

QPersistentModelIndex ZenoFullLink::linkInfo() const
{
    return m_index;
}

QPainterPath ZenoFullLink::shape() const
{
    return ZenoLink::shape();
}

QPointF ZenoFullLink::getSrcPos() const
{
    return m_srcPos;
}

QPointF ZenoFullLink::getDstPos() const
{
    return m_dstPos;
}

void ZenoFullLink::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    ZenoLink::mousePressEvent(event);
    if (event->button() == Qt::RightButton && !event->isAccepted())
        event->accept();
}

void ZenoFullLink::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    ZenoLink::mouseReleaseEvent(event);
}

void ZenoFullLink::hoverEnterEvent(QGraphicsSceneHoverEvent* event)
{
    if (!m_bHover)
    {
        m_bHover = true;
        update();
    }
    setCursor(QCursor(Qt::PointingHandCursor));
    ZenoLink::hoverEnterEvent(event);
}

void ZenoFullLink::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    if (m_bHover)
    {
        m_bHover = false;
        update();
    }
    setCursor(QCursor(Qt::ArrowCursor));
    ZenoLink::hoverLeaveEvent(event);
}

int ZenoFullLink::type() const
{
    return Type;
}

void ZenoFullLink::paint(QPainter* painter, QStyleOptionGraphicsItem const* styleOptions, QWidget* widget)
{
    if (m_bLegacyLink)
    {
        painter->save();
        QPen pen;
        pen.setColor(isSelected() ? QColor(0xFA6400) : QColor(83, 83, 85));
        pen.setWidthF(ZenoStyle::scaleWidth(3));
        if (zeno::Link_Ref == m_lnkProp)
            pen.setStyle(Qt::DashLine);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(shape());
        painter->restore();
    }
    else
    {
        bool bCollasped = false;
        zeno::SocketType inSockProp = zeno::NoSocket;
        getConnectedState(inSockProp, bCollasped);

        if (inSockProp == zeno::PrimarySocket) {
            painter->save();
            QPen pen;
            pen.setColor(isSelected() ? QColor(0xFA6400) : QColor("#FFFFFF"));
            pen.setWidthF(ZenoStyle::scaleWidth(3));
            if (zeno::Link_Ref == m_lnkProp)
                pen.setStyle(Qt::DashLine);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(shape());
            painter->restore();
        }
        else {
            painter->save();
            QPen pen;
            pen.setColor(isSelected() ? QColor(0xFA6400) : QColor("#4B9EF4"));
            pen.setWidthF(ZenoStyle::scaleWidth(1));
            if (zeno::Link_Ref == m_lnkProp)
                pen.setStyle(Qt::DashLine);
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(shape());
            painter->restore();
        }
        //ZenoLink::paint(painter, styleOptions, widget);
    }
}

void ZenoFullLink::contextMenuEvent(QGraphicsSceneContextMenuEvent* event)
{
    setSelected(true);

    QMenu* menu = new QMenu;
    QAction* pTraceToOutput = new QAction(tr("trace to output socket"));
    QAction* pTraceToInput = new QAction(tr("trace to input socket"));

    connect(pTraceToOutput, &QAction::triggered, this, [=]() {
        QModelIndex outSockIdx = m_index.data(ROLE_OUTSOCK_IDX).toModelIndex();
        QModelIndex nodeIdx = outSockIdx.data(ROLE_NODE_IDX).toModelIndex();
        focusOnNode(nodeIdx);
    });
    connect(pTraceToInput, &QAction::triggered, this, [=]() {
        QModelIndex inSockIdx = m_index.data(ROLE_INSOCK_IDX).toModelIndex();
        QModelIndex nodeIdx = inSockIdx.data(ROLE_NODE_IDX).toModelIndex();
        focusOnNode(nodeIdx);
    });

    menu->addAction(pTraceToOutput);
    menu->addAction(pTraceToInput);

    menu->exec(QCursor::pos());
    menu->deleteLater();
}