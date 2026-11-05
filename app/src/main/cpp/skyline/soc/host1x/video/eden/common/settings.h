// SPDX-License-Identifier: MPL-2.0

#pragma once

namespace Settings {
    // The settings of Eden that the codec files read, the decoder here has no options
    enum class NvdecEmulation { Off, Cpu, Gpu };
    struct Values {
        struct Option {
            NvdecEmulation GetValue() const { return NvdecEmulation::Cpu; }
        } nvdec_emulation;
    };
    inline Values values;
}
