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

struct ILink {
    std::weak_ptr<IParam> fromparam;
    std::weak_ptr<IParam> toparam;
    std::string fromkey;    //for dict/list 对于list来说，keyName好像不合适，不过ILink本来就存在于links里面，已经是列表储存了。
    std::string tokey;
    LinkFunction lnkProp = Link_Copy;
};

struct IParam {
    std::string name;

    std::weak_ptr<INode> m_wpNode;
    std::list<std::shared_ptr<ILink>> links;

    zvariant defl;
    zany result;

    ParamControl control = NullControl;
    ParamType type = Param_Null;
    SocketType socketType = NoSocket;
    std::optional<ControlProperty> optCtrlprops;
    bool isLegacy = false;      //TODO:

    bool m_idModify = false;    //该output param输出的obj是新创建的(false)还是基于已有的修改(true)
};

}