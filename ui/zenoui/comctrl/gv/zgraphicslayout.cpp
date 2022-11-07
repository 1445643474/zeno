#include "zgraphicslayout.h"
#include <set>
#include <zassert.h>
#include "zenogvhelper.h"
#include "variantptr.h"
#include "zenoparamwidget.h"


ZGraphicsLayout::ZGraphicsLayout(bool bHor)
    : m_spacing(0)
    , m_parent(nullptr)
    , m_bHorizontal(bHor)
    , m_parentItem(nullptr)
{
}

ZGraphicsLayout::~ZGraphicsLayout()
{
    clear();
}

void ZGraphicsLayout::addItem(QGraphicsItem* pItem)
{
    if (!pItem) return;

    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Item;
    item->pItem = pItem;
    item->pLayout = nullptr;
    item->pItem->setData(GVKEY_PARENT_LAYOUT, QVariant::fromValue((void*)this));
    pItem->setParentItem(m_parentItem);
    m_items.append(item);
}

void ZGraphicsLayout::addItem(QGraphicsItem* pItem, Qt::Alignment flag)
{
    if (!pItem) return;

    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Item;
    item->pItem = pItem;
    item->pLayout = nullptr;
    item->pItem->setData(GVKEY_PARENT_LAYOUT, QVariant::fromValue((void*)this));
    item->alignment = flag;
    pItem->setParentItem(m_parentItem);
    m_items.append(item);
}

void ZGraphicsLayout::insertItem(int i, QGraphicsItem* pItem)
{
    if (!pItem) return;

    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Item;
    item->pItem = pItem;
    item->pLayout = nullptr;
    item->pItem->setData(GVKEY_PARENT_LAYOUT, QVariant::fromValue((void*)this));
    pItem->setParentItem(m_parentItem);

    m_items.insert(i, item);
}

void ZGraphicsLayout::addLayout(ZGraphicsLayout* pLayout)
{
    if (!pLayout) return;

    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Layout;
    item->pItem = nullptr;
    item->pLayout = pLayout;
    pLayout->m_parent = this;
    pLayout->setParentItem(m_parentItem);
    m_items.append(item);
}

void ZGraphicsLayout::insertLayout(int i, ZGraphicsLayout* pLayout)
{
    if (!pLayout) return;

    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Layout;
    item->pItem = nullptr;
    item->pLayout = pLayout;
    pLayout->m_parent = this;
    pLayout->setParentItem(m_parentItem);

    m_items.insert(i, item);
}

void ZGraphicsLayout::addSpacing(qreal size)
{
    QSharedPointer<ZGvLayoutItem> item(new ZGvLayoutItem);
    item->type = Type_Spacing;
    item->pItem = nullptr;
    item->pLayout = nullptr;
    if (size == -1)
    {
        item->gvItemSz.policy = m_bHorizontal ? QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed) :
                                    QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    else
        item->gvItemSz.policy = QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    item->gvItemSz.minSize = m_bHorizontal ? QSizeF(size, 0) : QSizeF(0, size);

    m_items.append(item);
}

QGraphicsItem* ZGraphicsLayout::parentItem() const
{
    return m_parentItem;
}

ZGraphicsLayout* ZGraphicsLayout::parentLayout() const
{
    return m_parent;
}

void ZGraphicsLayout::setParentItem(QGraphicsItem* parent)
{
    m_parentItem = parent;
    //make every item in layout as the children of m_parentItem.
    for (auto item : m_items)
    {
        if (item->type == Type_Item) {
            item->pItem->setParentItem(parent);
        }
        else if (item->type == Type_Layout) {
            item->pLayout->setParentItem(parent);
        }
    }
}

ZGraphicsLayout* ZGraphicsLayout::parent() const
{
    return m_parent;
}

void ZGraphicsLayout::setSpacing(qreal spacing)
{
    m_spacing = spacing;
}

qreal ZGraphicsLayout::spacing() const
{
    return m_spacing;
}

void ZGraphicsLayout::setStretch(QList<int> stretchs)
{

}

void ZGraphicsLayout::setAlignment(QGraphicsItem* item, Qt::Alignment flag)
{
    for (auto _item : m_items)
    {
        if (_item->pItem == item) {
            _item->alignment = flag;
        }
    }
}

