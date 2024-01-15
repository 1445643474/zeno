#pragma once

#include <zeno/utils/api.h>
#include <zeno/core/Descriptor.h>
#include <memory>
#include <string>
#include <map>

namespace zeno {

struct Graph;
struct Session;
struct INode;

struct INodeClass {
    std::unique_ptr<Descriptor> desc;
    std::string name;

    ZENO_API INodeClass(Descriptor const &desc, std::string const &name);
    ZENO_API virtual ~INodeClass();

    virtual std::shared_ptr<INode> new_instance(std::string const& ident) const = 0;
};

struct IObject;
struct GlobalState;
struct GlobalComm;
struct GlobalStatus;
struct EventCallbacks;
struct UserData;

struct Session {
    std::map<std::string, std::unique_ptr<INodeClass>> nodeClasses;

    std::unique_ptr<GlobalState> const globalState;
    std::unique_ptr<GlobalComm> const globalComm;
    std::unique_ptr<GlobalStatus> const globalStatus;
    std::unique_ptr<EventCallbacks> const eventCallbacks;
    std::unique_ptr<UserData> const m_userData;
    std::shared_ptr<Graph> mainGraph;

    ZENO_API Session();
    ZENO_API ~Session();

    Session(Session const &) = delete;
    Session &operator=(Session const &) = delete;
    Session(Session &&) = delete;
    Session &operator=(Session &&) = delete;

    ZENO_API UserData &userData() const;
    ZENO_API std::shared_ptr<Graph> createGraph();
    ZENO_API std::string dumpDescriptors() const;
    ZENO_API std::string dumpDescriptorsJSON() const;
    ZENO_API void defNodeClass(std::shared_ptr<INode>(*ctor)(), std::string const &id, Descriptor const &desc = {});
};

ZENO_API Session &getSession();

}
