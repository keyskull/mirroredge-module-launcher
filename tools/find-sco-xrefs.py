import struct, sys
path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Program Files (x86)\Steam\steamapps\common\mirrors edge\Binaries\MirrorsEdge.exe"
data = open(path, "rb").read()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
opt = e_lfanew + 24
image_base = struct.unpack_from("<I", data, opt + 28)[0]
nsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
ssize = struct.unpack_from("<H", data, e_lfanew + 20)[0]
sec0 = e_lfanew + 24 + ssize
secs = []
for i in range(nsec):
    off = sec0 + i * 40
    name = data[off:off+8].split(b"\0")[0].decode(errors="ignore")
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off+8)
    secs.append((name, va, vsize, rawptr, rawsize))

def file_to_rva(foff):
    for name, va, vsize, rawptr, rawsize in secs:
        if rawptr <= foff < rawptr + max(rawsize, 1):
            return va + (foff - rawptr)
    return None

def find_xrefs(label, s):
    foff = data.find(s)
    if foff < 0:
        print(label, "MISS")
        return
    rva = file_to_rva(foff)
    va = image_base + rva
    print(label, "foff", hex(foff), "va", hex(va))
    pat = struct.pack("<I", va)
    rawptr, rawsize = secs[0][3], secs[0][4]
    chunk = data[rawptr:rawptr+rawsize]
    hits = []
    idx = 0
    while True:
        j = chunk.find(pat, idx)
        if j < 0:
            break
        hits.append(rawptr + j)
        idx = j + 1
        if len(hits) > 20:
            break
    print("  hits", len(hits))
    for h in hits[:8]:
        print(" ", hex(h), data[h-16:h+6].hex())

for label, text in [
    ("Cannot create", "Cannot create"),
    ("NewObject", "NewObject"),
    ("Failed to create", "Failed to create"),
]:
    find_xrefs(label, text.encode("utf-16le"))
