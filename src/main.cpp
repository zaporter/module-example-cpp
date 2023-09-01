#include <iostream>
#include <memory>

#include <boost/log/trivial.hpp>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_context.h>

#include <viam/api/common/v1/common.grpc.pb.h>
#include <viam/api/component/generic/v1/generic.grpc.pb.h>
#include <viam/api/robot/v1/robot.pb.h>

#include <viam/sdk/components/component.hpp>
#include <viam/sdk/components/generic/generic.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/module/module.hpp>
#include <viam/sdk/module/service.hpp>
#include <viam/sdk/registry/registry.hpp>
#include <viam/sdk/resource/resource.hpp>
#include <viam/sdk/rpc/dial.hpp>
#include <viam/sdk/rpc/server.hpp>

#include "signalmgr.hpp"
#include "wifi.hpp"

using viam::component::generic::v1::GenericService;
using namespace viam::sdk;

class MyModule : public GenericService::Service, public Component {
   public:
    API dynamic_api() const override {
        return Generic::static_api();
    }

    MyModule(ResourceConfig cfg) {
        name_ = cfg.name();
        std::cout << "Instantiating module with name " + name_ << std::endl;
    }

    MyModule(const MyModule&) = delete;
    MyModule& operator=(const MyModule&) = delete;

    ::grpc::Status DoCommand(::grpc::ServerContext* context,
                             const ::viam::common::v1::DoCommandRequest* request,
                             ::viam::common::v1::DoCommandResponse* response) override {
        if (auto errorMsg = read_wireless(response->mutable_result())) {} else {
            return grpc::Status(grpc::UNAVAILABLE, errorMsg);
        }
        return grpc::Status();
    }

   private:
    std::string name_;
};

int main_inner(int argc, char** argv) {
    if (argc < 2) {
        throw "need socket path as command line argument";
    }

    // Use set_logger_severity_from_args to set the boost trivial logger's
    // severity depending on commandline arguments.
    set_logger_severity_from_args(argc, argv);

    SignalManager signals;

    API generic = Generic::static_api();
    Model m("acme", "demo", "printer");

    std::shared_ptr<ModelRegistration> mr = std::make_shared<ModelRegistration>(
        ResourceType("MyModule"),
        generic,
        m,
        [](Dependencies, ResourceConfig cfg) { return std::make_unique<MyModule>(cfg); }
    );

    Registry::register_model(mr);

    // The `ModuleService_` must outlive the Server, so the declaration order
    // here matters.
    auto my_mod = std::make_shared<ModuleService_>(argv[1]);
    auto server = std::make_shared<Server>();

    my_mod->add_model_from_registry(server, generic, m);
    my_mod->start(server);
    BOOST_LOG_TRIVIAL(info) << "Module started " << m.to_string();

    std::thread server_thread([&server, &signals]() {
        server->start();
        int sig = 0;
        auto result = signals.wait(&sig);
        server->shutdown();
    });

    server->wait();
    server_thread.join();

    return 0;
};

int main(int argc, char** argv) {
    try {
        return main_inner(argc, argv);
    } catch (char const* msg) {
        // todo: better error wrapping
        printf("threw char const* %s\n", msg);
        return 1;
    }
}