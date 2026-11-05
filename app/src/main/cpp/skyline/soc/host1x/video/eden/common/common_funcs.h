// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace eden_compat {
    // std::to_underlying is C++23
    template<typename Enum>
    constexpr std::underlying_type_t<Enum> to_underlying(Enum value) noexcept {
        return static_cast<std::underlying_type_t<Enum>>(value);
    }
}

#define EDEN_CONCAT2(x, y) EDEN_CONCAT2_(x, y)
#define EDEN_CONCAT2_(x, y) x##y

#define INSERT_PADDING_BYTES(num_bytes) std::uint8_t EDEN_CONCAT2(pad, __LINE__)[(num_bytes)]
#define INSERT_PADDING_BYTES_NOINIT(num_bytes) std::uint8_t EDEN_CONCAT2(pad, __LINE__)[(num_bytes)]
#define INSERT_PADDING_WORDS(num_words) std::uint32_t EDEN_CONCAT2(pad, __LINE__)[(num_words)]
#define INSERT_PADDING_WORDS_NOINIT(num_words) std::uint32_t EDEN_CONCAT2(pad, __LINE__)[(num_words)]

// The macro below is from common/common_funcs.h of Eden (GPL-3.0-or-later)
#define DECLARE_ENUM_FLAG_OPERATORS(type)                                                          \
    [[nodiscard]] constexpr type operator|(type a, type b) noexcept {                              \
        return type(eden_compat::to_underlying(a) | eden_compat::to_underlying(b));                                \
    }                                                                                              \
    [[nodiscard]] constexpr type operator&(type a, type b) noexcept {                              \
        return type(eden_compat::to_underlying(a) & eden_compat::to_underlying(b));                                \
    }                                                                                              \
    [[nodiscard]] constexpr type operator^(type a, type b) noexcept {                              \
        return type(eden_compat::to_underlying(a) ^ eden_compat::to_underlying(b));                                \
    }                                                                                              \
    [[nodiscard]] constexpr type operator<<(type a, type b) noexcept {                             \
        return type(eden_compat::to_underlying(a) << eden_compat::to_underlying(b));                               \
    }                                                                                              \
    [[nodiscard]] constexpr type operator>>(type a, type b) noexcept {                             \
        return type(eden_compat::to_underlying(a) >> eden_compat::to_underlying(b));                               \
    }                                                                                              \
    constexpr type operator|=(type& a, type b) noexcept {                                          \
        return a = a | b;                                                                          \
    }                                                                                              \
    constexpr type operator&=(type& a, type b) noexcept {                                          \
        return a = a & b;                                                                          \
    }                                                                                              \
    constexpr type operator^=(type& a, type b) noexcept {                                          \
        return a = a ^ b;                                                                          \
    }                                                                                              \
    constexpr type operator<<=(type& a, type b) noexcept {                                         \
        return a = a << b;                                                                         \
    }                                                                                              \
    constexpr type operator>>=(type& a, type b) noexcept {                                         \
        return a = a >> b;                                                                         \
    }                                                                                              \
    [[nodiscard]] constexpr type operator~(type key) noexcept {                                    \
        return type(~eden_compat::to_underlying(key));                                                     \
    }                                                                                              \
    [[nodiscard]] constexpr bool True(type key) noexcept {                                         \
        return eden_compat::to_underlying(key) != 0;                                                       \
    }                                                                                              \
    [[nodiscard]] constexpr bool False(type key) noexcept {                                        \
        return eden_compat::to_underlying(key) == 0;                                                       \
    }

#define YUZU_NON_COPYABLE(cls) \
    cls(const cls &) = delete; \
    cls &operator=(const cls &) = delete

#define YUZU_NON_MOVEABLE(cls) \
    cls(cls &&) = delete; \
    cls &operator=(cls &&) = delete
