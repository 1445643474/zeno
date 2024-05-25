#include <zeno/core/INode.h>
#include <zeno/core/Graph.h>
#include <zeno/core/Descriptor.h>
#include <zeno/core/Session.h>
#include <zeno/core/Assets.h>
#include <zeno/core/ObjectManager.h>
#include <zeno/types/DummyObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/StringObject.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/extra/DirtyChecker.h>
#include <zeno/extra/TempNode.h>
#include <zeno/utils/Error.h>
#include <zeno/utils/string.h>
#include <zeno/funcs/ParseObjectFromUi.h>
#ifdef ZENO_BENCHMARKING
#include <zeno/utils/Timer.h>
#endif
#include <zeno/utils/safe_at.h>
#include <zeno/utils/logger.h>
#include <zeno/utils/uuid.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/core/CoreParam.h>
#include <zeno/DictObject.h>
#include <zeno/ListObject.h>
#include <zeno/utils/helper.h>
#include <zeno/utils/uuid.h>
#include <zeno/extra/SubnetNode.h>
#include <zeno/extra/GraphException.h>
#include <zeno/formula/formula.h>
#include <zeno/core/ReferManager.h>


namespace zeno {

ZENO_API INode::INode() {}

void INode::initUuid(std::shared_ptr<Graph> pGraph, const std::string nodecls) {
    m_nodecls = nodecls;
    this->graph = pGraph;

    m_uuid = generateUUID(nodecls);
    ObjPath path;
    path += m_uuid;
    while (pGraph) {
        const std::string name = pGraph->getName();
        if (name == "main") {
            break;
        }
        else {
            if (!pGraph->optParentSubgNode.has_value())
                break;
            auto pSubnetNode = pGraph->optParentSubgNode.value();
            assert(pSubnetNode);
            path = (pSubnetNode->m_uuid) + "/" + path;
            pGraph = pSubnetNode->graph.lock();
        }
    }
    m_uuidPath = path;
}

ZENO_API INode::~INode() = default;

ZENO_API std::shared_ptr<Graph> INode::getThisGraph() const {
    return graph.lock();
}

ZENO_API Session *INode::getThisSession() const {
    return &getSession();
}

ZENO_API GlobalState *INode::getGlobalState() const {
    return getSession().globalState.get();
}

ZENO_API void INode::doComplete() {
    set_output("DST", std::make_shared<DummyObject>());
    complete();
}

ZENO_API std::string INode::get_nodecls() const
{
    return m_nodecls;
}

ZENO_API std::string INode::get_ident() const
{
    return m_name;
}

ZENO_API std::string INode::get_show_name() const {
    if (nodeClass) {
        std::string dispName = nodeClass->m_customui.nickname;
        if (!dispName.empty())
            return dispName;
    }
    return m_nodecls;
}

ZENO_API std::string INode::get_show_icon() const {
    if (nodeClass) {
        return nodeClass->m_customui.iconResPath;
    }
    else {
        return "";
    }
}

ZENO_API CustomUI INode::get_customui() const {
    if (nodeClass) {
        return nodeClass->m_customui;
    }
    else {
        return CustomUI();
    }
}

ZENO_API ObjPath INode::get_path() const {
    ObjPath path;
    path = m_name;

    std::shared_ptr<Graph> pGraph = graph.lock();

    while (pGraph) {
        const std::string name = pGraph->getName();
        if (name == "main") {
            path = "/main" + path;
            break;
        }
        else {
            if (!pGraph->optParentSubgNode.has_value())
                break;
            auto pSubnetNode = pGraph->optParentSubgNode.value();
            assert(pSubnetNode);
            path = pSubnetNode->m_name + "/" + path;
            pGraph = pSubnetNode->graph.lock();
        }
    }
    return path;
}

ZENO_API std::string zeno::INode::get_path_str() const
{
    ObjPath path = get_path();
    std::stringstream ss;
    auto p = path.begin(), end = path.end();
    if (p != end)
        ss << *p++;
    for (; p != end; ++p) {
        ss << '/' << *p;
    }
    return ss.str();
    /*std::string path = m_name;
    std::shared_ptr<Graph> pGraph = graph.lock();
    if (!pGraph)
        return path;
    else
        path = pGraph->getName() + "/" + path;

    while (pGraph->optParentSubgNode.has_value())
    {
        auto pSubnetNode = pGraph->optParentSubgNode.value();
        assert(pSubnetNode);
        path = pSubnetNode->m_name+ "/" + path;
        pGraph = pSubnetNode->graph.lock();
    }
    return path;*/
}

std::string INode::get_uuid() const
{
    return m_uuid;
}

ZENO_API std::string INode::get_name() const
{
    return m_name;
}

ZENO_API void INode::set_name(const std::string& customname)
{
    m_name = customname;
}

ZENO_API void INode::set_view(bool bOn)
{
    CORE_API_BATCH

    m_bView = bOn;
    CALLBACK_NOTIFY(set_view, m_bView)

    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    spGraph->viewNodeUpdated(m_name, bOn);
}

ZENO_API bool INode::is_view() const
{
    return m_bView;
}

void INode::reportStatus(bool bDirty, NodeRunStatus status) {
    m_status = status;
    m_dirty = bDirty;
    zeno::getSession().reportNodeStatus(m_uuidPath, bDirty, status);
}

void INode::mark_previous_ref_dirty() {
    mark_dirty(true);
    //不仅要自身标脏，如果前面的节点是以引用的方式连接，说明前面的节点都可能被污染了，所有都要标脏。
    //TODO: 由端口而不是边控制。
    /*
    for (const auto& [name, param] : m_inputs) {
        for (const auto& link : param->links) {
            if (link->lnkProp == Link_Ref) {
                auto spOutParam = link->fromparam.lock();
                auto spPreviusNode = spOutParam->m_wpNode.lock();
                spPreviusNode->mark_previous_ref_dirty();
            }
        }
    }
    */
}

void INode::onInterrupted() {
    mark_dirty(true);
    mark_previous_ref_dirty();
}

ZENO_API void INode::mark_dirty(bool bOn, bool bWholeSubnet)
{
    scope_exit sp([&] {
        m_status = Node_DirtyReadyToRun;  //修改了数据，标脏，并置为此状态。（后续在计算过程中不允许修改数据，所以markDirty理论上是前端驱动）
        reportStatus(m_dirty, m_status);
    });

    if (m_dirty == bOn)
        return;

    m_dirty = bOn;
    if (m_dirty) {
        for (auto& [name, param] : m_outputObjs) {
            for (auto link : param->links) {
                auto inParam = link->toparam;
                assert(inParam);
                if (inParam) {
                    auto inNode = inParam->m_wpNode.lock();
                    assert(inNode);
                    inNode->mark_dirty(m_dirty);
                }
            }
        }
        for (auto& [name, param] : m_outputPrims) {
            for (auto link : param->links) {
                auto inParam = link->toparam;
                assert(inParam);
                if (inParam) {
                    auto inNode = inParam->m_wpNode.lock();
                    assert(inNode);
                    inNode->mark_dirty(m_dirty);
                }
            }
        }
    }

    if (SubnetNode* pSubnetNode = dynamic_cast<SubnetNode*>(this))
    {
        if (bWholeSubnet)
            pSubnetNode->mark_subnetdirty(bOn);
    }

    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    if (spGraph->optParentSubgNode.has_value())
    {
        spGraph->optParentSubgNode.value()->mark_dirty(true, false);
    }
}

void INode::mark_dirty_objs()
{
    for (auto const& [name, param] : m_outputObjs)
    {
        if (param->spObject) {
            if (param->spObject->key().empty()) {
                continue;
            }
            getSession().objsMan->collect_removing_objs(param->spObject->key());
        }
    }
}

ZENO_API void INode::complete() {}

ZENO_API void INode::preApply() {
    if (!m_dirty)
        return;

    reportStatus(true, Node_Pending);

    //TODO: the param order should be arranged by the descriptors.
    for (const auto& [name, param] : m_inputObjs) {
        bool ret = requireInput(name);
        if (!ret)
            zeno::log_warn("the param {} may not be initialized", name);
    }
    for (const auto& [name, param] : m_inputPrims) {
        bool ret = requireInput(name);
        if (!ret)
            zeno::log_warn("the param {} may not be initialized", name);
    }
}

ZENO_API void INode::registerObjToManager()
{
    for (auto const& [name, param] : m_outputObjs)
    {
        if (param->spObject)
        {
            if (std::dynamic_pointer_cast<NumericObject>(param->spObject) ||
                std::dynamic_pointer_cast<StringObject>(param->spObject)) {
                return;
            }

            if (param->spObject->key().empty())
            {
                //如果当前节点是引用前继节点产生的obj，则obj.key不为空，此时就必须沿用之前的id，
                //以表示“引用”，否则如果新建id，obj指针可能是同一个，会在manager引起混乱。
                param->spObject->update_key(m_uuid);
            }

            const std::string& key = param->spObject->key();
            assert(!key.empty());
            param->spObject->nodeId = m_name;

            auto& objsMan = getSession().objsMan;
            std::shared_ptr<INode> spNode = shared_from_this();
            objsMan->collectingObject(param->spObject, spNode, m_bView);
        }
    }
}

zany INode::get_output_result(std::shared_ptr<INode> outNode, std::string out_param, bool bCopy) {
    zany outResult = outNode->get_output_obj(out_param);
    if (bCopy && outResult) {
        outResult = outResult->clone();
        //if (outResult->key().empty()) {
        //    outResult->key = generateUUID();
        //}
    }
    return outResult;
}

ZENO_API bool INode::requireInput(std::string const& ds) {
    // 目前假设输入对象和输入数值，不能重名（不难实现，老节点直接改）。
    auto iter = m_inputObjs.find(ds);
    if (iter != m_inputObjs.end()) {
        ObjectParam* in_param = iter->second.get();
        if (in_param->links.empty()) {
            //节点如果定义了对象，但没有边连上去，是否要看节点apply如何处理？
        }
        else {
            switch (in_param->type)
            {
                case Param_Dict:
                {
                    in_param->spObject = processDict(in_param);
                    break;
                }
                case Param_List:
                {
                    in_param->spObject = processList(in_param);
                    break;
                }
                case Param_Curve:
                {
                    //Curve要视作Object，因为整合到variant太麻烦，只要对于最原始的MakeCurve节点，以字符串（储存json）作为特殊类型即可。
                }
                default:
                {
                    if (in_param->links.size() == 1)
                    {
                        std::shared_ptr<CoreLink> spLink = *in_param->links.begin();
                        ObjectParam* out_param = spLink->fromparam;
                        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

                        GraphException::translated([&] {
                            outNode->doApply();
                        }, outNode.get());

                        zany outResult = out_param->spObject;
                        //观察端口属性
                        //TODO
                        if (in_param->socketType == Socket_Clone) {
                            in_param->spObject = outResult->clone();
                        }
                        else if (in_param->socketType == Socket_Owning) {
                            in_param->spObject = outResult->move_clone();
                        }
                        else if (in_param->socketType == Socket_ReadOnly) {
                            in_param->spObject = outResult;
                            //TODO: readonly property on object.
                        }
                    }
                }
            }
        }
    }
    else {
        auto iter2 = m_inputPrims.find(ds);
        if (iter2 != m_inputPrims.end()) {
            PrimitiveParam* in_param = iter2->second.get();
            if (in_param->links.empty()) {
                in_param->result = process(in_param);
                //旧版本的requireInput指的是是否有连线，如果想兼容旧版本，这里可以返回false，但使用量不多，所以就修改它的定义。
            }
            else {
                if (in_param->links.size() == 1) {
                    std::shared_ptr<ParamLink> spLink = *in_param->links.begin();
                    std::shared_ptr<INode> outNode = spLink->fromparam->m_wpNode.lock();

                    GraphException::translated([&] {
                        outNode->doApply();
                    }, outNode.get());
                    //数值基本类型，直接复制。
                    in_param->result = spLink->fromparam->result;
                }
            }
        } else {
            return false;
        }
    }
    return true;
}

ZENO_API void INode::doOnlyApply() {
    apply();
}

ZENO_API void INode::doApply() {

    if (!m_dirty) {
        registerObjToManager();//如果只是打view，也是需要加到manager的。
        return;
    }

    /*
    zeno::scope_exit spe([&] {//apply时根据情况将IParam标记为modified，退出时将所有IParam标记为未modified
        for (auto const& [name, param] : m_outputs)
            param->m_idModify = false;
        });
    */

    preApply();

    if (zeno::getSession().is_interrupted()) {
        throw makeError<InterruputError>(m_uuidPath);
    }

    log_debug("==> enter {}", m_name);
    {
#ifdef ZENO_BENCHMARKING
        Timer _(m_name);
#endif
        reportStatus(true, Node_Running);
        apply();
    }
    log_debug("==> leave {}", m_name);

    registerObjToManager();
    reportStatus(false, Node_RunSucceed);
}

ZENO_API ObjectParams INode::get_input_object_params() const
{
    ObjectParams params;
    for (auto& [name, spObjParam] : m_inputObjs)
    {
        ParamObject obj;
        for (auto linkInfo : spObjParam->links) {
            obj.links.push_back(getEdgeInfo(linkInfo));
        }
        obj.name = name;
        obj.type = spObjParam->type;
        obj.bInput = true;
        //obj.prop = ?
        params.push_back(obj);
    }
    return params;
}

ZENO_API ObjectParams INode::get_output_object_params() const
{
    ObjectParams params;
    for (auto& [name, spObjParam] : m_outputObjs)
    {
        ParamObject obj;
        for (auto linkInfo : spObjParam->links) {
            obj.links.push_back(getEdgeInfo(linkInfo));
        }
        obj.name = name;
        obj.type = spObjParam->type;
        obj.bInput = false;
        //obj.prop = ?
        params.push_back(obj);
    }
    return params;
}

ZENO_API PrimitveParams INode::get_input_primitive_params() const {
    //TODO: deprecated node.
    PrimitveParams params;
    for (auto& [name, spParamObj] : m_inputPrims) {
        ParamInfo param;
        param.bInput = true;
        param.control = spParamObj->control;
        param.ctrlProps = spParamObj->optCtrlprops;
        param.defl = spParamObj->defl;
        for (auto spLink : spParamObj->links) {
            param.links.push_back(getEdgeInfo(spLink));
        }
        params.push_back(param);
    }
    return params;
}

ZENO_API PrimitveParams INode::get_output_primitivie_params() const {
    PrimitveParams params;
    for (auto& [name, spParamObj] : m_inputPrims) {
        ParamInfo param;
        param.bInput = false;
        param.control = NullControl;
        param.ctrlProps = std::nullopt;
        param.defl = spParamObj->defl;
        for (auto spLink : spParamObj->links) {
            param.links.push_back(getEdgeInfo(spLink));
        }
        params.push_back(param);
    }
    return params;
}

ZENO_API ParamInfo INode::get_input_prim_param(std::string const& name) const {
    auto& paramPrim = safe_at(m_inputPrims, name, "miss input param `" + name + "` on node `" + m_name + "`");
    ParamInfo param = paramPrim->export();
    return param;
}

ZENO_API ParamObject INode::get_input_obj_param(std::string const& name) const {
    auto& paramObj = safe_at(m_inputObjs, name, "miss input object `" + name + "` on node `" + m_name + "`");
    ParamObject param = paramObj->export();
    return param;
}

ZENO_API ParamInfo INode::get_output_prim_param(std::string const& name) const {
    auto& paramObj = safe_at(m_outputPrims, name, "miss input param `" + name + "` on node `" + m_name + "`");
    ParamInfo param = paramObj->export();
    return param;
}

ZENO_API ParamObject INode::get_output_obj_param(std::string const& name) const {
    auto& paramObj = safe_at(m_outputObjs, name, "miss output object `" + name + "` on node `" + m_name + "`");
    ParamObject param = paramObj->export();
    return param;
}

bool INode::add_input_prim_param(ParamInfo param) {
    if (m_inputPrims.find(param.name) != m_inputPrims.end()) {
        return false;
    }
    std::unique_ptr<PrimitiveParam> sparam = std::make_unique<PrimitiveParam>();
    sparam->bInput = true;
    sparam->control = param.control;
    sparam->defl = param.defl;
    sparam->m_wpNode = shared_from_this();
    sparam->name = param.name;
    sparam->socketType = param.socketType;
    sparam->type = param.type;
    sparam->optCtrlprops = param.ctrlProps;
    m_inputPrims.insert(std::make_pair(param.name, sparam));
    return true;
}

bool INode::add_input_obj_param(ParamObject param) {
    if (m_inputObjs.find(param.name) != m_inputObjs.end()) {
        return false;
    }
    std::unique_ptr<ObjectParam> sparam = std::make_unique<ObjectParam>();
    sparam->bInput = true;
    sparam->name = param.name;
    sparam->type = param.type;
    sparam->socketType = param.socketType;
    sparam->m_wpNode = shared_from_this();
    m_inputObjs.insert(std::make_pair(param.name, sparam));
    return true;
}

bool INode::add_output_prim_param(ParamInfo param) {
    if (m_inputPrims.find(param.name) != m_inputPrims.end()) {
        return false;
    }
    std::unique_ptr<PrimitiveParam> sparam = std::make_unique<PrimitiveParam>();
    sparam->bInput = false;
    sparam->control = param.control;
    sparam->defl = param.defl;
    sparam->m_wpNode = shared_from_this();
    sparam->name = param.name;
    sparam->socketType = param.socketType;
    sparam->type = param.type;
    sparam->optCtrlprops = param.ctrlProps;
    m_inputPrims.insert(std::make_pair(param.name, sparam));
    return true;
}

bool INode::add_output_obj_param(ParamObject param) {
    if (m_inputObjs.find(param.name) != m_inputObjs.end()) {
        return false;
    }
    std::unique_ptr<ObjectParam> sparam = std::make_unique<ObjectParam>();
    sparam->bInput = true;
    sparam->name = param.name;
    sparam->type = param.type;
    sparam->socketType = param.socketType;
    sparam->m_wpNode = shared_from_this();
    m_inputObjs.insert(std::make_pair(param.name, sparam));
    return true;
}

ZENO_API void INode::set_result(bool bInput, const std::string& name, zany spObj) {
    if (bInput) {
        auto& param = safe_at(m_inputObjs, name, "");
        param->spObject = spObj;
    }
    else {
        auto& param = safe_at(m_outputObjs, name, "");
        param->spObject = spObj;
    }
}

ZENO_API std::string INode::get_viewobject_output_param() const {
    //现在暂时还没有什么标识符用于指定哪个输出口是对应输出view obj的
    //一般都是默认第一个输出obj，暂时这么规定，后续可能用标识符。
    if (m_outputObjs.empty())
        return "";
    return m_outputObjs.begin()->second->name;
}

ZENO_API NodeData INode::exportInfo() const
{
    NodeData node;
    node.cls = m_nodecls;
    node.name = m_name;
    node.bView = m_bView;
    node.uipos = m_pos;
    //TODO: node type
    if (node.subgraph.has_value())
        node.type = Node_SubgraphNode;
    else
        node.type = Node_Normal;

    for (auto& [name, paramObj] : m_inputObjs)
    {
        node.inputObjs.push_back(paramObj->export());
    }
    for (auto& [name, paramObj] : m_inputPrims)
    {
        node.inputPrims.push_back(paramObj->export());
    }
    for (auto& [name, paramObj] : m_outputPrims)
    {
        node.outputPrims.push_back(paramObj->export());
    }
    for (auto& [name, paramObj] : m_outputObjs)
    {
        node.outputObjs.push_back(paramObj->export());
    }
    node.customUi = nodeClass->m_customui;
    return node;
}

ZENO_API bool INode::update_param(const std::string& param, const zvariant& new_value) {
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputPrims, param, "miss input param `" + param + "` on node `" + m_name + "`");
    if (!zeno::isEqual(spParam->defl, new_value, spParam->type))
    {
        zvariant old_value = spParam->defl;
        spParam->defl = new_value;

        std::shared_ptr<Graph> spGraph = graph.lock();
        assert(spGraph);

        spGraph->onNodeParamUpdated(spParam.get(), old_value, new_value);
        CALLBACK_NOTIFY(update_param, param, old_value, new_value)
        mark_dirty(true);
        checkReference(spParam);
        return true;
    }
    return false;
}

