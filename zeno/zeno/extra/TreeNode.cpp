#include <zeno/zeno.h>
#include <zeno/extra/TreeNode.h>
#include <zeno/types/TreeObject.h>
#include <zeno/types/NumericObject.h>
#include <sstream>
#include <cassert>

namespace zeno {

ZENO_API TreeNode::TreeNode() = default;
ZENO_API TreeNode::~TreeNode() = default;

ZENO_API void TreeNode::apply() {
    auto tree = std::make_shared<TreeObject>(this);
    set_output("out", std::move(tree));
}

ZENO_API std::string EmissionPass::finalizeCode() {
    auto defs = collectDefs();
    for (auto const &var: variables) {
        var.node->emitCode(this);
    }
    auto code = collectCode();
    code = defs + code;
    return {code};
}

ZENO_API int EmissionPass::determineType(IObject *object) {
    if (auto num = dynamic_cast<NumericObject *>(object)) {
        return std::visit([&] (auto const &value) -> int {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<float, T>) {
                return 1;
            } else if constexpr (std::is_same_v<vec2f, T>) {
                return 2;
            } else if constexpr (std::is_same_v<vec3f, T>) {
                return 3;
            } else if constexpr (std::is_same_v<vec4f, T>) {
                return 4;
            } else {
                throw zeno::Exception("bad numeric object type: " + (std::string)typeid(T).name());
            }
        }, num->value);

    } else if (auto tree = dynamic_cast<TreeObject *>(object)) {
        assert(tree->node);
        if (auto it = varmap.find(tree->node); it != varmap.end())
            return variables.at(it->second).type;
        int type = tree->node->determineType(this);
        varmap[tree->node] = variables.size();
        variables.push_back(VarInfo{type, tree->node});
        return type;

    } else {
        throw zeno::Exception("bad tree object type: " + (std::string)typeid(*object).name());
    }
}

ZENO_API int EmissionPass::currentType(TreeNode *node) const {
    return variables[varmap.at(node)].type;
}

ZENO_API std::string EmissionPass::determineExpr(IObject *object, TreeNode *node) const {
    return typeNameOf(currentType(node)) + "(" + determineExpr(object) + ")";
}

ZENO_API std::string EmissionPass::addCommonFunc(EmissionPass::CommonFunc const &comm) {
    int idx = commons.size();
    commons.push_back(comm);
    return "fun" + std::to_string(idx);
}

ZENO_API std::string EmissionPass::getCommonCode() const {
    std::string ret;
    for (int i = 0; i < commons.size(); i++) {
        auto funcname = "fun" + std::to_string(i);
        ret += typeNameOf(commons[i].rettype) + " " + funcname + commons[i].code + "\n";
    }
    return ret;
}

ZENO_API std::string EmissionPass::typeNameOf(int type) const {
    if (type == 1) return "float";
    else return (backend == HLSL ? "float" : "vec") + std::to_string(type);
}

ZENO_API std::string EmissionPass::collectDefs() const {
    std::string res;
    int cnt = 0;
    for (auto const &var: variables) {
        res += typeNameOf(var.type) + " tmp" + std::to_string(cnt) + ";\n";
        cnt++;
    }
    return res;
}

ZENO_API std::string EmissionPass::collectCode() const {
    std::string res;
    for (auto const &line: lines) {
        res += line + "\n";
    }
    return res;
}

static std::string ftos(float x) {
    std::ostringstream ss;
    ss << x;
    return ss.str();
}

ZENO_API std::string EmissionPass::determineExpr(IObject *object) const {
    if (auto num = dynamic_cast<NumericObject *>(object)) {
        return std::visit([&] (auto const &value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<float, T>) {
                return typeNameOf(1) + "(" + ftos(value) + ")";
            } else if constexpr (std::is_same_v<vec2f, T>) {
                return typeNameOf(2) + "(" + ftos(value[0]) + ", " + ftos(value[1]) + ")";
            } else if constexpr (std::is_same_v<vec3f, T>) {
                return typeNameOf(3) + "(" + ftos(value[0]) + ", " + ftos(value[1]) + ", "
                    + ftos(value[2]) + ")";
            } else if constexpr (std::is_same_v<vec4f, T>) {
                return typeNameOf(4) + "(" + ftos(value[0]) + ", " + ftos(value[1]) + ", "
                    + ftos(value[2]) + ", " + ftos(value[3]) + ")";
            } else {
                throw zeno::Exception("bad numeric object type: " + (std::string)typeid(T).name());
            }
        }, num->value);

    } else if (auto tree = dynamic_cast<TreeObject *>(object)) {
        return "tmp" + std::to_string(varmap.at(tree->node));
    }
}

ZENO_API void EmissionPass::emitCode(std::string const &line) {
    int idx = lines.size();
    lines.push_back("tmp" + std::to_string(idx) + " = " + line + ";");
}

ZENO_API std::string EmissionPass::finalizeCode(std::vector<std::pair<int, std::string>> const &keys,
                                                std::vector<std::shared_ptr<IObject>> const &vals) {
    std::vector<int> vartypes;
    vartypes.reserve(keys.size());
    for (int i = 0; i < keys.size(); i++) {
        int their_type = determineType(vals[i].get());
        int our_type = keys[i].first;
        if (their_type != our_type && their_type != 1)
            throw zeno::Exception("unexpected input for " + keys[i].second + " which requires "
                                  + typeNameOf(our_type) + " but got " + typeNameOf(their_type));
        vartypes.push_back(their_type);
    }
    auto code = finalizeCode();
    for (int i = 0; i < keys.size(); i++) {
        auto type = vartypes[i];
        auto expr = determineExpr(vals[i].get());
        int our_type = keys[i].first;
        code += typeNameOf(our_type) + " " + keys[i].second + " = " + typeNameOf(our_type) + "(" + expr + ");\n";
    }
    return code;
}

}
