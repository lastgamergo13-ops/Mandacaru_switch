// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <services/am/applet/IApplet.h>
#include <services/applet/common_arguments.h>
#include <jvm.h>
#include "software_keyboard_config.h"

namespace skyline::applet::swkbd {
    static_assert(sizeof(KeyboardConfigVB) == sizeof(JvmManager::KeyboardConfig));

    /**
     * @url https://switchbrew.org/wiki/Software_Keyboard
     * @brief An implementation for the Software Keyboard (swkbd) Applet which handles translating guest applet transactions to the appropriate host behavior
     */
    class SoftwareKeyboardApplet : public service::am::IApplet, service::am::EnableNormalQueue {
      private:
        /**
         * @url https://switchbrew.org/wiki/Software_Keyboard#CloseResult
         */
        enum class CloseResult : u32 {
            Enter = 0x0,
            Cancel = 0x1,
        };

        /**
         * @url https://switchbrew.org/wiki/Software_Keyboard#TextCheckResult
         */
        enum class TextCheckResult : u32 {
            Success = 0x0,
            ShowFailureDialog = 0x1,
            ShowConfirmDialog = 0x2,
        };

        static constexpr u32 SwkbdTextBytes{0x7D4}; //!< Size of the returned IStorage buffer that's used to return the input text

        static constexpr u32 MaxOneLineChars{32}; //!< The maximum number of characters for which anything other than InputFormMode::MultiLine is used

        /**
         * @brief The state of the InlineKeyboard, this is the first u32 of every reply sent to the guest
         * @url https://switchbrew.org/wiki/Software_Keyboard#InlineKeyboard
         */
        enum class InlineState : u32 {
            Uninitialized = 0x0,
            Initialized = 0x1,
            Ready = 0x2,
            DataAvailable = 0x3,
            Completed = 0x4,
        };

        /**
         * @brief The commands the guest sends to the InlineKeyboard through the interactive queue (the first u32 of the storage)
         */
        enum class InlineRequest : u32 {
            Finalize = 0x4,
            SetUserWordInfo = 0x6,
            SetCustomizeDic = 0x7,
            Calc = 0xA,
            SetCustomizedDictionaries = 0xB,
            UnsetCustomizedDictionaries = 0xC,
        };

        /**
         * @brief The replies the InlineKeyboard sends to the guest (the second u32 of the storage)
         */
        enum class InlineReply : u32 {
            FinishedInitialize = 0x0,
            Default = 0x1,
            ChangedString = 0x2,
            MovedCursor = 0x3,
            MovedTab = 0x4,
            DecidedEnter = 0x5,
            DecidedCancel = 0x6,
            ChangedStringUtf8 = 0x7,
            MovedCursorUtf8 = 0x8,
            DecidedEnterUtf8 = 0x9,
        };

        static constexpr u32 InlineCalcSize{0x4A0}; //!< The size of the CalcArg that follows the request command
        static constexpr u32 InlineUtf16TextBytes{0x3EC}; //!< The size of the string buffer in UTF-16 replies
        static constexpr u32 InlineUtf8TextBytes{0x7D4}; //!< The size of the string buffer in UTF-8 replies

        #pragma pack(push, 1)

        /**
         * @brief The final result after the swkbd has closed
         */
        struct OutputResult {
            CloseResult closeResult;
            std::array<u8, SwkbdTextBytes> chars{};

            OutputResult(CloseResult closeResult, std::u16string_view text, bool useUtf8Storage);
        };
        static_assert(sizeof(OutputResult) == 0x7D8);

        /**
         * @brief A request for validating a string inside guest code, this is pushed via the interactive queue
         */
        struct ValidationRequest {
            u64 size;
            std::array<u8, SwkbdTextBytes> chars{};

            ValidationRequest(std::u16string_view text, bool useUtf8Storage);
        };
        static_assert(sizeof(ValidationRequest) == 0x7DC);

