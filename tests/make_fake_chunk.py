"""Packs a folder into a chunkG*.bin-style archive (stored, encrypted blocks) for testing.

The layout matches what WorldChunkTool reads: a block table followed by 256 KiB blocks that form
one virtual stream (file table at 0x100, then file data). Blocks are stored uncompressed
(size field 0) because no Oodle compressor is available, and XOR-encrypted with the Iceborne
key order so the decryption path is exercised.

usage: python3 make_fake_chunk.py <folder> <out chunkG0.bin> <repo data/chunk_keyseq.bin>
"""
import os
import struct
import sys

BLOCK = 0x40000
KEYS = [bytes.fromhex(h) for h in """
ac76cb97ec7500133a81038e7a82c80a 6bb5c956e44d00bc305233cfbfaafa25 8f021dccb0f2787206fbdee2390bbb5c
da5c1e531d8359157875bd63bfaafa25 1f6d31c883dd716d7e8f598ce23f1929 4bb0de04e4e0856980ccb2942f9ce9f9
8bce54dc4c11139a7875bd63bfaafa25 ec13345966ce7312440089a2ceddcee9 e4662c709c753a039a2c0f5ae23f1929
a492fc9033949c15a033ac223735cca7 25099c1f911a26e5ce9172f07a82c80a d1d29d7446d4fdf1a033ac223735cca7
7eb268373b5d361ed6d313e2933c4dcb a1c7d2ea661895ac7875bd63bfaafa25 82a43b2108797c6a440089a2ceddcee9
41d055b3dd6015167e8f598ce23f1929""".split()]


def main():
    folder, out, keyseq_path = sys.argv[1:4]
    keyseq = open(keyseq_path, "rb").read()
    files = []
    for root, _, names in os.walk(folder):
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = "\\" + os.path.relpath(full, folder).replace(os.sep, "\\")
            files.append((rel, open(full, "rb").read()))
    toc = bytearray(0x100)
    struct.pack_into("<ii", toc, 0x0C, 1, len(files))
    parent = bytearray(0x58)
    parent[0:1] = b"\\"
    struct.pack_into("<qqii", parent, 0x3C, 0, 0, 1, len(files))
    toc += parent
    data_start = len(toc) + 0xB8 * len(files)
    blobs = bytearray()
    for rel, data in files:
        e = bytearray(0xB8)
        name = rel.encode("ascii")
        e[0:len(name)] = name
        struct.pack_into("<qqii", e, 0xA0, len(data), data_start + len(blobs), 0, 0)
        toc += e
        blobs += data
    stream = bytes(toc + blobs)
    blocks = [bytearray(stream[i:i + BLOCK]) for i in range(0, len(stream), BLOCK)]
    for i, b in enumerate(blocks):
        k = keyseq[i // 2] >> (4 * (i & 1)) & 0xF
        key = KEYS[k]
        for j in range(len(b)):
            b[j] ^= key[j & 15]
    header = struct.pack("<II", 0x00504B47, len(blocks))
    table = bytearray()
    offset = 8 + 8 * len(blocks)
    for b in blocks:
        table += b"\0\0\0" + offset.to_bytes(5, "little")
        offset += len(b)
    with open(out, "wb") as f:
        f.write(header + table + b"".join(blocks))
    print("wrote %s: %d files, %d blocks" % (out, len(files), len(blocks)))


if __name__ == "__main__":
    main()
