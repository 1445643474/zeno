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
    std::string keyName;    //for dict/list ����list��˵��keyName���񲻺��ʣ�����ILink�����ʹ�����links���棬�Ѿ����б����ˡ�
};

struct IParam {
    std::string name;
    ParamType type;
    std::weak_ptr<INode> m_spNode;
    std::vector<std::shared_ptr<ILink>> links;

    zvariant defl;
    zany result;

    //ZENO_API bool update_defl(const zvariant& var);
    //ZENO_API bool update_type(const ParamType type);
};

}