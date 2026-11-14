// Compara o decodificador BCn paralelo (DecodeBc1..7) com o serial: o resultado precisa ser identico byte a byte
// Inclui o .cpp para alcancar as funcoes *Serial (static).
#include <cstdio>
#include <random>
#include <vector>
#include <cstring>
#include "gpu/texture/bc_decoder.cpp"

// O linker pede isto (usado pela classe skyline::exception), o teste nunca lanca excecao do emulador
namespace skyline {
    std::vector<void *> exception::GetStackFrames() { return {}; }
}

using namespace bcn;

int main() {
    std::mt19937 rng{1234};
    int mismatches{}, cases{};
    struct Size { size_t w, h; };
    // Pequenas (caminho serial), grandes e com bordas parciais (largura/altura nao multiplas de 4)
    for (Size s : {Size{64, 64}, Size{256, 256}, Size{512, 512}, Size{1024, 1024}, Size{1000, 770}, Size{517, 1031}, Size{2048, 1030}, Size{4, 4096}, Size{3, 5}, Size{4097, 258}}) {
        size_t blocksX{(s.w + 3) / 4}, blocksY{(s.h + 3) / 4};
        for (int fmt{1}; fmt <= 7; fmt++) {
            size_t blockBytes{(fmt == 1 || fmt == 4) ? 8u : 16u};
            std::vector<uint8_t> src(blocksX * blocksY * blockBytes);
            for (auto &b : src) b = static_cast<uint8_t>(rng());
            size_t bpp{fmt == 4 ? 1u : fmt == 5 ? 2u : fmt == 6 ? 8u : 4u};
            std::vector<uint8_t> a(s.w * s.h * bpp, 0xAA), b(s.w * s.h * bpp, 0x55);
            for (int variant{}; variant < 2; variant++) {
                bool flag{variant == 1};
                std::fill(a.begin(), a.end(), 0xAA); std::fill(b.begin(), b.end(), 0x55);
                switch (fmt) {
                    case 1: DecodeBc1(src.data(), a.data(), s.w, s.h, flag); DecodeBc1Serial(src.data(), b.data(), s.w, s.h, flag); break;
                    case 2: DecodeBc2(src.data(), a.data(), s.w, s.h); DecodeBc2Serial(src.data(), b.data(), s.w, s.h); break;
                    case 3: DecodeBc3(src.data(), a.data(), s.w, s.h); DecodeBc3Serial(src.data(), b.data(), s.w, s.h); break;
                    case 4: DecodeBc4(src.data(), a.data(), s.w, s.h, flag); DecodeBc4Serial(src.data(), b.data(), s.w, s.h, flag); break;
                    case 5: DecodeBc5(src.data(), a.data(), s.w, s.h, flag); DecodeBc5Serial(src.data(), b.data(), s.w, s.h, flag); break;
                    case 6: DecodeBc6(src.data(), a.data(), s.w, s.h, flag); DecodeBc6Serial(src.data(), b.data(), s.w, s.h, flag); break;
                    case 7: DecodeBc7(src.data(), a.data(), s.w, s.h); DecodeBc7Serial(src.data(), b.data(), s.w, s.h); break;
                }
                cases++;
                if (a != b) { mismatches++; std::printf("MISMATCH fmt=BC%d %zux%zu flag=%d\n", fmt, s.w, s.h, flag); }
                if (fmt != 1 && fmt != 4 && fmt != 5 && fmt != 6) break; // sem flag
            }
        }
    }
    std::printf("cases=%d mismatches=%d\n", cases, mismatches);
    return mismatches ? 1 : 0;
}
