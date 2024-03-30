#pragma once

#include <zeno/core/IObject.h>
#include <zeno/core/INode.h>
#include <zeno/utils/PolymorphicMap.h>
#include <zeno/utils/api.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <set>
#include <functional>


namespace zeno {

    using SharedObjects = std::map<std::string, std::shared_ptr<zeno::IObject>>;

    struct RenderObjsInfo {
        SharedObjects newObjs;
        SharedObjects modifyObjs;
        std::set<std::string> remObjs;
        SharedObjects lightObjs;    //TODO:
        SharedObjects allObjects;

        bool empty() const {
            return newObjs.empty() && modifyObjs.empty() && remObjs.empty() && lightObjs.empty();
        }
    };


class ObjectManager
{
    struct _ObjInfo {
        std::shared_ptr<IObject> obj;
        std::set<ObjPath> attach_nodes;
        //ObjPath view_node;
    };

    using ViewObjects = std::map<std::string, _ObjInfo>;
    

    enum CacheType {
        MemoryCache,
        DiskCache
    };

    struct FrameData {
        std::optional<ViewObjects> view_objs;
        FILE* m_file = nullptr;
        CacheType cache;
    };

public:
     ObjectManager();
    ~ObjectManager();

    ZENO_API void beforeRun();
    ZENO_API void afterRun();

    ZENO_API void collectingObject(const std::string& id, std::shared_ptr<IObject> obj, std::shared_ptr<INode> view_node, bool bView);
    CALLBACK_REGIST(collectingObject, void, std::shared_ptr<IObject>, bool)

    ZENO_API void removeObject(const std::string& id);
    CALLBACK_REGIST(removeObject, void, std::string)

    ZENO_API void notifyTransfer(std::shared_ptr<IObject> obj);
    CALLBACK_REGIST(notifyTransfer, void, std::shared_ptr<IObject>)

    ZENO_API void viewObject(std::shared_ptr<IObject> obj, bool bView);
    CALLBACK_REGIST(viewObject, void, std::shared_ptr<IObject>, bool)

    ZENO_API int registerObjId(const std::string& objprefix);

    ZENO_API std::set<ObjPath> getAttachNodes(const std::string& id);

    ZENO_API void commit();
    ZENO_API void revert();

    ZENO_API void export_loading_objs(RenderObjsInfo& info);
    ZENO_API void clear_last_run();
    ZENO_API void collect_removing_objs(const std::string& objkey);
    ZENO_API void remove_attach_node_by_removing_objs();

private:
    void clear();

    std::map<std::string, int> m_objRegister;

    ViewObjects m_objects;  //��¼���е�ǰ����Ķ��󣬵��л�֡��ʱ�򣬿���ʹ�ò�������֡�Ķ������㡣
    std::map<int, FrameData> m_frameData;   //��¼������ص�֡����

    std::set<std::string> m_viewObjs;
    std::set<std::string> m_lastViewObjs;
    std::set<std::string> m_removing_objs;  //������ɾ���ڵ�ʱ��¼��Ҫɾ����obj��Ҫ����rollback�����

    std::set<std::string> m_newAdded;
    std::set<std::string> m_modify;
    std::set<std::string> m_remove;

    mutable std::mutex m_mtx;
};

}