#pragma once

#include <zeno/core/IObject.h>
#include <functional>
#include <memory>
#include <map>

namespace zeno {

struct FunctionObject : IObjectClone<FunctionObject> {
    using DictType = std::map<std::string, std::shared_ptr<IObject>>;
    std::function<DictType(DictType const &)> func;

    inline DictType call(DictType const &args) {
        return func(args);
    }
};

}