void ZGraphicsLayout::setContentsMargin(qreal top, qreal left, qreal bottom, qreal right)
{
    m_margins.setLeft(left);
    m_margins.setTop(top);
    m_margins.setRight(right);
    m_margins.setBottom(bottom);
}

QMargins ZGraphicsLayout::getContentsMargin() const
{
    return m_margins;
}

void ZGraphicsLayout::removeItem(QGraphicsItem* item)
{
    for (int i = 0; i < m_items.size(); i++)
    {
        if (m_items[i]->pItem == item) {
            delete item;
            m_items.remove(i);
            break;
        }
    }
}

void ZGraphicsLayout::removeLayout(ZGraphicsLayout* layout)
{
    //delete item from layout.
    layout->clear();
    for (int i = 0; i < m_items.size(); i++)
    {
        if (m_items[i]->pLayout == layout) {
            m_items.remove(i);
            break;
        }
    }
}

void ZGraphicsLayout::clear()
{
    while (!m_items.isEmpty())
    {
        auto item = m_items.first();
        if (item->type == Type_Layout) {
            item->pLayout->clear();
        }
        else if (item->type == Type_Item) {
            delete item->pItem;
        }
        m_items.removeFirst();
    }
}

QSizeF ZGraphicsLayout::calculateSize()
{
    //todo: support fixed size directly for whole layout.
    QSizeF size(0, 0);

    QSizeF szMargin(m_margins.left() + m_margins.right(), m_margins.top() + m_margins.bottom());
    size += szMargin;

    for (int i = 0; i < m_items.size(); i++)
    {
        auto item = m_items[i];
        item->bDirty = true;
        switch (item->type)
        {
            case Type_Item:
            {
                if (item->pItem && !item->pItem->isVisible())
                    continue;

                //item with a layout, setup it recursively.
                ZGraphicsLayout* pLayout = QVariantPtr<ZGraphicsLayout>::asPtr(item->pItem->data(GVKEY_OWNLAYOUT));
                if (pLayout)
                {
                    QSizeF _size = pLayout->calculateSize();
                    if (m_bHorizontal) {
                        size.setHeight(qMax(_size.height(), size.height()));
                        size += QSizeF(_size.width(), 0);
                    }
                    else {
                        size.setWidth(qMax(_size.width(), size.width()));
                        size += QSizeF(0, _size.height());
                    }
                }
                else
                {
                    QSizeF sizeHint = ZenoGvHelper::sizehintByPolicy(item->pItem);
                    if (m_bHorizontal) {
                        size.setHeight(qMax(sizeHint.height(), size.height()));
                        size += QSizeF(sizeHint.width(), 0);
                    }
                    else {
                        size.setWidth(qMax(sizeHint.width(), size.width()));
                        size += QSizeF(0, sizeHint.height());
                    }
                }
                break;
            }
            case Type_Spacing:
            {
                if (m_bHorizontal) {
                    size.setHeight(qMax(item->gvItemSz.minSize.height(), size.height()));
                    size += QSizeF(item->gvItemSz.minSize.width(), 0);
                }
                else {
                    size.setWidth(qMax(item->gvItemSz.minSize.width(), size.width()));
                    size += QSizeF(0, item->gvItemSz.minSize.height());
                }
                break;
            }
            case Type_Layout:
            {
                QSizeF _size = item->pLayout->calculateSize();
                if (m_bHorizontal) {
                    size.setHeight(qMax(_size.height(), size.height()));
                    size += QSizeF(_size.width(), 0);
                }
                else {
                    size.setWidth(qMax(_size.width(), size.width()));
                    size += QSizeF(0, _size.height());
                }
                break;
            }
        }
        if (i < m_items.size() - 1)
        {
            if (m_bHorizontal)
            {
                size += QSizeF(m_spacing, 0);
            }
            else
            {
                size += QSizeF(0, m_spacing);
            }
        }
    }
    return size;
}

