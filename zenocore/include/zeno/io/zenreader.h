#ifndef __ZEN_READER_H__
#define __ZEN_READER_H__

#include "zsgreader.h"
#include <zeno/core/Assets.h>

namespace zenoio
{
    class ZenReader : public ZsgReader
    {
    public:
        ZENO_API ZenReader();

    protected:
        bool _parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret) override;

        zeno::ParamInfo _parseSocket(
            const bool bInput,
            const std::string& id,
            const std::string& nodeCls,
            const std::string& inSock,
            const rapidjson::Value& sockObj,
            zeno::LinksData& links) override;

        bool _parseGraph(
            const rapidjson::Value& graph,
            const zeno::AssetsData& assets,
            zeno::GraphData& subgData);

        zeno::NodeData _parseNode(
            const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
            const std::string& nodeid,
            const rapidjson::Value& nodeObj,
            const zeno::AssetsData& subgraphDatas,
            zeno::LinksData& links);    //��parse�ڵ��ʱ��˳���ѽڵ��ϵı���ϢҲ�����¼������


    };
}

#endif