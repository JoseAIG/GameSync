"""Test fixture for ps5/src/xbox_hdd.c: a qcow2 Xbox disk with a FATX E:.

    python xbox_hdd_fixture.py make <image.qcow2>   # writes the image
    python xbox_hdd_fixture.py check <listing>       # checks test_xbox_hdd's output
    python xbox_hdd_fixture.py check-image <image>   # refcounts, after the writes

The disk is sparse: only the E: superblock, its FAT and a few data clusters
are allocated. Data clusters are stored zlib-compressed, the way qemu-img
convert -c writes them, so both cluster kinds get read. FATX layout as
documented on https://xboxdevwiki.net/FATX.
"""
import collections
import hashlib
import struct
import sys
import zlib

QCOW_CLUSTER_BITS = 16
QCOW_CLUSTER = 1 << QCOW_CLUSTER_BITS
DISK_SIZE = 8 << 30
E_OFFSET = 0xABE80000
E_SIZE = 0x1312D6000
SPC = 32
FATX_CLUSTER = SPC * 512
CLUSTERS = E_SIZE // FATX_CLUSTER
FAT_ENTRY = 2 if CLUSTERS < 0xFFF0 else 4
FAT_BYTES = (CLUSTERS * FAT_ENTRY + 4095) // 4096 * 4096
DATA = E_OFFSET + 0x1000 + FAT_BYTES
END = 0xFFFFFFFF

BIG = bytes((i * 7) & 0xFF for i in range(FATX_CLUSTER + 5000))  # two clusters
FILES = {  # path in the save -> contents
    "SaveMeta.xbx": b"Title=Fixture\r\n",
    "AB12CD34EF56\\big.dat": BIG,
    "AB12CD34EF56\\SaveMeta.xbx": b"Name=Slot 1\r\n",
    "empty.bin": b"",
}


def dirent(name, attr, first, size):
    raw = name.encode("latin1")
    return struct.pack("<BB42sII", len(raw), attr, raw.ljust(42, b"\xff"), first, size) + bytes(12)


def deleted(name):
    e = bytearray(dirent(name, 0, 0, 0))
    e[0] = 0xE5
    return bytes(e)


def build_guest():
    """guest offset -> bytes, and the FAT."""
    fat = {}
    writes = {}

    def put_cluster(c, data):
        writes[DATA + (c - 1) * FATX_CLUSTER] = data.ljust(FATX_CLUSTER, b"\x00")

    def chain(clusters):
        for a, b in zip(clusters, clusters[1:]):
            fat[a] = b
        fat[clusters[-1]] = END

    # 1 root, 2 UDATA, 3 the game, 4 TDSV0000 (not a game id), 5 save dir,
    # 6 SaveMeta.xbx, 7+9 big.dat (not contiguous), 8 the slot's SaveMeta,
    # 10 another title
    put_cluster(1, dirent("UDATA", 0x10, 2, 0) + dirent("TDATA", 0x10, 10, 0) + b"\xff" * 64)
    put_cluster(2, deleted("4D530001") + dirent("4d53006e", 0x10, 3, 0) +
                dirent("TDSV0000", 0x10, 4, 0) + b"\x00" * 64)
    put_cluster(3, dirent("SaveMeta.xbx", 0, 6, len(FILES["SaveMeta.xbx"])) +
                dirent("AB12CD34EF56", 0x10, 5, 0) + dirent("empty.bin", 0, 0, 0) + b"\xff" * 64)
    put_cluster(4, b"\xff" * 64)
    put_cluster(5, dirent("big.dat", 0, 7, len(BIG)) +
                dirent("SaveMeta.xbx", 0, 8, len(FILES["AB12CD34EF56\\SaveMeta.xbx"])) +
                b"\xff" * 64)
    put_cluster(6, FILES["SaveMeta.xbx"])
    put_cluster(7, BIG[:FATX_CLUSTER])
    put_cluster(9, BIG[FATX_CLUSTER:])
    put_cluster(8, FILES["AB12CD34EF56\\SaveMeta.xbx"])
    put_cluster(10, b"\xff" * 64)
    for c in (1, 2, 3, 4, 5, 6, 8, 10):
        chain([c])
    chain([7, 9])
    fat_bytes = bytearray(FAT_BYTES)
    fmt = "<H" if FAT_ENTRY == 2 else "<I"
    for c, n in fat.items():
        struct.pack_into(fmt, fat_bytes, c * FAT_ENTRY, n & (0xFFFF if FAT_ENTRY == 2 else END))
    writes[E_OFFSET] = b"FATX" + struct.pack("<III", 0x1234, SPC, 1) + bytes(4080)
    writes[E_OFFSET + 0x1000] = bytes(fat_bytes)
    return writes