ZENO_API params_change_info INode::update_editparams(const ParamsUpdateInfo& params)
{
    params_change_info ret;
    return ret;
}

ZENO_API void INode::init(const NodeData& dat)
{
    //IO init
    if (!dat.name.empty())
        m_name = dat.name;

    m_pos = dat.uipos;
    m_bView = dat.bView;
    if (m_bView) {
        std::shared_ptr<Graph> spGraph = graph.lock();
        assert(spGraph);
        spGraph->viewNodeUpdated(m_name, m_bView);
    }
    initParams(dat);
    m_dirty = true;
}

ZENO_API void INode::initParams(const NodeData& dat)
{
    for (const ParamObject& paramObj : dat.inputObjs)
    {
        add_input_obj_param(paramObj);
    }
    for (const ParamInfo& param : dat.inputPrims)
    {
        add_input_prim_param(param);
    }
    for (const ParamInfo& param : dat.outputPrims)
    {
        add_output_prim_param(param);
    }
    for (const ParamObject& paramObj : dat.outputObjs)
    {
        add_output_obj_param(paramObj);
    }
}

ZENO_API bool INode::has_input(std::string const &id) const {
    //这个has_input在旧的语义里，代表的是input obj，如果有一些边没有连上，那么有一些参数值仅有默认值，未必会设这个input的，
    //还有一种情况，就是对象值是否有输入引入
    //这种情况要看旧版本怎么处理。
    //对于新版本而言，对于数值型输入，没有连上边仅有默认值，就不算has_input，有点奇怪，因此这里直接判断参数是否存在。
    auto iter = m_inputObjs.find(id);
    if (iter != m_inputObjs.end()) {
        return !iter->second->links.empty();
    }
    else {
        return m_inputPrims.find(id) != m_inputPrims.end();
    }
}

