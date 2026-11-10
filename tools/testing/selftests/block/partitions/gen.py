#!/usr/bin/env python3
"""Generate MBR images covering the interesting msdos.c paths."""
import os, random, struct, sys

S = 512
TOTAL = 65536      # 32 MiB disk
EXT_BASE = 8192

def entry(boot, sysind, start, nr):
    b = bytearray(16)
    b[0] = boot
    b[4] = sysind
    struct.pack_into('<I', b, 8, start & 0xffffffff)
    struct.pack_into('<I', b, 12, nr & 0xffffffff)
    return bytes(b)

def mbr(entries, disksig=0xcafebabe, magic=True):
    sec = bytearray(S)
    for i, e in enumerate(entries):
        if e is not None:
            sec[0x1be + i*16:0x1be + (i+1)*16] = e
    struct.pack_into('<I', sec, 0x1b8, disksig)
    if magic:
        sec[510] = 0x55
        sec[511] = 0xaa
    return bytes(sec)

def image(sectors, total=TOTAL):
    buf = bytearray(total * S)
    for idx, data in sectors.items():
        buf[idx*S:idx*S+len(data)] = data
    return bytes(buf)

def chain_img(n, ext_type=0x05, stride=2048):
    secs = {0: mbr([entry(0x80, 0x83, 2048, 4096),
                    entry(0, ext_type, EXT_BASE, TOTAL - EXT_BASE - 1),
                    None, None])}
    for i in range(n):
        cur = EXT_BASE + i * stride
        e1 = entry(0, ext_type, (i + 1) * stride, 1024) if i + 1 < n else None
        secs[cur] = mbr([entry(0, 0x83, 1024, 1024), e1, None, None])
    return image(secs)

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    c = {}
    c['empty'] = image({})
    c['magic_only'] = image({0: mbr([None]*4)})
    c['one_fat'] = image({0: mbr([entry(0x80,0x0c,2048,4096),None,None,None])})
    c['four_prim'] = image({0: mbr([entry(0x80,0x83,2048,4096), entry(0,0x07,8192,4096),
                                     entry(0,0x83,12288,2048), entry(0,0xfd,16384,2048)])})
    c['raid'] = image({0: mbr([entry(0,0xfd,2048,4096),None,None,None])})
    c['protective'] = image({0: mbr([entry(0,0xee,1,0xffffffff),None,None,None])})
    c['bad_bootind'] = image({0: mbr([entry(0x55,0x83,2048,4096),None,None,None])})
    c['fat_bpb'] = image({0: bytes([0xeb,0x3c,0x90]) + b'MSDOS5.0' +
                             struct.pack('<H',512) + bytes([1]) + struct.pack('<H',1) +
                             bytes([2]) + bytes([0xe0]) + bytes([0xf8]) + bytes(400) +
                             b'\x55\xaa'})
    for n in (1, 2, 3, 5, 8):
        c['ext_%d' % n] = chain_img(n)
        c['ext_%d_0f' % n] = chain_img(n, 0x0f)
        c['ext_%d_85' % n] = chain_img(n, 0x85)
    c['ext_loop'] = image({0: mbr([entry(0,0x83,2048,4096), entry(0,0x05,EXT_BASE,4096), None, None]),
                           EXT_BASE: mbr([entry(0,0x83,1024,1024), entry(0,0x05,0,4096), None, None])})
    c['ext_overlap'] = image({0: mbr([entry(0,0x05,2048,4000), None, None, None]),
                              2048: mbr([entry(0,0x83,512,512), entry(0,0x83,512,512),
                                         entry(0,0x83,3000,2000), entry(0,0x83,100,100)])})
    c['overflow'] = image({0: mbr([entry(0,0x83,0xffffffff,0xffffffff),None,None,None])})
    c['zero_size'] = image({0: mbr([entry(0,0x83,2048,0),None,None,None])})
    c['no_magic'] = image({0: mbr([entry(0,0x83,2048,4096),None,None,None], magic=False)})
    rnd = random.Random(1234)
    for i in range(160):
        ents = [entry(rnd.choice([0,0x80]),
                      rnd.choice([0x83,0x0c,0x07,0xfd,0x05,0x0f,0x85,0xee,rnd.randrange(256)]),
                      rnd.randrange(0, 40000), rnd.randrange(0, 40000)) for _ in range(4)]
        c['fuzz%03d' % i] = image({0: mbr(ents, rnd.randrange(0, 1 << 32), magic=rnd.random() > 0.1)})
    for i in range(40):
        depth = rnd.randrange(1, 8)
        c['chain%02d' % i] = chain_img(depth, rnd.choice([0x05,0x0f,0x85]), rnd.choice([1024,2048,4096]))
    # F1 regression: two extended partitions, logical numbering must be shared.
    c['double_ext'] = image({
        0: mbr([entry(0,0x83,2048,2048), entry(0,0x05,8192,3000), entry(0,0x05,16384,3000), None]),
        8192: mbr([entry(0,0x83,1024,1024), entry(0,0x05,1024,1024), None, None]),
        9216: mbr([entry(0,0x83,1024,1024), None, None, None]),
        16384: mbr([entry(0,0x83,1024,1024), entry(0,0x05,1024,1024), None, None]),
        17408: mbr([entry(0,0x83,1024,1024), None, None, None]),
    })
    # AIX label: Rust must defer (-2) so the C AIX parser takes over.
    c['aix_magic'] = image({0: b'\xc9\xc2\xd4\xc1' + mbr([entry(0,0x83,2048,4096),None,None,None])[4:]})
    # BSD disklabel sub-table: documented gap, Kconfig forbids enabling both.
    bsd = bytearray(512)
    struct.pack_into('<I', bsd, 0, 0x82564557)
    struct.pack_into('<H', bsd, 138, 8)
    struct.pack_into('<I', bsd, 148, 0)      # p_offset
    struct.pack_into('<I', bsd, 148 + 4, 2048)  # p_size of first slice
    c['bsd_sub'] = image({0: mbr([entry(0,0xa5,2048,8192),None,None,None]),
                          2048: mbr([None]*4),
                          2049: bytes(bsd)})
    c['4k_one'] = image({0: mbr([entry(0x80,0x83,256,512),None,None,None])})
    c['4k_ext'] = image({0: mbr([entry(0,0x83,256,512), entry(0,0x05,2048,4000), None, None]),
                         2048: mbr([entry(0,0x83,256,256), entry(0,0x05,1024,900), None, None]),
                         3072: mbr([entry(0,0x83,256,256), None, None, None])})
    for name, data in c.items():
        with open(os.path.join(out, name + '.img'), 'wb') as f:
            f.write(data)
    print('wrote', len(c), 'images to', out)

if __name__ == '__main__':
    main()