// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "results.h"
#include "IFile.h"

namespace skyline::service::fssrv {
    IFile::IFile(std::shared_ptr<vfs::Backing> backing, const DeviceState &state, ServiceManager &manager)
        : BaseService(state, manager),
          backing(std::move(backing)) {}

    Result IFile::Read(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto readOption{request.Pop<u32>()};
        request.Skip<u32>();
        auto offset{request.Pop<i64>()};
        auto size{request.Pop<i64>()};

        if (offset < 0) {
            LOGW("Trying to read a file with a negative offset");
            return result::InvalidOffset;
        }

        if (size < 0) {
            LOGW("Trying to read a file with a negative size");
            return result::InvalidSize;
        }

        response.Push<u64>(backing->ReadUnchecked(request.outputBuf.at(0), static_cast<size_t>(offset)));
        return {};
    }

    Result IFile::Write(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto writeOption{request.Pop<u32>()};
        request.Skip<u32>();
        auto offset{request.Pop<i64>()};
        auto size{request.Pop<i64>()};

        if (offset < 0) {
            LOGW("Trying to write to a file with a negative offset");
            return result::InvalidOffset;
        }

        if (size < 0) {
            LOGW("Trying to write to a file with a negative size");
            return result::InvalidSize;
        }

        if (request.inputBuf.at(0).size() < size) {
            LOGW("The input buffer is not large enough to fit the requested size");
            return result::InvalidSize;
        }

        if (backing->Write(request.inputBuf.at(0), static_cast<size_t>(offset)) != size) {
            LOGW("Failed to write all data to the backing");
            return result::UnexpectedFailure;
        }

        return {};
    }

    Result IFile::Flush(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        backing->Sync();
        return {};
    }

    Result IFile::SetSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        backing->Resize(request.Pop<u64>());
        return {};
    }

    Result IFile::GetSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u64>(backing->size);
        return {};
    }

    Result IFile::OperateRange(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto operationId{request.Pop<u32>()};
        request.Skip<u32>();
        auto offset{request.Pop<i64>()};
        auto size{request.Pop<i64>()};

        if (offset < 0)
            return result::InvalidOffset;
        if (size < 0)
            return result::InvalidSize;

        // 0 is Clear, 1 ClearSignature, 2 InvalidateCache and 3 QueryRange. The file is a plain file of the host (or a part of an NCA), so there is no cache to invalidate or signature to clear, and the information about the range (the kind of storage and the flags of it) that QueryRange returns is all zeros
        if (operationId > 3) {
            LOGW("Unknown IFile::OperateRange operation: {}", operationId);
            return result::InvalidArgument;
        }

        response.Push(std::array<u8, 0x40>{});
        return {};
    }
}