ZENO_API zany INode::get_input(std::string const &id) const {
    auto iter = m_inputPrims.find(id);
    if (iter != m_inputPrims.end()) {
        auto& val = iter->second->result;
        switch (iter->second->type) {
            case Param_Int:
            case Param_Float:
            case Param_Bool:
            case Param_Vec2f:
            case Param_Vec2i:
            case Param_Vec3f:
            case Param_Vec3i:
            case Param_Vec4f:
            case Param_Vec4i:
            {
                //依然有很多节点用了NumericObject，为了兼容，需要套一层NumericObject出去。
                std::shared_ptr<NumericObject> spNum = std::make_shared<NumericObject>();
                zvariant value;
                if (std::holds_alternative<int>(val))
                {
                    spNum->set<int>(std::get<int>(val));
                }
                else if (std::holds_alternative<float>(val))
                {
                    spNum->set<float>(std::get<float>(val));
                }
                else if (std::holds_alternative<vec2i>(val))
                {
                    spNum->set<vec2i>(std::get<vec2i>(val));
                }
                else if (std::holds_alternative<vec2f>(val))
                {
                    spNum->set<vec2f>(std::get<vec2f>(val));
                }
                else if (std::holds_alternative<vec3i>(val))
                {
                    spNum->set<vec3i>(std::get<vec3i>(val));
                }
                else if (std::holds_alternative<vec3f>(val))
                {
                    spNum->set<vec3f>(std::get<vec3f>(val));
                }
                else if (std::holds_alternative<vec4i>(val))
                {
                    spNum->set<vec4i>(std::get<vec4i>(val));
                }
                else if (std::holds_alternative<vec4f>(val))
                {
                    spNum->set<vec4f>(std::get<vec4f>(val));
                }
                else
                {
                    //throw makeError<TypeError>(typeid(T));
                    //error, throw expection.
                }
                break;
            }
            case Param_String:
            {
                if (std::holds_alternative<std::string>(val))
                {
                    std::shared_ptr<StringObject> stringobj = std::make_shared<StringObject>();
                    return stringobj;
                }
                else {
                    //error, throw expection.
                }
                break;
            }
            return nullptr;
        }
    }
    else {
        auto iter2 = m_inputObjs.find(id);
        if (iter2 != m_inputObjs.end()) {
            return iter2->second->spObject;
        }
        else {
            return nullptr;
        }
    }
}

