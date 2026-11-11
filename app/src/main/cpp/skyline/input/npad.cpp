// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <input.h>
#include "npad.h"

namespace skyline::input {
    NpadManager::NpadManager(const DeviceState &state, input::HidSharedMemory *hid) : state(state), npads
        {NpadDevice{*this, hid->npad[0], NpadId::Player1}, {*this, hid->npad[1], NpadId::Player2},
         {*this, hid->npad[2], NpadId::Player3}, {*this, hid->npad[3], NpadId::Player4},
         {*this, hid->npad[4], NpadId::Player5}, {*this, hid->npad[5], NpadId::Player6},
         {*this, hid->npad[6], NpadId::Player7}, {*this, hid->npad[7], NpadId::Player8},
         {*this, hid->npad[8], NpadId::Handheld}, {*this, hid->npad[9], NpadId::Unknown},
        } { Activate(); /* NPads are activated by default, certain homebrew is reliant on this behavior */ }

    void NpadManager::Update() {
        std::scoped_lock guard{mutex};

        if (!activated)
            return;

        // The new mapping is built in a local array and only published at the end, resetting 'controller.device' to null up front made the input thread drop button/stick events (including the release of a pressed button) that arrived while the mapping was being recomputed, which left inputs stuck or ignored by the guest
        std::array<NpadDevice *, constant::ControllerCount> newDevices{};

        for (auto &id : supportedIds) {
            if (id == NpadId::Unknown || !IsNpadIdValid(id))
                continue;

            auto &device{at(id)};

            for (auto &controller : controllers) {
                if (newDevices[static_cast<size_t>(&controller - controllers.data())])
                    continue;

                NpadStyleSet style{};
                if (id != NpadId::Handheld) {
                    if (controller.type == NpadControllerType::ProController)
                        style.proController = true;
                    else if (controller.type == NpadControllerType::JoyconLeft)
                        style.joyconLeft = true;
                    else if (controller.type == NpadControllerType::JoyconRight)
                        style.joyconRight = true;
                    if (controller.type == NpadControllerType::JoyconDual || controller.partnerIndex != -1)
                        style.joyconDual = true;
                } else if (controller.type == NpadControllerType::Handheld) {
                    style.joyconHandheld = true;
                }
                style = NpadStyleSet{.raw = style.raw & styles.raw};

                if (style.raw) {
                    if (style.proController || style.joyconHandheld || style.joyconLeft || style.joyconRight) {
                        device.Connect(controller.type);
                        device.index = static_cast<i8>(&controller - controllers.data());
                        device.partnerIndex = -1;
                        newDevices[static_cast<size_t>(&controller - controllers.data())] = &device;
                    } else if (style.joyconDual && orientation == NpadJoyOrientation::Vertical && device.GetAssignment() == NpadJoyAssignment::Dual) {
                        device.Connect(NpadControllerType::JoyconDual);
                        device.index = static_cast<i8>(&controller - controllers.data());
                        device.partnerIndex = controller.partnerIndex;
                        newDevices[static_cast<size_t>(&controller - controllers.data())] = &device;
                        newDevices.at(static_cast<size_t>(controller.partnerIndex)) = &device;
                    } else {
                        continue;
                    }
                    break;
                }
            }
        }

        for (size_t i{}; i < controllers.size(); i++)
            controllers[i].device = newDevices[i];

        // We do this to prevent triggering the event unless there's a real change in a device's style, which would be caused if we disconnected all controllers then reconnected them
        for (auto &device : npads) {
            if (!ranges::any_of(controllers, [&](auto &controller) { return controller.device == &device; }))
                device.Disconnect();
        }

        // Diagnostics: only log when the mapping changes, this makes intermittent "controller not recognised" reports debuggable from Info level logs
        static std::string lastSummary;
        std::string summary;
        for (size_t i{}; i < controllers.size(); i++)
            summary += fmt::format("{}:{}{} ", i, static_cast<int>(controllers[i].type), controllers[i].device ? "+" : "-");
        summary = fmt::format("styles=0x{:X} supportedIds={} guestControllers=[{}]", styles.raw, supportedIds.size(), summary);
        if (summary != lastSummary) {
            LOGI("Npad mapping updated: {}", summary);
            lastSummary = std::move(summary);
        }
    }

    void NpadManager::Activate() {
        std::scoped_lock guard{mutex};
        if (!activated) {
            supportedIds = {NpadId::Handheld, NpadId::Player1, NpadId::Player2, NpadId::Player3, NpadId::Player4, NpadId::Player5, NpadId::Player6, NpadId::Player7, NpadId::Player8};
            styles = NpadStyleSet{}; // The padding bits of the union were left uninitialised by the designated initialiser (logs showed values like 0x8D001F), so the whole word is cleared first
            styles.proController = true;
            styles.joyconHandheld = true;
            styles.joyconDual = true;
            styles.joyconLeft = true;
            styles.joyconRight = true;
            activated = true;

            Update();
        }
    }

    void NpadManager::Deactivate() {
        std::scoped_lock guard{mutex};
        if (activated) {
            supportedIds = {};
            styles = {};
            activated = false;

            for (auto &npad : npads)
                npad.Disconnect();

            for (auto &controller : controllers)
                controller.device = nullptr;
        }
    }
}
