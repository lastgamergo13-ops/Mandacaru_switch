#include <fstream>
#include <string>
#include <random>
#include <iostream>
#include <vfs/ncz_backing.h>
#include <loader/loader.h>
using namespace skyline;
namespace skyline { bool AsyncLogger::CheckLogLevel(LogLevel) { return false; } void AsyncLogger::LogAsync(LogLevel, std::string &&, const char *) {} std::vector<void *> exception::GetStackFrames() { return {}; } }
struct FileBacking : vfs::Backing {
    std::vector<u8> data;
    FileBacking(const char *p) : Backing({true,false,false}, 0) { std::ifstream f(p, std::ios::binary); data.assign(std::istreambuf_iterator<char>(f), {}); size = data.size(); }
    size_t ReadImpl(span<u8> out, size_t off) override { size_t n = std::min(out.size(), data.size() - off); memcpy(out.data(), data.data() + off, n); return n; }
};
int main(int argc, char **argv) {
    std::string dir{argc > 1 ? argv[1] : "."};
    FileBacking orig((dir + "/orig.nca").c_str());
    auto ncz = vfs::NczBacking(std::make_shared<FileBacking>((dir + "/test.ncz").c_str()));
    if (ncz.size != orig.size) { std::cout << "SIZE MISMATCH " << ncz.size << " vs " << orig.size << "\n"; return 1; }
    std::mt19937_64 rng(1); int bad = 0;
    for (int i = 0; i < 4000; i++) {
        size_t off = rng() % orig.size, len = 1 + rng() % std::min<size_t>(70000, orig.size - off);
        if (i % 3 == 0) len = 1 + rng() % 40;
        std::vector<u8> a(len), b(len);
        ncz.Read(a, off);
        memcpy(b.data(), orig.data.data() + off, len);
        if (a != b) { if (bad++ < 5) std::cout << "MISMATCH off=" << off << " len=" << len << "\n"; }
    }
    std::vector<u8> all(orig.size); ncz.Read(all, 0);
    std::cout << (all == orig.data ? "FULL OK" : "FULL BAD") << " mismatches=" << bad << "\n";
    try { vfs::NczBacking s(std::make_shared<FileBacking>((dir + "/solid.ncz").c_str())); std::cout << "solid accepted?!\n"; }
    catch (const loader::loader_exception &e) { std::cout << "solid rejected with code " << int(e.error) << "\n"; }
}
