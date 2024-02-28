#ifndef __ZSG2_READER_H__
#define __ZSG2_READER_H__

#include "zsgreader.h"

namespace zenoio
{
    class Zsg2Reader : public ZsgReader 
    {
    public:
        ZENO_API Zsg2Reader();

    protected:
        bool _parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret) override;

        zeno::ParamInfo _parseSocket(
            const bool bInput,
            const std::string& id,
            const std::string& nodeCls,
            const std::string& inSock,
            const rapidjson::Value& sockObj,
            zeno::LinksData& links) override;

    private:
        bool _parseSubGraph(
                const std::string& graphPath,   //���� "/main"  "/main/aaa"
                const rapidjson::Value &subgraph,
                const std::map<std::string, zeno::GraphData>& subgraphDatas,
                zeno::GraphData& subgData);

        zeno::NodeData _parseNode(
                const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
                const std::string& nodeid,
                const rapidjson::Value& nodeObj,
                const std::map<std::string, zeno::GraphData>& subgraphDatas,
                zeno::LinksData& links);    //��parse�ڵ��ʱ��˳���ѽڵ��ϵı���ϢҲ�����¼������

        bool _parseParams(
                const std::string& id,
                const std::string& nodeCls,
                const rapidjson::Value& jsonParams,
                zeno::NodeData& ret);

        void _parseDictPanel(
                bool bInput,
                const rapidjson::Value& dictPanelObj,
                const std::string& id,
                const std::string& inSock,
                const std::string& nodeName,
                zeno::LinksData& links);
    };
}

#endif
