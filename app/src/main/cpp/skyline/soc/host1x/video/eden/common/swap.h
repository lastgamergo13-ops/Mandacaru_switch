// SPDX-License-Identifier: MPL-2.0
// The endianness tags bit_field.h needs, the video structures are little endian like the CPU so a tag never changes the type

#pragma once

struct LETag {};
struct BETag {};

template <typename T, typename Tag>
struct AddEndian {
    using type = T;
};