void ZGraphicsLayout::updateHierarchy(ZGraphicsLayout* pLayout)
{
    ZGraphicsLayout* rootLayout = visitRoot(pLayout);
    QSizeF sz = rootLayout->calculateSize();
    rootLayout->calcItemsSize(sz);

    QPointF pos(0, 0);  //by parent item.
    QRectF rc(0, 0, sz.width(), sz.height());

    rootLayout->setup(rc);

    //setup parent item.
    QGraphicsItem* pItem = rootLayout->parentItem();
    if (pItem)
    {
        SizeInfo info;
        info.pos = pItem->pos();
        info.minSize = sz;
        ZenoGvHelper::setSizeInfo(pItem, info);
        //z value manage.
        pItem->setZValue(-2);
    }
}

void ZGraphicsLayout::updateHierarchy(QGraphicsItem* pItem)
{
    //find the parent layout of this item.
    if (!pItem) return;

    if (ZGraphicsLayout* pLayout = QVariantPtr<ZGraphicsLayout>::asPtr(pItem->data(GVKEY_PARENT_LAYOUT)))
    {
        ZGraphicsLayout::updateHierarchy(pLayout);
    }
    else if (ZGraphicsLayout* pOwnLayout = QVariantPtr<ZGraphicsLayout>::asPtr(pItem->data(GVKEY_OWNLAYOUT)))
    {
        ZGraphicsLayout::updateHierarchy(pOwnLayout);
    }
    else
    {
        zeno::log_warn("the layout cannot be update hierarchically");
    }
}

ZGraphicsLayout* ZGraphicsLayout::visitRoot(ZGraphicsLayout* currentLayout)
{
    ZGraphicsLayout* root = currentLayout, * tmp = root;
    while (tmp)
    {
        while (tmp->parentLayout())
            tmp = tmp->parentLayout();

        root = tmp;
        auto item = tmp->parentItem();
        tmp = (ZGraphicsLayout*)item->data(GVKEY_PARENT_LAYOUT).value<void*>();
    }
    return root;
}

void ZGraphicsLayout::clearCacheSize()
{
    for (auto item : m_items)
    {
        if (item->type == Type_Layout) {
            item->pLayout->clearCacheSize();
        }
        item->bDirty = true;
    }
}

QRectF ZGraphicsLayout::geometry() const
{
    return m_geometry;
}