ZENO_API zvariant INode::resolveInput(std::string const& id) {
    if (requireInput(id)) {
        auto iter = m_inputPrims.find(id);
        return iter->second->result;
    }
    else {
        return nullptr;
    }
}

ZENO_API void INode::set_pos(std::pair<float, float> pos) {
    m_pos = pos;
    CALLBACK_NOTIFY(set_pos, m_pos)
}

ZENO_API std::pair<float, float> INode::get_pos() const {
    return m_pos;
}

ZENO_API bool INode::in_asset_file() const {
    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    return getSession().assets->isAssetGraph(spGraph);
}

ZENO_API bool INode::set_primitive_output(std::string const& id, const zvariant& val) {

}

ZENO_API bool INode::set_output(std::string const& param, zany obj) {
    auto iter = m_outputObjs.find(param);
    if (iter != m_outputObjs.end()) {
        iter->second->spObject = obj;
        return true;
    }
    else {
        auto iter2 = m_outputPrims.find(param);
        if (iter2 != m_outputPrims.end()) {
            //兼容以前NumericObject的情况
            if (auto numObject = std::dynamic_pointer_cast<NumericObject>(obj)) {
                const auto& val = numObject->value;
                if (std::holds_alternative<int>(val))
                {
                    iter2->second->result = std::get<int>(val);
                }
                else if (std::holds_alternative<float>(val))
                {
                    iter2->second->result = std::get<float>(val);
                }
                else if (std::holds_alternative<vec2i>(val))
                {
                    iter2->second->result = std::get<vec2i>(val);
                }
                else if (std::holds_alternative<vec2f>(val))
                {
                    iter2->second->result = std::get<vec2f>(val);
                }
                else if (std::holds_alternative<vec3i>(val))
                {
                    iter2->second->result = std::get<vec3i>(val);
                }
                else if (std::holds_alternative<vec3f>(val))
                {
                    iter2->second->result = std::get<vec3f>(val);
                }
                else if (std::holds_alternative<vec4i>(val))
                {
                    iter2->second->result = std::get<vec4i>(val);
                }
                else if (std::holds_alternative<vec4f>(val))
                {
                    iter2->second->result = std::get<vec4f>(val);
                }
                else
                {
                    //throw makeError<TypeError>(typeid(T));
                    //error, throw expection.
                }
            }
            else if (auto strObject = std::dynamic_pointer_cast<StringObject>(obj)) {
                const auto& val = numObject->value;
                if (std::holds_alternative<std::string>(val))
                {
                    iter2->second->result = std::get<std::string>(val);
                }
                else {
                    //throw makeError<TypeError>(typeid(T));
                    //error, throw expection.
                }
            }
            return true;
        }
    }
    return false;
}

