#ifndef __CORE_REFERMANAGER_H__
#define __CORE_REFERMANAGER_H__

#include <map>
#include <set>
#include <string>
#include <memory>
#include "common.h"
#include <zeno/utils/api.h>

namespace zeno {
    struct INode;
    struct IParam;
    struct Graph;

    struct ReferManager
    {
    public:
        ReferManager();
        ~ReferManager();
        ZENO_API void init(const std::shared_ptr<Graph>& pGraph);
        void addReferInfo(std::shared_ptr <IParam> spParam);
        //��ɾ�������������������Ľڵ����ɾ����Ӧ��Ϣ
        void removeReferParam(const std::string& uuid_param);
        //��ɾ���˱����õĽڵ����ɾ����Ӧ��Ϣ
        void removeBeReferedParam(const std::string& uuid_param, const std::string& path);
        //�������õĽڵ������޸ĺ���Ҫ��������
        void updateReferParam(const std::string& oldPath, const std::string& newPath);
        //�����õĲ����޸ĺ���Ҫ����m_referedUuidParams
        void updateBeReferedParam(const std::string& uuid_param);
        //�����õĲ�������ʱ��Ҫ�����õĽڵ����
        void updateDirty(const std::string& uuid_param);

        bool isRefered(const std::string& key) const;//�Ƿ�������������
        bool isBeRefered(const std::string& key) const;//�����Ƿ�����
        bool isReferSelf(const std::string& key) const;//�Ƿ�ѭ������

    private:
        std::set<std::string> referPaths(const std::string& currPath, const zvariant& val) const;
        bool updateParamValue(const std::string& oldVal, const std::string& newVal, const std::string& currentPath, zvariant& arg);

        std::map <std::string, std::shared_ptr<IParam>> m_referParams;//<���ò���uuid/param, ����ptr>
        std::map <std::string, std::set<std::string>> m_referedUuidParams;//<�����ò���uuid/param, ���ò���uuid/param ����>
        bool m_bModify;

    };
}
#endif