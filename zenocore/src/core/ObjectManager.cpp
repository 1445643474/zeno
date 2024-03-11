#include <zeno/core/ObjectManager.h>
#include <zeno/core/Graph.h>


namespace zeno {

    ObjectManager::ObjectManager()
    {
    }

    ObjectManager::~ObjectManager()
    {
    }

    ZENO_API void ObjectManager::commit()
    {
    }

    ZENO_API void ObjectManager::revert()
    {
    }

    ZENO_API void ObjectManager::collectingObject(const std::string& id, std::shared_ptr<IObject> obj, std::shared_ptr<INode> view_node, bool bView)
    {
        std::lock_guard lck(m_mtx);

        zeno::getSession().globalState->setCalcObjStatus(zeno::Collecting);

        auto it = m_collecting.find(id);
        auto path = view_node->get_path();
        if (it == m_collecting.end()) {
            _ObjInfo info;
            info.obj = obj;
            info.attach_nodes.insert(path);
            m_collecting.insert(std::make_pair(id, info));
            m_newAdded.insert(id);
        }
        else {
            it->second.obj = obj;
            it->second.attach_nodes.insert(path);
        }
        if (bView) {
            m_viewObjs.insert(id);
            m_lastViewObjs.erase(id);   //上一次运行有view，这一次也有view
        }
        else {
        }
        //CALLBACK_NOTIFY(collectingObject, obj, bView)
    }

    ZENO_API void ObjectManager::removeObject(const std::string& id)
    {
        std::lock_guard lck(m_mtx);
        if (m_collecting.find(id) != m_collecting.end()) {
            m_collecting.erase(id);
            //CALLBACK_NOTIFY(removeObject, id)
        }
    }

    ZENO_API void ObjectManager::notifyTransfer(std::shared_ptr<IObject> obj)
    {
        std::lock_guard lck(m_mtx);
        //CALLBACK_NOTIFY(notifyTransfer, obj)
    }

    ZENO_API void ObjectManager::viewObject(std::shared_ptr<IObject> obj, bool bView)
    {
        std::lock_guard lck(m_mtx);
        //CALLBACK_NOTIFY(viewObject, obj, bView)
    }

    ZENO_API int ObjectManager::registerObjId(const std::string& objprefix)
    {
        if (m_objRegister.find(objprefix) == m_objRegister.end()) {
            m_objRegister.insert(std::make_pair(objprefix, 0));
            m_objRegister[objprefix]++;
            return 0;
        }
        else {
            int newObjId = m_objRegister[objprefix]++;
            return newObjId;
        }
    }

    ZENO_API std::set<ObjPath> ObjectManager::getAttachNodes(const std::string& id)
    {
        auto it = m_collecting.find(id);
        if (it != m_collecting.end())
        {
            return it->second.attach_nodes;
        }
        return std::set<ObjPath>();
    }

    ZENO_API void ObjectManager::beforeRun()
    {
        std::lock_guard lck(m_mtx);     //可能此时渲染端在load_objects
        m_lastViewObjs = m_viewObjs;
        m_viewObjs.clear();
        m_newAdded.clear();
        m_modify.clear();
        m_remove.clear();
    }

    ZENO_API void ObjectManager::afterRun()
    {
        std::lock_guard lck(m_mtx);
        //剩下来的都是上一次view，而这一次没有view的。
        for (auto objkey : m_lastViewObjs)
        {
            //removeObject(objkey);
        }
        m_remove = m_lastViewObjs;
        m_lastViewObjs.clear();
    }

    ZENO_API void ObjectManager::clear_last_run()
    {
        std::lock_guard lck(m_mtx);
        m_newAdded.clear();
        m_modify.clear();
        m_remove.clear();
    }

    ZENO_API void ObjectManager::collect_removing_objs(const std::string& objkey)
    {
        m_removing_objs.insert(objkey);
    }

    ZENO_API void ObjectManager::remove_attach_node_by_removing_objs()
    {
        for (auto obj_key : m_removing_objs) {
            auto nodes = getAttachNodes(obj_key);
            for (auto node_path : nodes) {
                auto spNode = zeno::getSession().mainGraph->getNode(node_path);
                if (spNode)
                    spNode->mark_dirty(true);
            }
            removeObject(obj_key);
        }
    }

    ZENO_API void ObjectManager::export_loading_objs(RenderObjsInfo& info)
    {
        std::lock_guard lck(m_mtx);
        for (auto objkey : m_newAdded) {
            auto it = m_collecting.find(objkey);
            if (it != m_collecting.end()) {
                info.newObjs.insert(std::make_pair(objkey, it->second.obj));
            }
        }
        for (auto objkey : m_modify) {
            auto it = m_collecting.find(objkey);
            if (it != m_collecting.end()) {
                info.modifyObjs.insert(std::make_pair(objkey, it->second.obj));
            }
        }
        for (auto objkey : m_remove) {
            auto it = m_collecting.find(objkey);
            if (it != m_collecting.end()) {
                info.remObjs.insert(std::make_pair(objkey, it->second.obj));
            }
        }
    }

    void ObjectManager::clear()
    {
        //todo
    }

}