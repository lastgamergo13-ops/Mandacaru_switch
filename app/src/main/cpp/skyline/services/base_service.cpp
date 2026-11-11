// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <cxxabi.h>
#include <mutex>
#include <unordered_map>
#include <common/trace.h>
#include "base_service.h"

namespace skyline::service {
    const std::string &BaseService::GetName() {
        if (name.empty()) {
            auto mangledName{typeid(*this).name()};

            int status{};
            size_t length{};
            std::unique_ptr<char, decltype(&std::free)> demangled{abi::__cxa_demangle(mangledName, nullptr, &length, &status), std::free};

            name = (status == 0) ? std::string(demangled.get() + std::char_traits<char>::length("skyline::service::")) : mangledName;
        }
        return name;
    }

    /**
     * @brief Writes the error a service function returned to the guest to the log, a game that behaves wrongly after one of these is usually reacting to it
     * @note Only the first few failures of each function are written, a game that polls a function that keeps failing would otherwise flood the log
     */
    void BaseService::LogFailedRequest(const char *functionName, Result result) {
        static std::mutex failureMutex;
        static std::unordered_map<std::string, u32> failureCounts;

        std::string key{functionName}; // Already in the "Class::Function" format
        u32 count;
        {
            std::scoped_lock lock{failureMutex};
            count = ++failureCounts[key];
        }

        if (count <= 5 || count == 100 || count == 1000 || count == 10000)
            LOGW("{} returned error 0x{:X} (module {}, description {}) to the guest, {} time(s) so far", key, result.raw, result.raw & 0x1FF, (result.raw >> 9) & 0x1FFF, count);
    }

    Result service::BaseService::HandleRequest(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        ServiceFunctionDescriptor function;
        u32 functionId{request.isTipc ? static_cast<u32>(request.header->type) : request.payload->value};

        try {
            function = GetServiceFunction(functionId, request.isTipc);
            LOGDNF("Service: {}", function.name);
        } catch (const std::out_of_range &) {
            LOGW("Cannot find {0} function in service '{1}': 0x{2:X} ({2})", request.isTipc ? "TIPC" : "HIPC", GetName(), static_cast<u32>(functionId));
            return {};
        }
        TRACE_EVENT("service", perfetto::StaticString{function.name});
        try {
            Result result{function(session, request, response)};
            if (result.raw) [[unlikely]]
                LogFailedRequest(function.name, result);
            return result;
        } catch (exception &e) {
            // We need to forward any skyline::exception objects without modification even though they inherit from std::exception
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception &e) {
            throw exception("{} (Service: {})", e.what(), function.name);
        }
    }
}