ZENO_API zany INode::get_output_obj(std::string const& param) {
    auto& spParam = safe_at(m_outputObjs, param, "miss output param `" + param + "` on node `" + m_name + "`");
    return spParam->spObject;
}

ZENO_API bool INode::has_keyframe(std::string const &id) const {
    return false;
    //return kframes.find(id) != kframes.end();
}

ZENO_API zany INode::get_keyframe(std::string const &id) const 
{
    //deprecated: will parse it when processing defl value
    return nullptr;
    /*
    auto value = safe_at(inputs, id, "input socket of node `" + myname + "`");
    auto curves = dynamic_cast<zeno::CurveObject *>(value.get());
    if (!curves) {
        return value;
    }
    int frame = getGlobalState()->getFrameId();
    if (curves->keys.size() == 1) {
        auto val = curves->keys.begin()->second.eval(frame);
        value = objectFromLiterial(val);
    } else {
        int size = curves->keys.size();
        if (size == 2) {
            zeno::vec2f vec2;
            for (std::map<std::string, CurveData>::const_iterator it = curves->keys.cbegin(); it != curves->keys.cend();
                 it++) {
                int index = it->first == "x" ? 0 : 1;
                vec2[index] = it->second.eval(frame);
            }
            value = objectFromLiterial(vec2);
        } else if (size == 3) {
            zeno::vec3f vec3;
            for (std::map<std::string, CurveData>::const_iterator it = curves->keys.cbegin(); it != curves->keys.cend();
                 it++) {
                int index = it->first == "x" ? 0 : it->first == "y" ? 1 : 2;
                vec3[index] = it->second.eval(frame);
            }
            value = objectFromLiterial(vec3);
        } else if (size == 4) {
            zeno::vec4f vec4;
            for (std::map<std::string, CurveData>::const_iterator it = curves->keys.cbegin(); it != curves->keys.cend();
                 it++) {
                int index = it->first == "x" ? 0 : it->first == "y" ? 1 : it->first == "z" ? 2 : 3;
                vec4[index] = it->second.eval(frame);
            }
            value = objectFromLiterial(vec4);
        }
    }
    return value;
    */
}

