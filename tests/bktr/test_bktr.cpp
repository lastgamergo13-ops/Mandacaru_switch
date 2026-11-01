// Reads a synthetic patch section with BktrBacking and compares it with the expected content (see gen_bktr.py)
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vfs/bktr_backing.h>
#include <loader/loader.h>
using namespace skyline;
namespace skyline { bool AsyncLogger::CheckLogLevel(LogLevel) { return false; } void AsyncLogger::LogAsync(LogLevel, std::string &&, const char *) {} std::vector<void *> exception::GetStackFrames() { return {}; } }
struct FileBacking : vfs::Backing {
    std::vector<u8> data;
    FileBacking(const std::string &p) : Backing({true, false, false}, 0) { std::ifstream f(p, std::ios::binary); data.assign(std::istreambuf_iterator<char>(f), {}); size = data.size(); }
    size_t ReadImpl(span<u8> out, size_t off) override { size_t n = std::min(out.size(), data.size() - off); memcpy(out.data(), data.data() + off, n); return n; }
};
int main(int argc, char **argv) {
    std::string dir{argc > 1 ? argv[1] : "."};
    auto section = std::make_shared<FileBacking>(dir + "/section.bin");
    auto base = std::make_shared<FileBacking>(dir + "/base.bin");
    FileBacking expected(dir + "/expected.bin");
    std::ifstream pf(dir + "/params.txt"); std::string keyHex; u64 secure, gen, sectionOffset, ind, indSize, indCount, ctr, ctrSize, ctrCount;
    pf >> keyHex >> secure >> gen >> sectionOffset >> ind >> indSize >> indCount >> ctr >> ctrSize >> ctrCount;
    vfs::BktrParameters p{};
    p.section = section; p.encrypted = true; p.key = util::HexStringToArray<16>(keyHex);
    p.secureValue = static_cast<u32>(secure); p.generation = static_cast<u32>(gen); p.sectionOffset = sectionOffset;
    p.indirectOffset = ind; p.indirectSize = indSize; p.indirectEntryCount = static_cast<i32>(indCount);
    p.aesCtrExOffset = ctr; p.aesCtrExSize = ctrSize; p.aesCtrExEntryCount = static_cast<i32>(ctrCount);
    vfs::BktrBacking bktr(p, base);
    if (bktr.size != expected.size) { std::cout << "SIZE MISMATCH " << bktr.size << " vs " << expected.size << "\n"; return 1; }
    std::mt19937_64 rng(3); int bad = 0;
    for (int i = 0; i < 5000; i++) {
        size_t off = rng() % expected.size, len = 1 + rng() % std::min<size_t>(i % 4 == 0 ? 40 : 30000, expected.size - off);
        std::vector<u8> a(len); bktr.Read(a, off);
        if (memcmp(a.data(), expected.data.data() + off, len) != 0 && bad++ < 5) std::cout << "MISMATCH off=" << off << " len=" << len << "\n";
    }
    std::vector<u8> all(expected.size); bktr.Read(all, 0);
    std::cout << (all == expected.data ? "FULL OK" : "FULL BAD") << " mismatches=" << bad << "\n";
    // Without the base game the ranges that come from it are zeroes and the rest is still right
    vfs::BktrBacking withoutBase(p, nullptr);
    std::vector<u8> partial(expected.size); withoutBase.Read(partial, 0);
    std::cout << "without base reads " << partial.size() << " bytes\n";
}