def make(path):
    guest = {}  # qcow2 cluster index -> bytearray
    for off, data in build_guest().items():
        while data:
            vc, inner = divmod(off, QCOW_CLUSTER)
            k = min(len(data), QCOW_CLUSTER - inner)
            buf = guest.setdefault(vc, bytearray(QCOW_CLUSTER))
            buf[inner:inner + k] = data[:k]
            off += k
            data = data[k:]
    per_l2 = QCOW_CLUSTER // 8
    l1_size = (DISK_SIZE // QCOW_CLUSTER + per_l2 - 1) // per_l2
    out = bytearray(QCOW_CLUSTER * 2)  # header, L1
    l2_tables = {}

    def alloc(data):
        while len(out) % QCOW_CLUSTER:
            out.append(0)
        at = len(out)
        out.extend(data)
        return at

    for i1 in sorted({vc // per_l2 for vc in guest}):
        l2_tables[i1] = [alloc(bytes(QCOW_CLUSTER)), [0] * per_l2]
    x = 62 - (QCOW_CLUSTER_BITS - 8)
    for vc, data in sorted(guest.items()):
        i1, i2 = divmod(vc, per_l2)
        if vc * QCOW_CLUSTER >= DATA:
            co = zlib.compressobj(9, zlib.DEFLATED, -12)
            packed = co.compress(bytes(data)) + co.flush()
            at = len(out) + 3  # not sector-aligned, as qemu packs them
            out.extend(bytes(3))
            out.extend(packed)
            sectors = ((at & 511) + len(packed) + 511) // 512
            l2_tables[i1][1][i2] = (1 << 62) | ((sectors - 1) << x) | at
        else:
            l2_tables[i1][1][i2] = alloc(bytes(data)) | (1 << 63)  # COPIED: refcount 1
    while len(out) % 512:
        out.append(0)
    l1 = [0] * l1_size
    for i1, (at, entries) in l2_tables.items():
        out[at:at + QCOW_CLUSTER] = struct.pack(">%dQ" % per_l2, *entries)
        l1[i1] = at | (1 << 63)
    out[QCOW_CLUSTER:QCOW_CLUSTER + 8 * l1_size] = struct.pack(">%dQ" % l1_size, *l1)
    rt = alloc(bytes(QCOW_CLUSTER))
    block = alloc(bytes(QCOW_CLUSTER))
    out[rt:rt + 8] = struct.pack(">Q", block)
    header = struct.pack(">4sIQIIQIIQQIIQQQQII", b"QFI\xfb", 3, 0, 0, QCOW_CLUSTER_BITS,
                         DISK_SIZE, 0, l1_size, QCOW_CLUSTER, rt, 1, 0, 0, 0, 0, 0, 4, 104)
    out[:len(header)] = header
    for hc, n in references(bytes(out)).items():
        struct.pack_into(">H", out, block + hc * 2, n)
    with open(path, "wb") as f:
        f.write(out)


def references(img):
    """host cluster -> how many references the metadata makes to it"""
    refs = collections.Counter()
    cbits = struct.unpack_from(">I", img, 20)[0]
    cs = 1 << cbits
    l1_size, l1_off, rt_off, rt_clusters = struct.unpack_from(">IQQI", img, 36)
    mask = 0x00FFFFFFFFFFFE00
    refs[0] += 1
    for c in range((l1_size * 8 + cs - 1) // cs):
        refs[l1_off // cs + c] += 1
    for c in range(rt_clusters):
        refs[rt_off // cs + c] += 1
    for i in range(rt_clusters * cs // 8):
        b = struct.unpack_from(">Q", img, rt_off + i * 8)[0] & mask
        if b:
            refs[b // cs] += 1
    x = 62 - (cbits - 8)
    for i1 in range(l1_size):
        l2 = struct.unpack_from(">Q", img, l1_off + i1 * 8)[0] & mask
        if not l2:
            continue
        refs[l2 // cs] += 1
        for i2 in range(cs // 8):
            e = struct.unpack_from(">Q", img, l2 + i2 * 8)[0]
            if e & (1 << 62):
                off = e & ((1 << x) - 1)
                sectors = ((e >> x) & ((1 << (cbits - 8)) - 1)) + 1
                start = off & ~511
                for hc in range(start // cs, (start + sectors * 512 - 1) // cs + 1):
                    refs[hc] += 1
            elif e & mask:
                refs[(e & mask) // cs] += 1
    return refs


def check_image(path):
    """What matters of qemu-img check: every cluster's refcount matches its
    references (a leftover count with no reference is a harmless leak) and
    nothing points past the end of the file."""
    img = open(path, "rb").read()
    cs = 1 << struct.unpack_from(">I", img, 20)[0]
    rt_off, rt_clusters = struct.unpack_from(">QI", img, 48)
    per_block = cs // 2
    stored = {}
    for ri in range(rt_clusters * cs // 8):
        b = struct.unpack_from(">Q", img, rt_off + ri * 8)[0] & 0x00FFFFFFFFFFFE00
        for k in range(per_block if b else 0):
            n = struct.unpack_from(">H", img, b + k * 2)[0]
            if n:
                stored[ri * per_block + k] = n
    refs = references(img)
    bad = [(hc, n, stored.get(hc, 0)) for hc, n in refs.items() if stored.get(hc, 0) < n]
    assert not bad, "refcount errors (cluster, references, refcount): %r" % bad[:10]
    assert (max(refs) + 1) * cs <= len(img) + cs - 1, "a reference past the end of the file"
    leaks = sum(1 for hc, n in stored.items() if n > refs.get(hc, 0))
    print("image ok (%d leaked clusters)" % leaks)


def check(listing):
    lines = open(listing).read().split("\n")
    assert lines[0] == "titles 4D53006E", lines[0]
    want = []
    h = hashlib.sha256()
    for p in sorted(FILES):
        want.append("%s %d %s" % (p, len(FILES[p]), hashlib.sha256(FILES[p]).hexdigest()))
        h.update(FILES[p])
    want.append("hash " + h.hexdigest())
    got = [l for l in lines[1:] if l]
    assert got == want, "\n".join(["got:"] + got + ["want:"] + want)
    print("xbox_hdd ok")


if __name__ == "__main__":
    {"make": make, "check": check, "check-image": check_image}[sys.argv[1]](sys.argv[2])
