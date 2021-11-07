#ifndef QDMLISTVIEWNODEMENU_H
#define QDMLISTVIEWNODEMENU_H

#include <zeno/common.h>
#include <QListView>
#include <QStandardItem>
#include <memory>
#include <vector>

ZENO_NAMESPACE_BEGIN

class QDMListViewNodeMenu : public QListView
{
    Q_OBJECT

    std::vector<std::unique_ptr<QStandardItem>> items;

public:
    explicit QDMListViewNodeMenu(QWidget *parent = nullptr);
    ~QDMListViewNodeMenu();

signals:
    void entryClicked(QString name);
};

ZENO_NAMESPACE_END

#endif // QDMLISTVIEWNODEMENU_H
