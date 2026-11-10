// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <kernel/types/KProcess.h>
#include "IParentalControlService.h"

namespace skyline::service::pctl {
    IParentalControlService::IParentalControlService(const DeviceState &state, ServiceManager &manager)
        : BaseService(state, manager),
          synchronizationEvent(std::make_shared<type::KEvent>(state, false)),
          requestSuspensionEvent(std::make_shared<type::KEvent>(state, false)),
          unlinkedEvent(std::make_shared<type::KEvent>(state, false)) {}

    Result IParentalControlService::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IParentalControlService::CheckFreeCommunicationPermission(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IParentalControlService::ConfirmStereoVisionPermission(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return IsStereoVisionPermittedImpl();
    }

    Result IParentalControlService::EndFreeCommunication(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IParentalControlService::IsFreeCommunicationAvailable(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(0);
        return {};
    }

    Result IParentalControlService::ConfirmStereoVisionRestrictionConfigurable(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (stereoVisionRestrictionConfigurable)
            return {};
        else
            return result::StereoVisionRestrictionConfigurableDisabled;
    }

    Result IParentalControlService::GetStereoVisionRestriction(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        bool stereoVisionRestrictionTemp{false};

        if (stereoVisionRestrictionConfigurable)
            stereoVisionRestrictionTemp = stereoVisionRestriction;

        response.Push<u8>(stereoVisionRestrictionTemp);

        return {};
    }

    Result IParentalControlService::SetStereoVisionRestriction(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        bool stereoVisionRestrictionTemp{request.Pop<bool>()};

        if (!featuresRestriction && stereoVisionRestrictionConfigurable)
            stereoVisionRestriction = stereoVisionRestrictionTemp;

        return {};
    }

    Result IParentalControlService::ResetConfirmedStereoVisionPermission(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        stereoVisionRestriction = false;
        return {};
    }

    Result IParentalControlService::IsStereoVisionPermitted(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        bool isStereoVisionPermitted{false};

        Result resultCode{IsStereoVisionPermittedImpl()};

        if (resultCode == Result{})
            isStereoVisionPermitted = true;

        response.Push<u8>(isStereoVisionPermitted);

        return resultCode;
    }

    Result IParentalControlService::IsStereoVisionPermittedImpl() {
        if (stereoVisionRestrictionConfigurable && stereoVisionRestriction)
            return result::StereoVisionDenied;
        else
            return {};
    }

    Result IParentalControlService::GetSynchronizationEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.copyHandles.push_back(state.process->InsertItem(synchronizationEvent));
        return {};
    }

    Result IParentalControlService::GetPlayTimerEventToRequestSuspension(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.copyHandles.push_back(state.process->InsertItem(requestSuspensionEvent));
        return {};
    }

    Result IParentalControlService::GetUnlinkedEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.copyHandles.push_back(state.process->InsertItem(unlinkedEvent));
        return {};
    }
}
