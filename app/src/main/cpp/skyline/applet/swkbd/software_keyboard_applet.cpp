// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2019-2022 Ryujinx Team and Contributors

#include <codecvt>
#include <services/am/storage/ObjIStorage.h>
#include <common/settings.h>
#include "software_keyboard_applet.h"
#include <jvm.h>

class Utf8Utf16Converter : public std::codecvt<char16_t, char8_t, std::mbstate_t> {
  public:
    ~Utf8Utf16Converter() override = default;
};

namespace skyline::applet::swkbd {
    static void WriteStringToSpan(span<u8> chars, std::u16string_view text, bool useUtf8Storage) {
        if (useUtf8Storage) {
            auto u8chars{chars.cast<char8_t>()};
            Utf8Utf16Converter::state_type convert_state;
            const char16_t *from_next;
            char8_t *to_next;
            Utf8Utf16Converter().out(convert_state, text.data(), text.end(), from_next, u8chars.data(), u8chars.end().base(), to_next);
            // Null terminate the string, if it isn't out of bounds
            if (to_next < reinterpret_cast<const char8_t *>(text.end()))
                *to_next = u8'\0';
        } else {
            std::memcpy(chars.data(), text.data(), std::min(text.size() * sizeof(char16_t), chars.size()));
            // Null terminate the string, if it isn't out of bounds
            if (text.size() * sizeof(char16_t) < chars.size())
                *(reinterpret_cast<char16_t *>(chars.data()) + text.size()) = u'\0';
        }
    }

    SoftwareKeyboardApplet::ValidationRequest::ValidationRequest(std::u16string_view text, bool useUtf8Storage) : size{sizeof(ValidationRequest)} {
        WriteStringToSpan(chars, text, useUtf8Storage);
    }

    SoftwareKeyboardApplet::OutputResult::OutputResult(CloseResult closeResult, std::u16string_view text, bool useUtf8Storage) : closeResult{closeResult} {
        WriteStringToSpan(chars, text, useUtf8Storage);
    }

    static std::u16string FillDefaultText(u32 minLength, u32 maxLength) {
        std::u16string text{u"Skyline"};
        while (text.size() < minLength)
            text += u"Emulator" + text;
        if (text.size() > maxLength)
            text.resize(maxLength);
        return text;
    }

    void SoftwareKeyboardApplet::SendResult() {
        if (dialog)
            state.jvm->CloseKeyboard(dialog);
        PushNormalDataAndSignal(std::make_shared<service::am::ObjIStorage<OutputResult>>(state, manager, OutputResult{currentResult, currentText, config.commonConfig.isUseUtf8}));
        onAppletStateChanged->Signal();
    }

    SoftwareKeyboardApplet::SoftwareKeyboardApplet(
        const DeviceState &state,
        service::ServiceManager &manager,
        std::shared_ptr<kernel::type::KEvent> onAppletStateChanged,
        std::shared_ptr<kernel::type::KEvent> onNormalDataPushFromApplet,
        std::shared_ptr<kernel::type::KEvent> onInteractiveDataPushFromApplet,
        service::applet::LibraryAppletMode appletMode)
        : IApplet{state,
                  manager,
                  std::move(onAppletStateChanged),
                  std::move(onNormalDataPushFromApplet),
                  std::move(onInteractiveDataPushFromApplet),
                  appletMode}, mode{appletMode} {
    }