ZENO_API bool INode::has_formula(std::string const &id) const {
    return false;
    //return formulas.find(id) != formulas.end();
}

ZENO_API zany INode::get_formula(std::string const &id) const 
{
    //deprecated: will parse it when processing defl value
    return nullptr;
    /*
    auto value = safe_at(inputs, id, "input socket of node `" + myname + "`");
    if (auto formulas = dynamic_cast<zeno::StringObject *>(value.get())) 
    {
        std::string code = formulas->get();

        auto& desc = nodeClass->desc;
        if (!desc)
            return value;

        bool isStrFmla = false;
        for (auto const& [type, name, defl, _] : desc->inputs) {
            if (name == id && (type == "string" || type == "writepath" || type == "readpath" || type == "multiline_string")) {
                isStrFmla = true;
                break;
            }
        }
        if (!isStrFmla) {
            for (auto const& [type, name, defl, _] : desc->params) {
                auto name_ = name + ":";
                if (id == name_ &&
                    (type == "string" || type == "writepath" || type == "readpath" || type == "multiline_string")) {
                    isStrFmla = true;
                    break;
                }
            }
        }

        //remove '='
        code.replace(0, 1, "");

        if (isStrFmla) {
            auto res = getThisGraph()->callTempNode("StringEval", { {"zfxCode", objectFromLiterial(code)} }).at("result");
            value = objectFromLiterial(std::move(res));
        }
        else
        {
            std::string prefix = "vec3";
            std::string resType;
            if (code.substr(0, prefix.size()) == prefix) {
                resType = "vec3f";
            }
            else {
                resType = "float";
            }
            auto res = getThisGraph()->callTempNode("NumericEval", { {"zfxCode", objectFromLiterial(code)}, {"resType", objectFromLiterial(resType)} }).at("result");
            value = objectFromLiterial(std::move(res));
        }
    }     
    return value;
    */
}

