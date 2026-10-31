import ctypes, os, struct, random, sys
OUT = sys.argv[1] if len(sys.argv) > 1 else "."
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
z = ctypes.CDLL("libzstd.so.1")  # sudo apt install libzstd1 python3-cryptography
z.ZSTD_compress.restype = ctypes.c_size_t
z.ZSTD_compress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
z.ZSTD_compressBound.restype = ctypes.c_size_t
z.ZSTD_compressBound.argtypes = [ctypes.c_size_t]
def zc(b):
    cap = z.ZSTD_compressBound(len(b)); out = ctypes.create_string_buffer(cap)
    n = z.ZSTD_compress(out, cap, b, len(b), 3); return out.raw[:n]
random.seed(7)
H = os.urandom(0x4000)
def comp(n):  # compressible data with some random runs
    parts=[]
    while sum(map(len,parts))<n:
        parts.append(bytes([random.randrange(256)])*random.randrange(1,600) if random.random()<.7 else os.urandom(random.randrange(1,3000)))
    return b"".join(parts)[:n]
secs=[(0x4000,0x30000,1),(0x34000,0x50123,3),(0x84123 + (0x10-0x84123%0x10)%0x10,0x2F010,4)]
# make offsets contiguous & first sections 0x200-ish aligned except test odd sizes
secs=[]; off=0x4000
for size,t in [(0x30000,1),(0x50200,3),(0x2F000,4),(0x1200,2)]:
    secs.append((off,size,t)); off+=size
end=off
keys=[os.urandom(16) for _ in secs]; ctrs=[os.urandom(8)+b"\0"*8 for _ in secs]
plain=comp(end-0x4000)
# make one random incompressible stretch to force raw blocks
rp=0x20000; plain=plain[:rp]+os.urandom(0x9000)+plain[rp+0x9000:]
orig=bytearray(H+plain)
for (o,s,t),k,c in zip(secs,keys,ctrs):
    if t in (3,4):
        aligned=o & ~0xF; assert aligned==o or True
        iv=c[:8]+struct.pack(">Q",o>>4)
        e=Cipher(algorithms.AES(k),modes.CTR(iv)).encryptor()
        orig[o:o+s]=e.update(bytes(orig[o:o+s]))
# NOTE: sections must be 16-aligned start for this generator; keep it
open(OUT + "/orig.nca","wb").write(orig)
BS=14; B=1<<BS
blocks=[]
for i in range(0,len(plain),B):
    ch=plain[i:i+B]; c=zc(ch)
    blocks.append(ch if len(c)>=len(ch) else c)
f=H+b"NCZSECTN"+struct.pack("<Q",len(secs))
for (o,s,t),k,c in zip(secs,keys,ctrs): f+=struct.pack("<QQQQ",o,s,t,0)+k+c
f+=b"NCZBLOCK"+bytes([2,1,0,BS])+struct.pack("<IQ",len(blocks),len(plain))
f+=b"".join(struct.pack("<I",len(b)) for b in blocks)+b"".join(blocks)
open(OUT + "/test.ncz","wb").write(f)
solid=H+b"NCZSECTN"+struct.pack("<Q",1)+struct.pack("<QQQQ",0x4000,len(plain),1,0)+bytes(32)+zc(plain)
open(OUT + "/solid.ncz","wb").write(solid)
print("raw blocks:",sum(1 for b in blocks if len(b)==B), "of",len(blocks), "nca size",len(orig))
