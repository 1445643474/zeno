#include "zenreader.h"


namespace zenoio
{
    ZenReader::ZenReader()
    {
    }

    bool ZenReader::_parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret)
    {
        return false;
    }

    zeno::NodeData ZenReader::_parseNode(
        const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
        const std::string& nodeid,
        const rapidjson::Value& nodeObj,
        const std::map<std::string, zeno::GraphData>& subgraphDatas,
        zeno::LinksData& links)
    {
        zeno::NodeData node;
        return node;
    }

    zeno::ParamInfo ZenReader::_parseSocket(
        const bool bInput,
        const std::string& id,
        const std::string& nodeCls,
        const std::string& inSock,
        const rapidjson::Value& sockObj,
        zeno::LinksData& links)
    {
        zeno::ParamInfo param;
        return param;
    }
}