ZENO_API TempNodeCaller INode::temp_node(std::string const &id) {
    //TODO: deprecated
    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    return TempNodeCaller(spGraph.get(), id);
}

float INode::resolve(const std::string& formulaOrKFrame, const ParamType type)
{
    int frame = getGlobalState()->getFrameId();
    if (zeno::starts_with(formulaOrKFrame, "=")) {
        std::string code = formulaOrKFrame.substr(1);
        std::set<std::string>paths = zeno::getReferPath(code);
        std::string currPath = get_path_str();
        currPath = currPath.substr(0, currPath.find_last_of("/"));
        for (auto& path : paths)
        {
            auto absolutePath = zeno::absolutePath(currPath, path);
            if (absolutePath != path)
            {
                code.replace(code.find(path), path.size(), absolutePath);
            }
        }
        Formula fmla(code);
        float res = 0.;
        int ret = fmla.parse(res);
        return res;
    }
    else if (zany curve = zeno::parseCurveObj(formulaOrKFrame)) {
        std::shared_ptr<zeno::CurveObject> curves = std::dynamic_pointer_cast<zeno::CurveObject>(curve);
        assert(curves && curves->keys.size() == 1);
        float fVal = curves->keys.begin()->second.eval(frame);
        return fVal;
    }
    else {
        if (Param_Float == type)
        {
            float fVal = std::stof(formulaOrKFrame);
            return fVal;
        }
        else {
            float fVal = std::stoi(formulaOrKFrame);
            return fVal;
        }
    }
}

void INode::checkReference(std::shared_ptr<CoreParam> spParam)
{
    const auto& referMgr = getSession().referManager;
    std::string key = spParam->m_wpNode.lock()->m_uuid + "/" + spParam->name;
    bool bRef = referMgr->isRefered(key);//是否引用了其他参数
    bool bBeRef = referMgr->isBeRefered(key);//是否被其他参数引用
    std::set<std::string> paths = zeno::getReferPaths(spParam->defl);
    bool bExits = !paths.empty();

    if (bRef)
    {
        if (bExits)
            referMgr->updateBeReferedParam(key);
        else
            referMgr->removeReferParam(key);//位包含引用参数时需要删除信息
    }
    if (!bRef && bExits)
    {
        referMgr->addReferInfo(spParam);
    }
    if (bBeRef)
    {
        //被引用的参数数据更新时，引用该参数的节点需要标脏
        referMgr->updateDirty(key);
    }
}

template<class T, class E> T INode::resolveVec(const zvariant& defl, const ParamType type)
{
    if (std::holds_alternative<T>(defl)) {
        return std::get<T>(defl);
    }
    else if (std::holds_alternative<E>(defl)) {
        E vec = std::get<E>(defl);
        T vecnum;
        for (int i = 0; i < vec.size(); i++) {
            float fVal = resolve(vec[i], type);
            vecnum[i] = fVal;
        }
        return vecnum;
    }
    else {
        //error, throw expection.
        throw makeError<TypeError>(typeid(T));
    }
}

