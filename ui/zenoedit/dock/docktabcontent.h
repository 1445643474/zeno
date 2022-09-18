#ifndef __DOCKTAB_CONTENT_H__
#define __DOCKTAB_CONTENT_H__

#include <QtWidgets>
#include <unordered_set>
#include <zenoui/comctrl/ztoolbutton.h>

class ZIconToolButton;

class ZToolBarButton : public ZToolButton
{
    Q_OBJECT
public:
    ZToolBarButton(bool bCheckable, const QString& icon, const QString& iconOn);
};


class DockContent_Parameter : public QWidget
{
    Q_OBJECT
public:
    explicit DockContent_Parameter(QWidget* parent = nullptr);
    void onNodesSelected(const QModelIndex& subgIdx, const QModelIndexList& nodes, bool select);
    void onPrimitiveSelected(const std::unordered_set<std::string>& primids);

private:
    QLabel* m_plblName;
    QLineEdit* m_pLineEdit;
};

class DockContent_Editor : public QWidget
{
    Q_OBJECT
public:
    explicit DockContent_Editor(QWidget* parent = nullptr);

private:

};

class DockContent_View : public QWidget
{
    Q_OBJECT
public:
    explicit DockContent_View(QWidget* parent = nullptr);
};



#endif