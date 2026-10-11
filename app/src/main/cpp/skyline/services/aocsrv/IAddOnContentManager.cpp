// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <kernel/types/KProcess.h>
#include <vfs/installed_content.h>
#include "IAddOnContentManager.h"
#include "IPurchaseEventManager.h"

namespace skyline::service::aocsrv {
    IAddOnContentManager::IAddOnContentManager(const DeviceState &state, ServiceManager &manager)
        : BaseService(state, manager),
          addOnContentListChangedEvent(std::make_shared<type::KEvent>(state, false)) {}

    Result IAddOnContentManager::CountAddOnContent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(static_cast<u32>(vfs::ListInstalledDlcIndices(state).size()));
        return {};
    }

    Result IAddOnContentManager::ListAddOnContent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 offset{request.Pop<u32>()};
        u32 count{request.Pop<u32>()};

        auto indices{vfs::ListInstalledDlcIndices(state)};
        u32 written{};
        if (!request.outputBuf.empty() && offset < indices.size()) {
            auto output{request.outputBuf.at(0).cast<u32>()};
            written = static_cast<u32>(std::min<size_t>({indices.size() - offset, static_cast<size_t>(count), output.size()}));
            std::copy_n(indices.begin() + offset, written, output.begin());
        }

        response.Push<u32>(written);
        return {};
    }

    Result IAddOnContentManager::GetAddOnContentBaseId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        // The DLC of a game have the title ids following (application id + 0x1000)
        response.Push<u64>(vfs::GetCurrentTitleId(state) + 0x1000);
        return {};
    }

    Result IAddOnContentManager::PrepareAddOnContent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IAddOnContentManager::GetAddOnContentListChangedEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        KHandle handle{state.process->InsertItem(addOnContentListChangedEvent)};
        LOGD("Add On Content List Changed Event Handle: 0x{:X}", handle);

        response.copyHandles.push_back(handle);
        return {};
    }

    Result IAddOnContentManager::GetAddOnContentListChangedEventWithProcessId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        KHandle handle{state.process->InsertItem(addOnContentListChangedEvent)};
        LOGD("Add On Content List Changed Event Handle: 0x{:X}", handle);

        response.copyHandles.push_back(handle);
        return {};
    }

    Result IAddOnContentManager::CheckAddOnContentMountStatus(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IAddOnContentManager::CreateEcPurchasedEventManager(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(IPurchaseEventManager), session, response);
        return {};
    }
}