std::shared_ptr<DictObject> INode::processDict(ObjectParam* in_param) {
    std::shared_ptr<DictObject> spDict;
    //连接的元素是list还是list of list的规则，参照Graph::addLink下注释。
    bool bDirecyLink = false;
    const auto& inLinks = in_param->links;
    if (inLinks.size() == 1)
    {
        std::shared_ptr<CoreLink> spLink = inLinks.front();
        auto out_param = spLink->fromparam;
        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

        if (out_param->type == in_param->type && !spLink->tokey.empty())
        {
            bDirecyLink = true;
            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());
            zany outResult = get_output_result(outNode, out_param->name, Link_Copy == spLink->lnkProp);
            spDict = std::dynamic_pointer_cast<DictObject>(outResult);
        }
    }
    if (!bDirecyLink)
    {
        spDict = std::make_shared<DictObject>();
        for (const auto& spLink : in_param->links)
        {
            const std::string& keyName = spLink->tokey;
            auto out_param = spLink->fromparam;
            std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());

            zany outResult = get_output_result(outNode, out_param->name, Link_Copy == spLink->lnkProp);
            spDict->lut[keyName] = outResult;
        }
    }
    return spDict;
}

std::shared_ptr<ListObject> INode::processList(ObjectParam* in_param) {
    std::shared_ptr<ListObject> spList;
    bool bDirectLink = false;
    if (in_param->links.size() == 1)
    {
        std::shared_ptr<CoreLink> spLink = in_param->links.front();
        auto out_param = spLink->fromparam;
        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

        if (out_param->type == in_param->type && !spLink->tokey.empty()) {
            bDirectLink = true;

            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());

            zany outResult = get_output_result(outNode, out_param->name, Link_Copy == spLink->lnkProp);
            spList = std::dynamic_pointer_cast<ListObject>(outResult);
        }
    }
    if (!bDirectLink)
    {
        auto oldinput = std::dynamic_pointer_cast<ListObject>(in_param->result);

        spList = std::make_shared<ListObject>();
        int indx = 0;
        for (const auto& spLink : in_param->links)
        {
            //list的情况下，keyName是不是没意义，顺序怎么维持？
            auto out_param = spLink->fromparam;
            std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();
            if (outNode->is_dirty()) {  //list中的元素是dirty的，重新计算并加入list
                GraphException::translated([&] {
                    outNode->doApply();
                    }, outNode.get());

                zany outResult = get_output_result(outNode, out_param->name, Link_Copy == spLink->lnkProp);
                spList->push_back(outResult);
                //spList->dirtyIndice.insert(indx);
            }
            else {
                zany outResult = get_output_result(outNode, out_param->name, Link_Copy == spLink->lnkProp);
                spList->push_back(outResult);
            }
        }
    }
    return spList;
}

zvariant INode::process(PrimitiveParam* in_param)
{
    if (!in_param) {
        return nullptr;
    }

    int frame = getGlobalState()->getFrameId();
    //zany result;

    const ParamType type = in_param->type;
    const zvariant defl = in_param->defl;
    zvariant result;

    switch (type) {
        case Param_Int:
        case Param_Float:
        case Param_Bool:
        {
            //先不考虑int float的划分,直接按variant的值来。
            zvariant resolve_value;
            if (std::holds_alternative<std::string>(defl))
            {
                std::string str = std::get<std::string>(defl);
                float fVal = resolve(str, type);
                result = fVal;
            }
            else if (std::holds_alternative<int>(defl))
            {
                result = defl;
            }
            else if (std::holds_alternative<float>(defl))
            {
                result = defl;
            }
            else
            {
                //error, throw expection.
            }
            break;
        }
        case Param_String:
        {
            if (std::holds_alternative<std::string>(defl))
            {
                result = defl;
            }
            else {
                //error, throw expection.
            }
            break;
        }
        case Param_Vec2f:   result = resolveVec<vec2f, vec2s>(defl, type);  break;
        case Param_Vec2i:   result = resolveVec<vec2i, vec2s>(defl, type);  break;
        case Param_Vec3f:   result = resolveVec<vec3f, vec3s>(defl, type);  break;
        case Param_Vec3i:   result = resolveVec<vec3i, vec3s>(defl, type);  break;
        case Param_Vec4f:   result = resolveVec<vec4f, vec4s>(defl, type);  break;
        case Param_Vec4i:   result = resolveVec<vec4i, vec4s>(defl, type);  break;
        case Param_Heatmap:
        {
            //TODO: heatmap的结构体要整合到zvariant.
            //if (std::holds_alternative<std::string>(defl))
            //    result = zeno::parseHeatmapObj(std::get<std::string>(defl));
            break;
        }
        //这里指的是基础类型的List/Dict.
        case Param_List:
        {
            //TODO: List现在还没有ui支持，而且List是泛型容器，对于非Literal值不好设定默认值。
            break;
        }
        case Param_Dict:
        {
            break;
        }
    }
    return result;
}

}
