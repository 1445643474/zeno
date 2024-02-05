#ifndef __ZEN_READER_H__
#define __ZEN_READER_H__

#include "zsgreader.h"

namespace zenoio
{
    class ZenReader : public ZsgReader
    {
    public:
        ZenReader();

    protected:
        bool _parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret) override;

        zeno::NodeData _parseNode(
            const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
            const std::string& nodeid,
            const rapidjson::Value& nodeObj,
            const std::map<std::string, zeno::GraphData>& subgraphDatas,
            zeno::LinksData& links) override;    //��parse�ڵ��ʱ��˳���ѽڵ��ϵı���ϢҲ�����¼������

        zeno::ParamInfo _parseSocket(
            const bool bInput,
            const std::string& id,
            const std::string& nodeCls,
            const std::string& inSock,
            const rapidjson::Value& sockObj,
            zeno::LinksData& links) override;
    };
}

#endif