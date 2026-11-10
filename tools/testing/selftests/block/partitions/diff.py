#!/usr/bin/env python3
"""Differential test: Rust msdos parser vs the running kernel's parser."""
import os, subprocess, sys, time

HARNESS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'harness')
IMGS = os.environ['PARTS_IMGS']
INFO_ONLY = {'bsd_sub.img'}

def run_rust(path, scale):
    out = subprocess.check_output([HARNESS, path, str(scale)], text=True)
    parts = {}
    for line in out.splitlines():
        f = line.split()
        if f[0].startswith('p'):
            parts[int(f[0][1:])] = (int(f[1]), int(f[2]))
    return parts

def loop_attach(path, bs):
    cmd = ['losetup', '--find', '--show', '--read-only', '--partscan']
    if bs != 512:
        cmd += ['--sector-size', str(bs)]
    return subprocess.check_output(cmd + [path], text=True).strip()

def run_rust_full(path, scale):
    out = subprocess.check_output([HARNESS, path, str(scale)], text=True)
    parts = {}
    ret = None
    for line in out.splitlines():
        f = line.split()
        if f[0] == 'ret':
            ret = int(f[1])
        elif f[0].startswith('p'):
            parts[int(f[0][1:])] = (int(f[1]), int(f[2]))
    return ret, parts

def kernel_parts(dev):
    name = os.path.basename(dev)
    base = '/sys/block/%s' % name
    prev, stable = None, 0
    for _ in range(200):
        cur = tuple(sorted(e for e in os.listdir(base) if e.startswith(name + 'p')))
        stable = stable + 1 if cur == prev else 0
        prev = cur
        if stable >= 3:
            break
        time.sleep(0.01)
    parts = {}
    for entry in prev or ():
        num = int(entry[len(name)+1:])
        start = int(open(os.path.join(base, entry, 'start')).read())
        size = int(open(os.path.join(base, entry, 'size')).read())
        parts[num] = (start, size)
    return parts

def apply_eod(parts, capacity):
    """Model blk_add_partition(): drop beyond-EOD, clamp the rest."""
    out = {}
    for n, (start, size) in parts.items():
        if start >= capacity:
            continue
        if start + size > capacity:
            size = capacity - start
        out[n] = (start, size)
    return out

def main():
    fails = 0
    names = sorted(os.listdir(IMGS))
    for n in names:
        bs = 4096 if n.startswith('4k_') else 512
        scale = bs // 512
        path = os.path.join(IMGS, n)
        capacity = os.path.getsize(path) // 512
        ret, raw = run_rust_full(path, scale)
        if n in INFO_ONLY:
            print('INFO', n, 'rust ret', ret, 'parts', sorted(raw.items())[:6])
            continue
        if ret == -2:
            print('INFO', n, 'AIX defer (-2), shim falls back to the C parser')
            continue
        rust = apply_eod(raw, capacity)
        dev = loop_attach(path, bs)
        try:
            kern = kernel_parts(dev)
        finally:
            subprocess.call(['losetup', '--detach', dev], stderr=subprocess.DEVNULL)
        if rust != kern:
            fails += 1
            print('MISMATCH', n, 'bs', bs)
            print('  rust:', sorted(rust.items())[:8])
            print('  kern:', sorted(kern.items())[:8])
    print('checked %d images, %d mismatches' % (len(names), fails))
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main())