        /**
         * @brief The result of validating text submitted to the guest
         */
        struct ValidationResult {
            TextCheckResult result;
            std::array<char16_t, SwkbdTextBytes / sizeof(char16_t)> chars;
        };
        static_assert(sizeof(ValidationResult) == 0x7D8);

        struct InlineReplyBase {
            u32 state;
            u32 type;
        };
        static_assert(sizeof(InlineReplyBase) == 0x8);

        struct InlineReplyFinishedInitialize {
            InlineReplyBase base;
            u8 data;
        };
        static_assert(sizeof(InlineReplyFinishedInitialize) == 0x9);

        struct InlineReplyChangedStringUtf16 {
            InlineReplyBase base;
            std::array<u8, InlineUtf16TextBytes> chars;
            u32 length;
            i32 _unk0_;
            i32 _unk1_;
            u32 cursorPos;
        };
        static_assert(sizeof(InlineReplyChangedStringUtf16) == 0x8 + 0x3FC);

        struct InlineReplyChangedStringUtf8 {
            InlineReplyBase base;
            std::array<u8, InlineUtf8TextBytes> chars;
            u32 length;
            i32 _unk0_;
            i32 _unk1_;
            u32 cursorPos;
        };
        static_assert(sizeof(InlineReplyChangedStringUtf8) == 0x8 + 0x7E4);

        struct InlineReplyDecidedEnterUtf16 {
            InlineReplyBase base;
            std::array<u8, InlineUtf16TextBytes> chars;
            u32 length;
        };
        static_assert(sizeof(InlineReplyDecidedEnterUtf16) == 0x8 + 0x3F0);

        struct InlineReplyDecidedEnterUtf8 {
            InlineReplyBase base;
            std::array<u8, InlineUtf8TextBytes> chars;
            u32 length;
        };
        static_assert(sizeof(InlineReplyDecidedEnterUtf8) == 0x8 + 0x7D8);

        #pragma pack(pop)

        KeyboardConfigVB config{};
        service::applet::LibraryAppletMode mode{};
        bool validationPending{};
        std::u16string currentText{};
        CloseResult currentResult{};

        jobject dialog{};

        void SendResult();

        InlineState inlineState{}; //!< The current state of the InlineKeyboard
        bool inlineUtf8{}; //!< If the guest wants the text of the InlineKeyboard in UTF-8 instead of UTF-16

        /**
         * @brief Handles a request the guest pushed to the InlineKeyboard
         */
        void ProcessInlineRequest(span<u8> request);

        /**
         * @brief Handles the Calc request, it carries what the guest wants the InlineKeyboard to do (initialize, show, change the text...)
         */
        void ProcessInlineCalc(span<u8> calc);

        /**
         * @brief Shows the host keyboard dialog and sends the typed text back to the guest as InlineKeyboard replies
         */
        void ShowInlineKeyboard();

        /**
         * @brief Sends a reply that doesn't carry any data
         */
        void SendInlineReply(InlineReply type, InlineState replyState);

        /**
         * @brief Sends a ChangedString or DecidedEnter reply with the given text (picking the UTF-8 or UTF-16 variant the guest asked for)
         */
        void SendInlineText(InlineReply type, std::u16string_view text, InlineState replyState);

      public:
        SoftwareKeyboardApplet(const DeviceState &state, service::ServiceManager &manager, std::shared_ptr<kernel::type::KEvent> onAppletStateChanged, std::shared_ptr<kernel::type::KEvent> onNormalDataPushFromApplet, std::shared_ptr<kernel::type::KEvent> onInteractiveDataPushFromApplet, service::applet::LibraryAppletMode appletMode);

        Result Start() override;

        Result GetResult() override;

        void PushNormalDataToApplet(std::shared_ptr<service::am::IStorage> data) override;

        void PushInteractiveDataToApplet(std::shared_ptr<service::am::IStorage> data) override;
    };
}