    Result SoftwareKeyboardApplet::Start() {
        if (mode != service::applet::LibraryAppletMode::AllForeground) {
            // The InlineKeyboard is driven entirely through requests the guest pushes later, nothing is sent until then
            inlineState = InlineState::Uninitialized;
            return {};
        }

        std::scoped_lock lock{normalInputDataMutex};
        auto commonArgs{normalInputData.front()->GetSpan().as<service::applet::CommonArguments>()};
        normalInputData.pop();

        auto configSpan{normalInputData.front()->GetSpan()};
        normalInputData.pop();
        config = [&] {
            if (commonArgs.apiVersion < 0x30007)
                return KeyboardConfigVB{configSpan.as<KeyboardConfigV0>()};
            else if (commonArgs.apiVersion < 0x6000B)
                return KeyboardConfigVB{configSpan.as<KeyboardConfigV7>()};
            else
                return configSpan.as<KeyboardConfigVB>();
        }();
        LOGD("Swkbd Config:\n* KeyboardMode: {}\n* InvalidCharFlags: {:#09b}\n* TextMaxLength: {}\n* TextMinLength: {}\n* PasswordMode: {}\n* InputFormMode: {}\n* IsUseNewLine: {}\n* IsUseTextCheck: {}",
                      static_cast<u32>(config.commonConfig.keyboardMode),
                      config.commonConfig.invalidCharFlags.raw,
                      config.commonConfig.textMaxLength,
                      config.commonConfig.textMinLength,
                      static_cast<u32>(config.commonConfig.passwordMode),
                      static_cast<u32>(config.commonConfig.inputFormMode),
                      config.commonConfig.isUseNewLine,
                      config.commonConfig.isUseTextCheck
        );

        auto maxChars{static_cast<u32>(SwkbdTextBytes / (config.commonConfig.isUseUtf8 ? sizeof(char8_t) : sizeof(char16_t)))};
        config.commonConfig.textMaxLength = std::min(config.commonConfig.textMaxLength, maxChars);
        if (config.commonConfig.textMaxLength == 0)
            config.commonConfig.textMaxLength = maxChars;
        config.commonConfig.textMinLength = std::min(config.commonConfig.textMinLength, config.commonConfig.textMaxLength);

        if (config.commonConfig.textMaxLength > MaxOneLineChars)
            config.commonConfig.inputFormMode = InputFormMode::MultiLine;

        if (!normalInputData.empty() && config.commonConfig.initialStringLength > 0)
            currentText = std::u16string(normalInputData.front()->GetSpan().subspan(config.commonConfig.initialStringOffset).cast<char16_t>().data(), config.commonConfig.initialStringLength);

        dialog = state.jvm->ShowKeyboard(*reinterpret_cast<JvmManager::KeyboardConfig *>(&config), currentText);
        if (!dialog) {
            LOGW("Couldn't show keyboard dialog, using default text");
            currentResult = CloseResult::Enter;
            currentText = FillDefaultText(config.commonConfig.textMinLength, config.commonConfig.textMaxLength);
        } else {
            auto result{state.jvm->WaitForSubmitOrCancel(dialog)};
            currentResult = static_cast<CloseResult>(result.first);
            currentText = result.second;
        }
        if (config.commonConfig.isUseTextCheck && currentResult == CloseResult::Enter) {
            PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<ValidationRequest>>(state, manager, ValidationRequest{currentText, config.commonConfig.isUseUtf8}));
            validationPending = true;
        } else {
            SendResult();
        }
        return {};
    }

    Result SoftwareKeyboardApplet::GetResult() {
        return {};
    }

    void SoftwareKeyboardApplet::PushNormalDataToApplet(std::shared_ptr<service::am::IStorage> data) {
        PushNormalInput(data);
    }

    void SoftwareKeyboardApplet::PushInteractiveDataToApplet(std::shared_ptr<service::am::IStorage> data) {
        if (mode != service::applet::LibraryAppletMode::AllForeground) {
            ProcessInlineRequest(data->GetSpan());
            return;
        }

        if (validationPending) {
            auto dataSpan{data->GetSpan()};
            auto validationResult{dataSpan.as<ValidationResult>()};
            if (validationResult.result == TextCheckResult::Success) {
                validationPending = false;
                SendResult();
            } else {
                if (dialog) {
                    if (static_cast<CloseResult>(state.jvm->ShowValidationResult(dialog, static_cast<JvmManager::KeyboardTextCheckResult>(validationResult.result), std::u16string(validationResult.chars.data()))) == CloseResult::Enter) {
                        // Accepted on confirmation dialog
                        validationPending = false;
                        SendResult();
                    } else {
                        // Cancelled or failed validation, go back to waiting for text
                        auto result{state.jvm->WaitForSubmitOrCancel(dialog)};
                        currentResult = static_cast<CloseResult>(result.first);
                        currentText = result.second;
                        if (currentResult == CloseResult::Enter) {
                            PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<ValidationRequest>>(state, manager, ValidationRequest{currentText, config.commonConfig.isUseUtf8}));
                        } else {
                            SendResult();
                        }
                    }
                } else {
                    std::array<u8, SwkbdTextBytes> chars{};
                    WriteStringToSpan(chars, std::u16string(validationResult.chars.data()), true);
                    std::string message{reinterpret_cast<char *>(chars.data())};
                    if (validationResult.result == TextCheckResult::ShowFailureDialog)
                        LOGW("Sending default text despite being rejected by the guest with message: \"{}\"", message);
                    else
                        LOGD("Guest asked to confirm default text with message: \"{}\"", message);
                    PushNormalDataAndSignal(std::make_shared<service::am::ObjIStorage<OutputResult>>(state, manager, OutputResult{CloseResult::Enter, currentText, config.commonConfig.isUseUtf8}));
                }
            }
        }
    }

    static std::u16string Utf8ToUtf16(std::string_view in) {
        std::u16string out;
        for (size_t i{}; i < in.size();) {
            u8 c{static_cast<u8>(in[i])};
            u32 codePoint;
            size_t length;
            if (c < 0x80) {
                codePoint = c;
                length = 1;
            } else if ((c >> 5) == 0x6) {
                codePoint = c & 0x1F;
                length = 2;
            } else if ((c >> 4) == 0xE) {
                codePoint = c & 0xF;
                length = 3;
            } else if ((c >> 3) == 0x1E) {
                codePoint = c & 0x7;
                length = 4;
            } else {
                i++; // Invalid lead byte
                continue;
            }

            if (i + length > in.size())
                break;
            for (size_t k{1}; k < length; k++)
                codePoint = (codePoint << 6) | (static_cast<u8>(in[i + k]) & 0x3F);
            i += length;

            if (codePoint >= 0x10000) {
                codePoint -= 0x10000;
                out += static_cast<char16_t>(0xD800 + (codePoint >> 10));
                out += static_cast<char16_t>(0xDC00 + (codePoint & 0x3FF));
            } else {
                out += static_cast<char16_t>(codePoint);
            }
        }
        return out;
    }

    void SoftwareKeyboardApplet::ProcessInlineRequest(span<u8> request) {
        if (request.size() < sizeof(u32))
            return;

        switch (static_cast<InlineRequest>(request.as<u32>())) {
            case InlineRequest::Finalize:
                // The guest is done with the keyboard, this makes the applet exit
                inlineState = InlineState::Uninitialized;
                if (dialog) {
                    state.jvm->CloseKeyboard(dialog);
                    dialog = nullptr;
                }
                onAppletStateChanged->Signal();
                break;

            case InlineRequest::Calc:
                ProcessInlineCalc(request.subspan(sizeof(u32)));
                break;

            default:
                LOGD("Ignoring InlineKeyboard request 0x{:X}", request.as<u32>());
                break;
        }
    }

    void SoftwareKeyboardApplet::ProcessInlineCalc(span<u8> calc) {
        if (calc.size() < InlineCalcSize)
            return;

        // Offsets are relative to the CalcArg, see https://switchbrew.org/wiki/Software_Keyboard#CalcArg
        u64 flags{calc.subspan(0x8).as<u64>()};
        constexpr u64 InitializeFlag{0x1}, AppearFlag{0x4}, SetInputTextFlag{0x8}, SetUtf8ModeFlag{0x20};

        if (flags & SetUtf8ModeFlag)
            inlineUtf8 = calc[0x45C] != 0;

        if (flags & SetInputTextFlag) {
            auto textSpan{calc.subspan(0x68, 0x3F4)};
            if (inlineUtf8) {
                std::string_view utf8{reinterpret_cast<const char *>(textSpan.data()), textSpan.size()};
                currentText = Utf8ToUtf16(utf8.substr(0, utf8.find('\0')));
            } else {
                auto chars{textSpan.cast<char16_t>()};
                size_t length{};
                while (length < chars.size() && chars[length] != u'\0')
                    length++;
                currentText.assign(chars.data(), length);
            }
        }

        if ((flags & InitializeFlag) && inlineState == InlineState::Uninitialized) {
            inlineState = InlineState::Initialized;
            SendInlineReply(InlineReply::FinishedInitialize, inlineState);
        }

        if (flags & AppearFlag)
            ShowInlineKeyboard();
    }

    void SoftwareKeyboardApplet::ShowInlineKeyboard() {
        inlineState = InlineState::Ready;
        SendInlineReply(InlineReply::Default, inlineState);

        // The guest doesn't tell us the limits so use the biggest text an inline reply can carry, the guest rejects what it can't use
        KeyboardConfigVB inlineConfig{};
        inlineConfig.commonConfig.keyboardMode = KeyboardMode::Full;
        inlineConfig.commonConfig.textMaxLength = 250;
        inlineConfig.commonConfig.textMinLength = 0;
        inlineConfig.commonConfig.passwordMode = PasswordMode::Show;
        inlineConfig.commonConfig.inputFormMode = InputFormMode::OneLine;
        inlineConfig.commonConfig.isUseUtf8 = inlineUtf8;
        inlineConfig.commonConfig.isUseNewLine = false;

        CloseResult result;
        std::u16string text{currentText};
        dialog = state.jvm->ShowKeyboard(*reinterpret_cast<JvmManager::KeyboardConfig *>(&inlineConfig), currentText);
        if (!dialog) {
            LOGW("Couldn't show the InlineKeyboard dialog, accepting the initial text");
            result = CloseResult::Enter;
        } else {
            auto dialogResult{state.jvm->WaitForSubmitOrCancel(dialog)};
            result = static_cast<CloseResult>(dialogResult.first);
            text = dialogResult.second;
            state.jvm->CloseKeyboard(dialog);
            dialog = nullptr;
        }

        if (result == CloseResult::Enter) {
            currentText = text;
            SendInlineText(InlineReply::ChangedString, text, InlineState::DataAvailable);
            inlineState = InlineState::Completed;
            SendInlineText(InlineReply::DecidedEnter, text, inlineState);
        } else {
            inlineState = InlineState::Completed;
            SendInlineReply(InlineReply::DecidedCancel, inlineState);
        }

        inlineState = InlineState::Ready;
    }

    void SoftwareKeyboardApplet::SendInlineReply(InlineReply type, InlineState replyState) {
        if (type == InlineReply::FinishedInitialize) {
            InlineReplyFinishedInitialize reply{};
            reply.base = {static_cast<u32>(replyState), static_cast<u32>(type)};
            PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyFinishedInitialize>>(state, manager, std::move(reply)));
        } else {
            InlineReplyBase reply{static_cast<u32>(replyState), static_cast<u32>(type)};
            PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyBase>>(state, manager, std::move(reply)));
        }
    }

    void SoftwareKeyboardApplet::SendInlineText(InlineReply type, std::u16string_view text, InlineState replyState) {
        auto write{[&](auto reply, u32 textBytes) {
            WriteStringToSpan(span<u8>(reply.chars).first(textBytes - sizeof(char16_t)), text, inlineUtf8);
            // Length in characters without the terminator
            reply.length = inlineUtf8 ? static_cast<u32>(strnlen(reinterpret_cast<const char *>(reply.chars.data()), textBytes)) : static_cast<u32>(std::min<size_t>(text.size(), textBytes / sizeof(char16_t) - 1));
            reply.cursorPos = 0;
            return reply;
        }};

        // Both replies carry the cursor position/length after the string, DecidedEnter only has the length
        if (type == InlineReply::ChangedString) {
            if (inlineUtf8) {
                InlineReplyChangedStringUtf8 reply{};
                reply.base = {static_cast<u32>(replyState), static_cast<u32>(InlineReply::ChangedStringUtf8)};
                reply = write(reply, InlineUtf8TextBytes);
                reply.cursorPos = reply.length;
                PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyChangedStringUtf8>>(state, manager, std::move(reply)));
            } else {
                InlineReplyChangedStringUtf16 reply{};
                reply.base = {static_cast<u32>(replyState), static_cast<u32>(InlineReply::ChangedString)};
                reply = write(reply, InlineUtf16TextBytes);
                reply.cursorPos = reply.length;
                PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyChangedStringUtf16>>(state, manager, std::move(reply)));
            }
        } else {
            if (inlineUtf8) {
                InlineReplyDecidedEnterUtf8 reply{};
                reply.base = {static_cast<u32>(replyState), static_cast<u32>(InlineReply::DecidedEnterUtf8)};
                auto written{[&] {
                    WriteStringToSpan(span<u8>(reply.chars).first(InlineUtf8TextBytes - 1), text, true);
                    return static_cast<u32>(strnlen(reinterpret_cast<const char *>(reply.chars.data()), InlineUtf8TextBytes));
                }()};
                reply.length = written;
                PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyDecidedEnterUtf8>>(state, manager, std::move(reply)));
            } else {
                InlineReplyDecidedEnterUtf16 reply{};
                reply.base = {static_cast<u32>(replyState), static_cast<u32>(InlineReply::DecidedEnter)};
                WriteStringToSpan(span<u8>(reply.chars).first(InlineUtf16TextBytes - sizeof(char16_t)), text, false);
                reply.length = static_cast<u32>(std::min<size_t>(text.size(), InlineUtf16TextBytes / sizeof(char16_t) - 1));
                PushInteractiveDataAndSignal(std::make_shared<service::am::ObjIStorage<InlineReplyDecidedEnterUtf16>>(state, manager, std::move(reply)));
            }
        }
    }
}
