#ifndef __PARAM_MODEL_H__
#define __PARAM_MODEL_H__

#include "modeldata.h"
#include "modelrole.h"

class IGraphsModel;

class IParamModel : public QAbstractItemModel
{
    Q_OBJECT
    typedef QAbstractItemModel _base;

public:
    struct _ItemInfo
    {
        QString name;
        QString type;
        QVariant pConst;    //const (default) value on socket or param.
        //CurveModel* pVar;   //variable on time frame.
        PARAM_CONTROL ctrl;
        PARAM_LINKS links;
    };

    explicit IParamModel(
        PARAM_CLASS paramType,
        IGraphsModel* pModel,
        const QPersistentModelIndex& subgIdx,
        const QPersistentModelIndex& nodeIdx,
        QObject* parent = nullptr);
    ~IParamModel();

    bool getInputSockets(INPUT_SOCKETS& inputs);
    bool getOutputSockets(OUTPUT_SOCKETS& outputs);
    bool getParams(PARAMS_INFO& params);
    void setInputSockets(const INPUT_SOCKETS& inputs);
    void setParams(const PARAMS_INFO& params);
    void setOutputSockets(const OUTPUT_SOCKETS& outputs);

    //QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QModelIndexList match(const QModelIndex& start, int role,
        const QVariant& value, int hits = 1,
        Qt::MatchFlags flags =
        Qt::MatchFlags(Qt::MatchStartsWith | Qt::MatchWrap)) const override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;

    //ParamModel
    void insertRow(int row,
        const QString& sockName,
        const QString& type = "",
        const QVariant& deflValue = QVariant(),
        PARAM_CONTROL ctrl = CONTROL_NONE,
        const PARAM_LINKS& links = PARAM_LINKS());

    void appendRow(
        const QString& sockName,
        const QString& type = "",
        const QVariant& deflValue = QVariant(),
        PARAM_CONTROL ctrl = CONTROL_NONE,
        const PARAM_LINKS& links = PARAM_LINKS());

    void setItem(
        const QModelIndex& idx,
        const QString& type,
        const QVariant& deflValue,
        PARAM_CONTROL ctrl,
        const PARAM_LINKS& links = PARAM_LINKS());

    bool addLinkToParam(const QString& sockName, const QModelIndex& linkIdx);

    bool removeLink(const QString& sockName, const QModelIndex& linkIdx);

    QStringList sockNames() const;

    QModelIndex index(const QString& name) const;

    PARAM_CLASS paramClass() const;

    void clear();

signals:
    void mock_dataChanged(const QModelIndex& idx, const QVariant& oldValue, int role);

private:
    QString nameFromRow(int row) const;
    bool _insertRow(int row,
        const QString& name,
        const QString& type = "",
        const QVariant& deflValue = QVariant(),
        PARAM_CONTROL ctrl = CONTROL_NONE,
        const PARAM_LINKS& links = PARAM_LINKS());
    bool _removeRow(const QModelIndex& index);
    void onSubIOEdited(const QVariant& value, const _ItemInfo& item);

    const QPersistentModelIndex m_nodeIdx;
    const QPersistentModelIndex m_subgIdx;

    const PARAM_CLASS m_class;
    QMap<QString, int> m_key2Row;
    QMap<int, QString> m_row2Key;
    QMap<QString, _ItemInfo> m_items;
    IGraphsModel* m_model;
};

#endif