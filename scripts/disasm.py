#!/usr/bin/env python3
"""ARM64 disassembler for libUnreal.so — ELF-aware (RVA -> file offset via PT_LOAD).
usage: disasm.py <libUnreal.so> <RVA> [len] [--chan]
Known anchors (21.30 CL-21088273):
  ProcessEvent=0x7611b5c  WorldTick=0x949685c  EngineExec=0x98bb160
  InitializeAthenaQuickBars=0x56607ec  GenericArrayAdd=0x0944A668
  SetIsDoorOpen=0x05955e38
  QuickBar: Primary 0x278 / Secondary 0x308 / Creative 0x398 / SlotSize 0x28
"""
import sys, struct

def load_segments(data):
    if data[:4] != b'\x7fELF': raise SystemExit('not ELF')
    e_phoff = struct.unpack_from('<Q', data, 0x20)[0]
    e_phentsize = struct.unpack_from('<H', data, 0x36)[0]
    e_phnum = struct.unpack_from('<H', data, 0x38)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if struct.unpack_from('<I', data, off)[0] == 1:
            p_offset = struct.unpack_from('<Q', data, off + 0x08)[0]
            p_vaddr = struct.unpack_from('<Q', data, off + 0x10)[0]
            p_filesz = struct.unpack_from('<Q', data, off + 0x20)[0]
            segs.append((p_vaddr, p_offset, p_filesz))
    return segs

def rva2off(segs, rva):
    for va, off, sz in segs:
        if va <= rva < va + sz: return off + (rva - va)
    raise SystemExit(f'RVA {rva:#x} not in PT_LOAD')

ANCHORS = {0x7611B5C: 'ProcessEvent', 0x949685C: 'WorldTick', 0x98BB160: 'EngineExec',
           0x56607EC: 'InitializeAthenaQuickBars', 0x0944A668: 'GenericArrayAdd',
           0x05955E38: 'SetIsDoorOpen'}
CHAN = {'0x278': 'QuickBar Primary', '0x308': 'QuickBar Secondary', '0x398': 'QuickBar Creative',
        '0x10]': 'QuickBar.Slots', '0x738': 'QuickBars vtable slot'}

def main():
    if len(sys.argv) < 3: print(__doc__); sys.exit(1)
    path, rva = sys.argv[1], int(sys.argv[2], 0)
    length, annotate = 0x200, False
    for extra in sys.argv[3:]:
        if extra == '--chan': annotate = True
        else:
            try: length = int(extra, 0)
            except ValueError: pass
    data = open(path, 'rb').read()
    segs = load_segments(data)
    off = rva2off(segs, rva)
    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
    except ImportError:
        print('capstone missing: pip install capstone'); sys.exit(1)
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    print(f'// {path} RVA {rva:#x} ({ANCHORS.get(rva,"?")}) off {off:#x}')
    for ins in md.disasm(data[off:off + length], rva):
        note = ''
        if annotate:
            for k, v in CHAN.items():
                if k in ins.op_str: note = f'  ; {v}'; break
        print(f'{ins.address:#x}:\t{ins.mnemonic:<10}\t{ins.op_str}{note}')

if __name__ == '__main__':
    main()
