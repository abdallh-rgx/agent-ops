#!/usr/bin/env python3
"""
find_native.py — ابحث عن الدوال الأصلية (native RVAs) في libUnreal.so (ARM64)
بدون IDA: عبر تتبع مراجع سلاسل أسماء الدوال (ADRP+ADD) وإيجاد مؤشر الدالة المجاور
(نمط FNativeFunctionRegistrar::RegisterFunction).

usage:
  python3 find_native.py <libUnreal.so> Name1 Name2 ... [--window 0x180] [--strings SubStr]
  python3 find_native.py <libUnreal.so> --strings ButterCake        # اكتشاف أسماء
  python3 find_native.py <libUnreal.so> SpawnLoot SetPickupItems ServerOnAttemptInteract

targets معروفة للتحقق (21.30 CL-21088273):
  ProcessEvent=0x7611b5c  InitializeAthenaQuickBars=0x56607ec
  SetIsDoorOpen=0x05955e38 (مرجع للتحقق من صحة الماسح)
"""
import sys, struct, re

def load_elf(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF': raise SystemExit('not ELF')
    e_phoff = struct.unpack_from('<Q', data, 0x20)[0]
    e_phentsize = struct.unpack_from('<H', data, 0x36)[0]
    e_phnum = struct.unpack_from('<H', data, 0x38)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from('<II', data, off)
        p_offset = struct.unpack_from('<Q', data, off + 0x08)[0]
        p_vaddr = struct.unpack_from('<Q', data, off + 0x10)[0]
        p_filesz = struct.unpack_from('<Q', data, off + 0x20)[0]
        segs.append({'type': p_type, 'flags': p_flags, 'off': p_offset, 'va': p_vaddr, 'size': p_filesz})
    loads = [s for s in segs if s['type'] == 1]
    text = next((s for s in loads if s['flags'] & 1), loads[0])  # X segment
    return data, loads, text

def rva2off(loads, rva):
    for s in loads:
        if s['va'] <= rva < s['va'] + s['size']:
            return s['off'] + (rva - s['va'])
    return None

def find_string_vas(data, loads, needle):
    """كل مواضع سلسلة (null-terminated) داخل المقاطع القابلة للقراءة."""
    vas = []
    nb = needle.encode() if isinstance(needle, str) else needle
    for s in loads:
        if not (s['flags'] & 2):  # W only
            continue
        seg = data[s['off']:s['off'] + s['size']]
        start = 0
        while True:
            i = seg.find(nb, start)
            if i < 0: break
            # يجب أن تكون بداية سلسلة (الحرف قبلها 0) وتنتهي بـ 0
            before_ok = (i == 0) or seg[i-1] == 0
            end = i + len(nb)
            after_ok = (end < len(seg)) and seg[end] == 0
            if before_ok and after_ok:
                vas.append(s['va'] + i)
            start = i + 1
    return vas

def scan_refs(data, loads, text):
    """مسح واحد لكل مقطع X: يرجع (str_refs, code_refs, data_refs)
    str_refs: [(pc, target_va, reg)] لكل ADRP+ADD يحسب عنواناً
    code_refs/data_refs: [(pc, target_va)]"""
    str_refs, code_refs, data_refs = [], [], []
    tva, toff, tsize = text['va'], text['off'], text['size']
    words = memoryview(data)[toff:toff + tsize]
    n = tsize // 4
    regs = [0] * 32   # صفحات ADRP لكل مسجل
    have = [False] * 32
    for k in range(n):
        pc = tva + k * 4
        w = words[k * 4] | (words[k*4+1] << 8) | (words[k*4+2] << 16) | (words[k*4+3] << 24)
        # ADRP: op=1(bit31), bits30-29=immlo, bit28-24=10000
        if (w & 0x9F000000) == 0x90000000:
            rd = w & 31
            immlo = (w >> 29) & 3
            immhi = (w >> 5) & 0x7FFFF
            imm = (immhi << 2) | immlo
            if imm & (1 << 20): imm -= (1 << 21)
            regs[rd] = ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFFFFFF
            have[rd] = True
            continue
        # ADD imm 64-bit: 1001000100 -> (w & 0xFFC00000) == 0x91000000
        if (w & 0xFFC00000) == 0x91000000:
            rd = w & 31
            rn = (w >> 5) & 31
            imm = (w >> 10) & 0xFFF
            if rn != 31 and have[rn]:
                target = (regs[rn] + imm) & 0xFFFFFFFFFFFFFFFF
                str_refs.append((pc, target, rd))
                have[rd] = False  # أُعيد استخدام المسجل
            continue
        # LDR (imm, unsigned) 64-bit: 1111100101 -> F9400000 mask FF C0 0000 == F9400000
        if (w & 0xFFC00000) == 0xF9400000:
            rt = w & 31
            rn = (w >> 5) & 31
            imm = ((w >> 10) & 0xFFF) * 8
            if rn != 31 and have[rn]:
                data_refs.append((pc, (regs[rn] + imm) & 0xFFFFFFFFFFFFFFFF))
            have[rt] = False
    return str_refs, code_refs, data_refs

def prologue(data, loads, text, rva, count=8):
    """فك برولوج الدالة إن توفر capstone، وإلا hex."""
    off = rva2off(loads, rva)
    if off is None: return '(rva خارج المقاطع)'
    try:
        from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
        md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
        out = []
        for ins in md.disasm(data[off:off + count * 4], rva):
            out.append(f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}')
            if len(out) >= count: break
        return '\n'.join(out)
    except ImportError:
        return data[off:off + count * 4].hex()

KNOWN = {0x7611B5C: 'ProcessEvent', 0x56607EC: 'InitializeAthenaQuickBars', 0x05955E38: 'SetIsDoorOpen(expected)'}

def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__); sys.exit(1)
    lib = args[0]
    rest = args[1:]
    window = 0x180
    names, strings_only = [], []
    i = 0
    while i < len(rest):
        if rest[i] == '--window':
            window = int(rest[i+1], 0); i += 2
        elif rest[i] == '--strings':
            strings_only.append(rest[i+1]); i += 2
        else:
            names.append(rest[i]); i += 1

    data, loads, text = load_elf(lib)
    print(f'// {lib}: X-seg va={text["va"]:#x} size={text["size"]:#x}')

    queries = [(s, 'string') for s in strings_only] + [(n, 'func') for n in names]
    if not queries:
        print('لا أهداف'); return

    # اجمع كل عناوين السلاسل
    targets = {}
    for q, kind in queries:
        vas = find_string_vas(data, loads, q)
        if not vas:
            print(f'!! سلسلة "{q}" غير موجودة في الملف')
        for va in vas:
            targets[va] = q
        print(f'// "{q}": {len(vas)} موضع(s) ' + ' '.join(f'{v:#x}' for v in vas[:8]))

    if strings_only and not names:
        return

    print('// جاري مسح مقطع الكود (مرة واحدة)…')
    str_refs, code_refs, data_refs = scan_refs(data, loads, text)
    print(f'// refs: strings={len(str_refs)}')

    # فهرس refs بالسلسلة
    by_va = {}
    for pc, tva, rd in str_refs:
        if tva in targets:
            by_va.setdefault(tva, []).append(pc)

    code_arr = [pc for pc, t in [] ]  # placeholder
    # candidates: نحتاج كل ADRP+ADD يحسب عنوان كود — لكن scan_refs خزّن الكل في str_refs (عناوين أي شيء).
    # عنوان الكود = كل ref هدفه داخل مقطع X:
    text_lo, text_hi = text['va'], text['va'] + text['size']
    code_sites = [(pc, t) for pc, t, _ in str_refs if text_lo <= t < text_hi and t != pc - 0]  # استبعد self-page
    code_pcs = sorted(pc for pc, t in code_sites)
    import bisect
    code_addrs = sorted(t for pc, t in code_sites)

    for q, kind in queries:
        vas = [va for va, name in targets.items() if name == q]
        if not vas: continue
        print(f'\n=== {q} ===')
        seen = set()
        for vi, va in enumerate(sorted(vas)):
            refs = by_va.get(va, [])
            for pc in refs:
                # مرشحو الكود قرب pc
                lo = bisect.bisect_left(code_pcs, pc - window)
                hi = bisect.bisect_right(code_pcs, pc + window)
                for cp in code_pcs[lo:hi]:
                    idx = code_pcs.index(cp) if False else None
                cands = []
                for cp in code_pcs[lo:hi]:
                    # ابحث عنوان الهدف الخاص بـ cp
                    pass
                # أعد الحساب مباشرة
                cand = [(cpc, ct) for (cpc, ct) in code_sites if abs(cpc - pc) <= window]
                for cpc, ct in sorted(cand, key=lambda x: abs(x[0] - pc))[:4]:
                    key = (pc, ct)
                    if key in seen: continue
                    seen.add(key)
                    known = KNOWN.get(ct)
                    tag = f'  <== KNOWN: {known}' if known else ''
                    if text_lo <= ct < text_hi:
                        print(f'  [str@{va:#x}] ref@{pc:#x} -> FUNC cand {ct:#x} (Δ{abs(cpc-pc):#x}){tag}')
                        print('    ' + prologue(data, loads, text, ct, 5).replace('\n', '\n    '))
                # مرشحو البيانات (vtable/GOT)
    print('\n// انتهى. تحقق: cand قريب من KNOWN SetIsDoorOpen=0x05955e38 يعني الماسح دقيق.')

if __name__ == '__main__':
    main()
