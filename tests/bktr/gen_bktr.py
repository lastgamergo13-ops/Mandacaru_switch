# Generates a synthetic patch section (update) the way the BKTR format describes it, to test BktrBacking:
#   section = [patch data][relocation table][padding][encryption subsection table]
#   - patch data and the relocation table are AES-CTR encrypted per subsection (every subsection has its own generation, some are not encrypted)
#   - the subsection table is AES-CTR encrypted with the generation of the section header
# Writes section.bin, base.bin, expected.bin and params.txt into the directory given as the first argument
import os, random, struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

out = sys.argv[1] if len(sys.argv) > 1 else "."
random.seed(11)
NODE = 0x4000

def ctr_crypt(key, secure, generation, abs_offset, data):
    # abs_offset is the offset in the NCA, the counter works on blocks of 16 bytes so an unaligned start is padded
    aligned = abs_offset & ~0xF
    pad = abs_offset - aligned
    iv = struct.pack(">II", secure, generation) + struct.pack(">Q", aligned >> 4)
    c = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
    return c.update(b"\0" * pad + data)[pad:]

def bucket_tree(entries, entry_size, end_offset):
    # entries: list of bytes (entry_size each, first 8 bytes = offset). Returns node storage + entry storage
    per_set = (NODE - 0x10) // entry_size
    sets = [entries[i:i + per_set] for i in range(0, len(entries), per_set)]
    first_offsets = [struct.unpack("<q", e[0][:8])[0] for e in sets]
    l1 = struct.pack("<iiq", 0, len(sets), end_offset) + b"".join(struct.pack("<q", o) for o in first_offsets)
    nodes = l1.ljust(NODE, b"\0")
    body = b""
    for i, s in enumerate(sets):
        end = first_offsets[i + 1] if i + 1 < len(sets) else end_offset
        node = struct.pack("<iiq", i, len(s), end) + b"".join(s)
        body += node.ljust(NODE, b"\0")
    return nodes + body

key = os.urandom(16)
secure = 0x1234ABCD
header_generation = 5
section_offset = 0x4000 + 0x200 * 7  # where the section starts in the "NCA"

base = os.urandom(0x30000)
patch_parts = []          # the data stored in the update
patch_len = 0
relocs = []               # (virtual offset, physical offset, storage index)
virtual = bytearray()
base_cursor = 0
# Alternate segments from the base and from the update, many small ones so several entry sets are needed
for i in range(2300):
    length = random.choice([16, 17, 31, 48, 64, 100, 200])
    virt = len(virtual)
    if random.random() < 0.5 and base_cursor + length < len(base):
        phys = random.randrange(0, len(base) - length)
        data = base[phys:phys + length]
        relocs.append((virt, phys, 0))
    else:
        phys = patch_len
        data = os.urandom(length)
        patch_parts.append(data)
        patch_len += length
        relocs.append((virt, phys, 1))
    virtual += data
virtual_size = len(virtual)
patch_data = b"".join(patch_parts)

# Layout of the section (offsets in the section)
indirect_offset = (patch_len + 0xFF) & ~0xFF
reloc_entries = [struct.pack("<qqi", v, p, s) for v, p, s in relocs]
reloc_table = bucket_tree(reloc_entries, 0x14, virtual_size)
indirect_size = len(reloc_table)
aes_ctr_ex_offset = (indirect_offset + indirect_size + 0x1FF) & ~0x1FF

# Encryption subsections cover [0, aes_ctr_ex_offset), boundaries on 16 byte blocks
plain_data = bytearray(patch_data.ljust(indirect_offset, b"\0") + reloc_table)
plain_data = plain_data.ljust(aes_ctr_ex_offset, b"\0")
bounds = [0]
while bounds[-1] < aes_ctr_ex_offset - 0x200:
    bounds.append(bounds[-1] + random.choice([0x10, 0x20, 0x30, 0x40, 0x50, 0x200]))
bounds.append(aes_ctr_ex_offset)
bounds = sorted(set(b for b in bounds if b <= aes_ctr_ex_offset))
sub_entries = []
enc = bytearray(plain_data)
for i, start in enumerate(bounds[:-1]):
    end = bounds[i + 1]
    encrypted = random.random() < 0.85
    gen = random.randrange(0, 4)
    sub_entries.append(struct.pack("<qBBBBi", start, 0 if encrypted else 1, 0, 0, 0, gen))
    if encrypted:
        enc[start:end] = ctr_crypt(key, secure, gen, section_offset + start, bytes(plain_data[start:end]))
subsection_table = bucket_tree(sub_entries, 0x10, aes_ctr_ex_offset)
aes_ctr_ex_size = len(subsection_table)
meta = ctr_crypt(key, secure, header_generation, section_offset + aes_ctr_ex_offset, subsection_table)

section = bytes(enc) + meta
open(out + "/section.bin", "wb").write(section)
open(out + "/base.bin", "wb").write(base)
open(out + "/expected.bin", "wb").write(bytes(virtual))
open(out + "/params.txt", "w").write(" ".join(map(str, [
    key.hex(), secure, header_generation, section_offset, indirect_offset, indirect_size, len(reloc_entries),
    aes_ctr_ex_offset, aes_ctr_ex_size, len(sub_entries)])))
print("reloc entries", len(reloc_entries), "sets", (len(reloc_entries) + 817) // 818, "subsections", len(sub_entries), "virtual size", virtual_size)
