#!/bin/sh
# Testes que rodam no PC/CI (nao no Android), sem o emulador inteiro:
#   1. NczBacking: le um .ncz sintetico por blocos (zstd + AES-CTR) e compara com o NCA original
#   2. libopus multi-stream (5.1): o mesmo fluxo que o servico hwopus usa (tamanho, init em memoria crua, decodificar, resetar)
# Requisitos: gcc/g++ 13+, python3 com 'cryptography', libzstd (libzstd1; com libzstd-dev o cabecalho real e usado).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/app"
WORK="${TMPDIR:-/tmp}/mandacaru_host_tests"
rm -rf "$WORK" && mkdir -p "$WORK/mbed" "$WORK/opus" "$WORK/src"
SHIMS="$ROOT/tests/shims"
[ -f /usr/include/zstd.h ] && ZSTD_INC="" || ZSTD_INC="-I$SHIMS"
ZSTD_LIB="$(ls /usr/lib/*/libzstd.so* /usr/lib/libzstd.so* 2>/dev/null | head -1)"

echo "== mbedtls (so o AES/CTR e o que o cipher precisa)"
for f in aes aesni arc4 aria blowfish camellia ccm chacha20 chachapoly cipher cipher_wrap cmac des gcm nist_kw platform platform_util poly1305 xtea; do
  gcc -c -w -O1 -I"$APP/libraries/mbedtls/include" "$APP/libraries/mbedtls/library/$f.c" -o "$WORK/mbed/$f.o"
done

echo "== NCZ"
# Uma copia das fontes com o assembly do ARM trocado, para compilar no x86
cp -r "$APP/src/main/cpp" "$WORK/src/cpp"
sed -i 's/asm volatile("MRS %0, CNTFRQ_EL0" : "=r"(frequency));/frequency = 1;/; s/asm volatile("MRS %0, CNTVCT_EL0" : "=r"(ticks));/ticks = 1;/' "$WORK/src/cpp/skyline/common/utils.h"
BOOST=""; for d in "$APP"/libraries/boost/libs/*/include; do BOOST="$BOOST -I$d"; done
INC="-I$SHIMS $ZSTD_INC -I$WORK/src/cpp/skyline -I$WORK/src/cpp -I$APP/libraries/fmt/include -I$APP/libraries/frozen/include -I$APP/libraries/robin-map/include -I$APP/libraries/vkhpp -I$APP/libraries/vkhpp/Vulkan-Headers/include -I$APP/libraries/vkma/include -I$APP/libraries/range/include -I$APP/libraries/perfetto/sdk -I$APP/libraries/mbedtls/include -I$APP/libraries/thread-pool -I$APP/libraries/lz4/lib -I$APP/libraries/opus/include -I$APP/libraries/cubeb/include $BOOST"
python3 "$ROOT/tests/ncz/gen_ncz.py" "$WORK"
g++ -std=c++20 -O1 -DFMT_HEADER_ONLY -D__ANDROID__ -DPAGE_SIZE=4096 -include cstdint -include optional -include csignal -include sys/ucontext.h -Dsiginfo=siginfo_t -Ducontext=ucontext_t $INC \
  "$ROOT/tests/ncz/test_ncz.cpp" "$WORK/src/cpp/skyline/vfs/ncz_backing.cpp" "$WORK/src/cpp/skyline/crypto/aes_cipher.cpp" "$WORK"/mbed/*.o "$ZSTD_LIB" -o "$WORK/test_ncz" 2>&1 | grep -E "error|undefined" && exit 1
"$WORK/test_ncz" "$WORK" | tee "$WORK/ncz.out"
grep -q "FULL OK mismatches=0" "$WORK/ncz.out"
grep -q "solid rejected with code 6" "$WORK/ncz.out"

echo "== libopus multi-stream"
python3 - "$APP/libraries/opus" "$WORK/opus_srcs.txt" <<'PY'
import re, sys
root, out = sys.argv[1], sys.argv[2]
def lst(f, var):
    t = open(f"{root}/{f}").read().replace("\\\n", " ")
    m = re.search(r"^%s\s*=\s*(.*)$" % var, t, re.M)
    return m.group(1).split() if m else []
src = lst("celt_sources.mk", "CELT_SOURCES") + lst("silk_sources.mk", "SILK_SOURCES") + lst("silk_sources.mk", "SILK_SOURCES_FLOAT") + lst("opus_sources.mk", "OPUS_SOURCES") + lst("opus_sources.mk", "OPUS_SOURCES_FLOAT")
open(out, "w").write("\n".join(s for s in src if s.endswith(".c")))
PY
OPUS="$APP/libraries/opus"
for s in $(cat "$WORK/opus_srcs.txt"); do
  gcc -c -O1 -w -DOPUS_BUILD -DUSE_ALLOCA -DHAVE_LRINT -DHAVE_LRINTF -DPACKAGE_VERSION='"1.3"' -I"$OPUS" -I"$OPUS/include" -I"$OPUS/celt" -I"$OPUS/silk" -I"$OPUS/silk/float" -I"$OPUS/silk/fixed" "$OPUS/$s" -o "$WORK/opus/$(echo "$s" | tr '/' '_').o"
done
gcc -O1 -w -I"$OPUS/include" "$ROOT/tests/opus/test_multistream.c" "$WORK"/opus/*.o -lm -o "$WORK/test_multistream"
"$WORK/test_multistream" | tee "$WORK/opus.out"
grep -q "mismatches=0" "$WORK/opus.out"
echo "TODOS OS TESTES PASSARAM"
