//
// Created by zhouhang on 2022/8/22.
//

#ifndef ZENO_ZLIGHTSMODEL_H
#define ZENO_ZLIGHTSMODEL_H

#include <QAbstractListModel  >

class ZLightsModel: public QAbstractListModel   {
    int rowCount(const QModelIndex &parent) const override;
    QVariant data(const QModelIndex &index, int role) const override;
};


#endif //ZENO_ZLIGHTSMODEL_H
