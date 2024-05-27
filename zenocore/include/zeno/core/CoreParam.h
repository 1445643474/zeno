#pragma once

#include <zeno/utils/api.h>
#include <zeno/core/IObject.h>
#include <zeno/core/INode.h>
#include <zeno/core/common.h>
#include <zeno/utils/safe_dynamic_cast.h>
#include <zeno/funcs/LiterialConverter.h>
#include <variant>
#include <memory>
#include <string>
#include <set>
#include <map>
#include <optional>
#include <zeno/types/CurveObject.h>
#include <zeno/extra/GlobalState.h>

namespace zeno {

struct ObjectLink {
    ObjectParam* fromparam = nullptr;  //IParam stored as unique ptr in the INode, so we need no smart pointer.
    ObjectParam* toparam = nullptr;
    std::string fromkey;    //for dict/list ����list��˵��keyName���񲻺��ʣ�����ILink�����ʹ�����links���棬�Ѿ����б����ˡ�
    std::string tokey;
};


struct PrimitiveLink {
    PrimitiveParam* fromparam = nullptr;
    PrimitiveParam* toparam = nullptr;
};

struct CoreParam {
    std::string name;
    std::weak_ptr<INode> m_wpNode;
    
    ParamType type = Param_Null;
    SocketType socketType = NoSocket;
    bool bInput = true;
    bool m_idModify = false;    //��output param�����obj���´�����(false)���ǻ������е��޸�(true)
};

struct ObjectParam : CoreParam {
    std::list<std::shared_ptr<ObjectLink>> links;
    zany spObject;

    ParamObject export() {
        ParamObject objparam;
        objparam.name = name;
        objparam.bInput = bInput;
        objparam.type = type;
        objparam.socketType = socketType;
        for (auto spLink : links) {
            objparam.links.push_back(getEdgeInfo(spLink));
        }
        return objparam;
    }
};

struct PrimitiveParam : CoreParam {
    zvariant defl;
    zvariant result;
    std::list<std::shared_ptr<PrimitiveLink>> links;
    ParamControl control = NullControl;
    std::optional<ControlProperty> optCtrlprops;

    ParamPrimitive export() const {
        ParamPrimitive param;
        param.name = name;
        param.bInput = bInput;
        param.type = type;
        param.socketType = socketType;
        param.defl = defl;
        param.control = control;
        param.ctrlProps = optCtrlprops;
        for (auto spLink : links) {
            param.links.push_back(getEdgeInfo(spLink));
        }
        return param;
    }
};

}