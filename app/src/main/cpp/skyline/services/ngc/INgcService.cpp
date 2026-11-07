// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <cstring>
#include "INgcService.h"

namespace skyline::service::ngc {
    namespace {
        /**
         * @brief Copies the text of the first input buffer to the first output buffer (what a masked text is when no word is masked)
         */
        void CopyText(ipc::IpcRequest &request) {
            if (request.inputBuf.empty() || request.outputBuf.empty())
                return;
            auto input{request.inputBuf.at(0)};
            auto output{request.outputBuf.at(0)};
            std::memcpy(output.data(), input.data(), std::min(input.size(), output.size()));
        }
    }

    INgcService::INgcService(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result INgcService::GetContentVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(1);
        return {};
    }

    Result INgcService::Check(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(0); // No word of the text matched
        return {};
    }

    Result INgcService::Mask(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        CopyText(request);
        response.Push<u32>(0);
        return {};
    }

    Result INgcService::Reload(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    ITextFilter::ITextFilter(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result ITextFilter::Match(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(0); // Nothing to filter
        return {};
    }

    Result ITextFilter::Filter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        CopyText(request);
        return {};
    }
}
