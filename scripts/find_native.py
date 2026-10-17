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

def find_string_vas(data, loads, needle, raw=False):
    """كل مواضع السلسلة. raw=True يجد كل التطابقات (FNamePool بلا null-termination)
    ويضيف أيضاً مواضع رأس الإدخال (va-2 / va-4) لأن المرجع يشير لرأس الإدخال."""
    vas = []
    nb = needle.encode() if isinstance(needle, str) else needle
    for s in loads:
        if not (s['flags'] & 4):  # يجب أن يكون قابلاً للقراءة (R)
            continue
        seg = data[s['off']:s['off'] + s['size']]
        start = 0
        while True:
            i = seg.find(nb, start)
            if i < 0: break
            va = s['va'] + i
            if raw:
                vas.append(va)
                if i >= 2: vas.append(va - 2)   # FNamePool header (uint16)
                if i >= 4: vas.append(va - 4)
            else:
                before_ok = (i == 0) or seg[i-1] == 0
                end = i + len(nb)
                after_ok = (end < len(seg)) and seg[end] == 0
                if before_ok and after_ok:
                    vas.append(va)
            start = i + 1
    return sorted(set(vas))

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

    # اجمع كل عناوين السلاسل (raw دائماً — FNamePool بلا null)
    targets = {}
    for q, kind in queries:
        vas = find_string_vas(data, loads, q, raw=True)
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

    # فهرس refs بالسلسلة (كود مباشر ADRP+ADD)
    by_va = {}
    for pc, tva, rd in str_refs:
        if tva in targets:
            by_va.setdefault(tva, []).append(pc)

    # refs عبر جداول البيانات (ADRP+LDR) — جدول يحتوي عنوان الإدخال، والقيم حوله مؤشرات كود
    data_by_va = {}
    for pc, tva in data_refs:
        if tva in targets:
            data_by_va.setdefault(tva, []).append(pc)

    text_lo, text_hi = text['va'], text['va'] + text['size']
    import bisect
    code_sites = sorted((pc, t) for (pc, t, _) in str_refs if text_lo <= t < text_hi)
    code_pcs = [p for p, _ in code_sites]

    def code_cands_near(pc, win):
        lo = bisect.bisect_left(code_pcs, pc - win)
        hi = bisect.bisect_right(code_pcs, pc + win)
        return [(cpc, code_sites[i][1]) for i, cpc in enumerate(code_pcs[lo:hi], start=lo)]

    def code_ptrs_near(dva, span=0x60):
        """اقرأ qwords حول عنوان جدول واعثر على مؤشرات كود."""
        out = []
        for s in loads:
            if s['va'] <= dva < s['va'] + s['size'] and (s['flags'] & 4):
                base = s['off'] + (dva - s['va'])
                for delta in range(-span, span + 1, 8):
                    o = base + delta
                    if 0 <= o <= len(data) - 8:
                        q = struct.unpack_from('<Q', data, o)[0]
                        if text_lo <= q < text_hi:
                            out.append((dva + delta, q))
                break
        return out

    for q, kind in queries:
        vas = [va for va, name in targets.items() if name == q]
        if not vas: continue
        print(f'\n=== {q} ===')
        seen = set()
        total_refs = 0
        for vi, va in enumerate(sorted(vas)):
            # مسار 1: مراجع كود مباشرة
            for pc in by_va.get(va, []):
                total_refs += 1
                cand = [(cpc, ct) for (cpc, ct) in code_cands_near(pc, window) if ct != va]
                for cpc, ct in sorted(cand, key=lambda x: abs(x[0] - pc))[:4]:
                    key = (pc, ct)
                    if key in seen: continue
                    seen.add(key)
                    known = KNOWN.get(ct)
                    tag = f'  <== KNOWN: {known}' if known else ''
                    print(f'  [str@{va:#x}] code-ref@{pc:#x} -> FUNC cand {ct:#x} (Δ{abs(cpc-pc):#x}){tag}')
                    print('    ' + prologue(data, loads, text, ct, 5).replace('\n', '\n    '))
            # مسار 2: جداول البيانات
            for dpc in data_by_va.get(va, []):
                total_refs += 1
                for tbl, ptr in code_ptrs_near(va):
                    key = ('d', tbl, ptr)
                    if key in seen: continue
                    seen.add(key)
                    known = KNOWN.get(ptr)
                    tag = f'  <== KNOWN: {known}' if known else ''
                    print(f'  [str@{va:#x}] data-tbl@{tbl:#x} (ref@{dpc:#x}) -> PTR {ptr:#x}{tag}')
                    print('    ' + prologue(data, loads, text, ptr, 5).replace('\n', '\n    '))
        if total_refs == 0:
            print('  (لا مراجع كود/بيانات — الاسم في FNamePool فقط؛ استدعِ الدالة عبر ProcessEvent بدلاً من hooking)')

if __name__ == '__main__':
    main()
