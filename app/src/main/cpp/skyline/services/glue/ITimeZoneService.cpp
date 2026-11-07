// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <algorithm>
#include <os.h>
#include <kernel/types/KProcess.h>
#include <services/timesrv/common.h>
#include <services/timesrv/results.h>
#include <services/timesrv/ITimeZoneService.h>
#include <services/timesrv/core.h>
#include "ITimeZoneService.h"

namespace skyline::service::glue {
    /**
     * @brief Reads the time zone binary of a location from the assets
     * @return false if there is no time zone with that name, so a game that asks for a wrong name gets an error instead of an exception
     */
    static bool ReadTimeZoneBinary(const DeviceState &state, std::string_view locationName, std::vector<u8> &buffer) {
        if (locationName.empty() || locationName.find("..") != std::string_view::npos)
            return false;

        std::string path{fmt::format("tzdata/zoneinfo/{}", locationName)};
        if (!state.os->assetFileSystem->FileExists(path))
            return false;

        auto file{state.os->assetFileSystem->OpenFile(path)};
        buffer.resize(file->size);
        file->Read(buffer);
        return true;
    }
    ITimeZoneService::ITimeZoneService(const DeviceState &state, ServiceManager &manager, std::shared_ptr<timesrv::ITimeZoneService> core, timesrv::core::TimeServiceObject &timesrvCore, bool writeable)
        : BaseService(state, manager),
          core(std::move(core)),
          timesrvCore(timesrvCore),
          locationNameUpdateEvent(std::make_shared<kernel::type::KEvent>(state, false)),
          writeable(writeable) {}

    Result ITimeZoneService::GetDeviceLocationName(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->GetDeviceLocationName(session, request, response);
    }

    Result ITimeZoneService::SetDeviceLocationName(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (!writeable)
            return timesrv::result::PermissionDenied;

        auto locationName{span(request.Pop<timesrv::LocationName>()).as_string(true)};
        std::vector<u8> timeZoneBinaryBuffer;
        if (!ReadTimeZoneBinary(state, locationName, timeZoneBinaryBuffer))
            return timesrv::result::TimeZoneNotFound;

        auto result{core->SetDeviceLocationNameWithTimeZoneBinary(span(locationName).as_string(true), timeZoneBinaryBuffer)};
        if (result)
            return result;

        locationNameUpdateEvent->Signal();
        return {};
    }

    Result ITimeZoneService::GetTotalLocationNameCount(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->GetTotalLocationNameCount(session, request, response);
    }

    Result ITimeZoneService::LoadLocationNameList(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto outList{request.outputBuf.at(0).cast<timesrv::LocationName>()};
        auto offset{request.Pop<u32>()};

        // The part of the list that is asked for can go past its end, only what exists is copied
        const auto &list{timesrvCore.locationNameList};
        size_t count{offset < list.size() ? std::min<size_t>(outList.size(), list.size() - offset) : 0};
        std::copy_n(list.begin() + offset, count, outList.begin());

        response.Push(static_cast<u32>(count));
        return {};
    }

    Result ITimeZoneService::LoadTimeZoneRule(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto locationName{span(request.Pop<timesrv::LocationName>()).as_string(true)};
        std::vector<u8> timeZoneBinaryBuffer;
        if (!ReadTimeZoneBinary(state, locationName, timeZoneBinaryBuffer))
            return timesrv::result::TimeZoneNotFound;

        return core->ParseTimeZoneBinary(timeZoneBinaryBuffer, request.outputBuf.at(0));
    }

    Result ITimeZoneService::GetTimeZoneRuleVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->GetTimeZoneRuleVersion(session, request, response);
    }

    Result ITimeZoneService::GetDeviceLocationNameAndUpdatedTime(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->GetDeviceLocationNameAndUpdatedTime(session, request, response);
    }

    Result ITimeZoneService::SetDeviceLocationNameWithTimeZoneBinary(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (!writeable)
            return timesrv::result::PermissionDenied;

        auto result{core->SetDeviceLocationNameWithTimeZoneBinaryIpc(session, request, response)};
        if (result)
            return result;

        locationNameUpdateEvent->Signal();
        return {};
    }

    Result ITimeZoneService::ParseTimeZoneBinary(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->ParseTimeZoneBinaryIpc(session, request, response);
    }

    Result ITimeZoneService::GetDeviceLocationNameOperationEventReadableHandle(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto handle{state.process->InsertItem(locationNameUpdateEvent)};
        LOGD("Location Name Update Event Handle: 0x{:X}", handle);
        response.copyHandles.push_back(handle);
        return {};
    }

    Result ITimeZoneService::ToCalendarTime(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->ToCalendarTime(session, request, response);
    }

    Result ITimeZoneService::ToCalendarTimeWithMyRule(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->ToCalendarTimeWithMyRule(session, request, response);
    }

    Result ITimeZoneService::ToPosixTime(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->ToPosixTime(session, request, response);
    }

    Result ITimeZoneService::ToPosixTimeWithMyRule(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return core->ToPosixTimeWithMyRule(session, request, response);
    }
}