void ZGraphicsLayout::calcItemsSize(QSizeF layoutSize)
{
    auto getSize = [this](const QSizeF& sz) {
        return m_bHorizontal ? sz.width() : sz.height();
    };
    auto setSize = [this](QSizeF& sz, qreal val) {
        if (m_bHorizontal)
            sz.setWidth(val);
        else
            sz.setHeight(val);
    };

    QVector<int> szs(m_items.size(), 0);
    std::set<int> fixedItems, expandingItems;
    layoutSize = layoutSize.shrunkBy(m_margins);
    qreal remaining = getSize(layoutSize);

    QSizeF szMargin(m_margins.left() + m_margins.right(), m_margins.top() + m_margins.bottom());
    remaining -= getSize(szMargin);

    for (int i = 0; i < m_items.size(); i++)
    {
        auto item = m_items[i];
        ZGraphicsLayout* pLayout = nullptr;
        if (Type_Layout == item->type)
        {
            pLayout = item->pLayout;
            ZASSERT_EXIT(pLayout);
        }
        else if (Type_Item == item->type)
        {
            ZASSERT_EXIT(item->pItem);
            pLayout = QVariantPtr<ZGraphicsLayout>::asPtr(item->pItem->data(GVKEY_OWNLAYOUT));
            if (!item->pItem->isVisible())
                continue;
        }

        if (pLayout)
        {
            qreal sz = 0;
            if (item->bDirty) {
                QSizeF _layoutSize = pLayout->calculateSize();
                if (m_bHorizontal) {
                    pLayout->calcItemsSize(QSizeF(_layoutSize.width(), layoutSize.height()));
                }
                else {
                    pLayout->calcItemsSize(QSizeF(layoutSize.width(), _layoutSize.height()));
                }
                sz = getSize(_layoutSize);
                if (m_bHorizontal)
                {
                    item->gvItemSz.minSize.setWidth(sz);
                    item->gvItemSz.minSize.setHeight(layoutSize.height());
                }
                else
                {
                    item->gvItemSz.minSize.setWidth(layoutSize.width());
                    item->gvItemSz.minSize.setHeight(sz);
                }
                item->bDirty = false;
            }
            else {
                sz = getSize(item->gvItemSz.minSize);
            }
            remaining -= sz;
            fixedItems.insert(i);
            szs[i] = sz;
        }
        else
        {
            QGraphicsItem* pItem = item->pItem;

            if (QGraphicsProxyWidget* pWidget = qgraphicsitem_cast<QGraphicsProxyWidget*>(pItem))
            {
                ZenoVecEditItem* pVecEdit = qobject_cast<ZenoVecEditItem*>(pWidget);
                if (pVecEdit)
                {
                    int j;
                    j = 0;
                }
            }

            QSizePolicy policy = pItem ? pItem->data(GVKEY_SIZEPOLICY).value<QSizePolicy>() : item->gvItemSz.policy;
            QSizeF sizeHint = pItem ? ZenoGvHelper::sizehintByPolicy(pItem) : item->gvItemSz.minSize;

            item->gvItemSz.minSize = sizeHint;

            if (m_bHorizontal) {
                if (policy.verticalPolicy() == QSizePolicy::Expanding) {
                    item->gvItemSz.minSize.setHeight(layoutSize.height());
                }
            }
            else {
                if (policy.horizontalPolicy() == QSizePolicy::Expanding) {
                    item->gvItemSz.minSize.setWidth(layoutSize.width());    //minus margins.
                }
            }


            if ((m_bHorizontal && policy.horizontalPolicy() == QSizePolicy::Expanding) ||
                (!m_bHorizontal && policy.verticalPolicy() == QSizePolicy::Expanding))
            {
                expandingItems.insert(i);
            }
            else
            {
                fixedItems.insert(i);
                qreal sz = getSize(sizeHint);
                remaining -= sz;
                setSize(item->gvItemSz.minSize, sz);
                item->bDirty = false;
                szs[i] = sz;
            }
        }
        if (i < m_items.size() - 1)
            remaining -= m_spacing;
    }
    //remaining space allocated for all expanding widget.
    if (!expandingItems.empty())
    {
        int n = expandingItems.size();
        qreal sz = remaining / n;
        for (int i : expandingItems)
        {
            szs[i] = sz;
            setSize(m_items[i]->gvItemSz.minSize, sz);
            m_items[i]->bDirty = false;
        }
    }
}

void ZGraphicsLayout::setup(QRectF rc)
{
    //set geometry relative to item which owns this layout, indicated by rc.
    m_geometry = rc;
    rc = rc.marginsRemoved(m_margins);
    qreal xPos = rc.topLeft().x(), yPos = rc.topLeft().y();
    //set geometry for each component.
    for (int i = 0; i < m_items.size(); i++)
    {
        auto item = m_items[i];
        switch (item->type)
        {
            case Type_Layout:
            case Type_Item:
            case Type_Spacing:
            {
                QSizeF sz = item->gvItemSz.minSize;
                QRectF _rc(QPointF(xPos, yPos), sz);

                if (item->type == Type_Layout)
                {
                    ZASSERT_EXIT(item->pLayout);
                    item->pLayout->setup(_rc);
                }
                else if (item->type == Type_Item)
                {
                    SizeInfo info;
                    info.minSize = sz;
                    info.pos.setX(xPos);
                    info.pos.setY(yPos);

                    if (item->alignment & Qt::AlignRight)
                    {
                        info.pos.setX(rc.right() - sz.width());
                    }

                    if (item->alignment & Qt::AlignVCenter)
                    {
                        info.pos.setY(rc.center().y() - sz.height() / 2);
                    }

                    ZenoGvHelper::setSizeInfo(item->pItem, info);

                    //item with a layout, setup it recursively.
                    ZGraphicsLayout* pLayout = QVariantPtr<ZGraphicsLayout>::asPtr(item->pItem->data(GVKEY_OWNLAYOUT));
                    if (pLayout) {
                        QRectF __rc(0, 0, _rc.width(), _rc.height());
                        pLayout->setup(__rc);
                    }
                }

                if (m_bHorizontal)
                    xPos += sz.width();
                else
                    yPos += sz.height();
                break;
            }
        }
        if (m_bHorizontal)
            xPos += m_spacing;
        else
            yPos += m_spacing;
    }
}