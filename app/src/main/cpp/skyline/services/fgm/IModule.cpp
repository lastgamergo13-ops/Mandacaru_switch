// SPDX-License-Identifier: MPL-2.0

#include <kernel/types/KProcess.h>
#include "IModule.h"

namespace skyline::service::fgm {
    IRequest::IRequest(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager), event{std::make_shared<type::KEvent>(state, true)} {}

    Result IRequest::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.copyHandles.push_back(state.process->InsertItem(event));
        return {};
    }

    Result IRequest::Set(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IRequest::Get(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(0);
        return {};
    }

    Result IRequest::Cancel(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    IModule::IModule(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result IModule::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(IRequest), session, response);
        return {};
    }
}
