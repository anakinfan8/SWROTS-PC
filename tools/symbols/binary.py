"""Executable images (Xbox XBE and Windows PE) and the facts we extract from their code.

A Binary maps an executable's sections at their virtual addresses and knows
which ranges hold code. Analysis finds function starts, then disassembles each
function once to record what it references: strings, other functions (calls),
and a relocation-independent fingerprint of its code.
"""
import bisect
import hashlib
import re
import struct

import capstone
from capstone import x86

CLASS_NAME = re.compile(rb"[IT][A-Z][A-Za-z0-9_]{1,80}\Z")


class Binary:
    def __init__(self, name):
        self.name = name
        self.sections = []  # (name, va, size, data, is_code)
        self.relocs = None  # set of addresses holding absolute pointers (PE only)
        self.entry = None

    # --- memory -------------------------------------------------------------------------------

    def section_at(self, va):
        for s in self.sections:
            if s[1] <= va < s[1] + s[2]:
                return s
        return None

    def read(self, va, n):
        s = self.section_at(va)
        if not s:
            return None
        off = va - s[1]
        chunk = s[3][off:off + n]
        return chunk + b"\0" * (n - len(chunk)) if len(chunk) < n else chunk

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack("<I", b)[0] if b else None

    def is_code(self, va):
        s = self.section_at(va)
        return bool(s and s[4])

    def is_data(self, va):
        s = self.section_at(va)
        return bool(s and not s[4])

    def cstring(self, va, limit=512):
        s = self.section_at(va)
        if not s or s[4]:
            return None
        off = va - s[1]
        end = s[3].find(b"\0", off, off + limit)
        if end < 0:
            return None
        raw = s[3][off:end]
        if len(raw) < 2 or any(c < 0x20 or c > 0x7E for c in raw if c not in (9, 10, 13)):
            return None
        return raw.decode("latin1")

    def code_ranges(self):
        return [(s[1], s[1] + s[2]) for s in self.sections if s[4]]


def load_xbe(path, code_sections=(".text", "D3D", "D3DX", "XGRPH", "DSOUND", "WMADEC", "XPP")):
    d = open(path, "rb").read()
    u32 = lambda o: struct.unpack_from("<I", d, o)[0]
    base = u32(0x104)
    b = Binary("xbe")
    for i in range(u32(0x11C)):
        flags, va, vsize, raw, rsize, name_va = struct.unpack_from("<6I", d, u32(0x120) - base + i * 0x38)
        name = d[name_va - base:d.index(b"\0", name_va - base)].decode()
        data = d[raw:raw + rsize] + b"\0" * max(0, vsize - rsize)
        b.sections.append((name, va, vsize, data, name in code_sections))
    b.entry = u32(0x128) ^ 0xA8FC57AB
    return b


def load_pe(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsize = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", d, opt + 28)[0]
    b = Binary(path.replace("\\", "/").rsplit("/", 1)[-1])
    b.entry = base + struct.unpack_from("<I", d, opt + 16)[0]
    reloc_rva, reloc_size = struct.unpack_from("<II", d, opt + 96 + 5 * 8)
    sec = opt + optsize
    raw_of = []
    for i in range(nsec):
        o = sec + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode()
        vsize, rva, rsize, raw = struct.unpack_from("<4I", d, o + 8)
        chars = struct.unpack_from("<I", d, o + 36)[0]
        data = d[raw:raw + min(rsize, vsize)] + b"\0" * max(0, vsize - rsize)
        b.sections.append((name, base + rva, vsize, data, bool(chars & 0x20000000)))
        raw_of.append((rva, vsize, raw))
    # Base relocations: exact set of absolute pointers in the image.
    relocs = set()
    if reloc_rva:
        r = next(raw + reloc_rva - rva for rva, vs, raw in raw_of if rva <= reloc_rva < rva + vs)
        end = r + reloc_size
        while r < end:
            page, size = struct.unpack_from("<II", d, r)
            if size < 8:
                break
            for k in range((size - 8) // 2):
                e = struct.unpack_from("<H", d, r + 8 + 2 * k)[0]
                if e >> 12 == 3:
                    relocs.add(base + page + (e & 0xFFF))
            r += size
    b.relocs = relocs
    return b


# --- function discovery ----------------------------------------------------------------------------

def find_vtables(b, starts):
    """Runs of 2+ pointers to known function starts in data sections: [(va, [fn, ...])]."""
    out = []
    for name, va, size, data, is_code in b.sections:
        if is_code:
            continue
        run_start, run = None, []
        for off in range(0, size - 3, 4):
            p = struct.unpack_from("<I", data, off)[0]
            if p in starts:
                if not run:
                    run_start = va + off
                run.append(p)
            else:
                if len(run) >= 2:
                    out.append((run_start, run))
                run = []
        if len(run) >= 2:
            out.append((run_start, run))
    return out


def discover_functions(b, known=()):
    """Initial function starts: code after int3 padding, and code pointed to from data
    (vtables, callback tables). Call targets are added after disassembly (see
    discover_all)."""
    starts = set(k for k in known if b.is_code(k))
    if b.entry and b.is_code(b.entry):
        starts.add(b.entry)
    for lo, hi in b.code_ranges():
        data = b.section_at(lo)[3]
        for m in re.finditer(rb"\xCC+", data):
            e = m.end()
            if e < len(data) and data[e] not in (0xCC, 0x00):
                starts.add(lo + e)
    for name, va, size, data, is_code in b.sections:
        if is_code:
            continue
        for off in range(0, size - 3, 4):
            p = struct.unpack_from("<I", data, off)[0]
            if b.is_code(p):
                prev = b.read(p - 1, 1)
                if p % 16 == 0 or prev in (b"\xCC", b"\xC3"):
                    starts.add(p)
    return starts


def discover_all(b, known=()):
    """Starts from discover_functions plus every direct call target; returns (starts, funcs)."""
    starts = discover_functions(b, known)
    funcs = analyze(b, starts)
    extra = {t for f in funcs.values() for t in f.calls if b.is_code(t)} - starts
    if extra:
        starts |= extra
        funcs = analyze(b, starts)
    return starts, funcs


# --- per-function facts ------------------------------------------------------------------------------

class Function:
    __slots__ = ("addr", "size", "strings", "calls", "fingerprint", "shape", "ninsn", "getter", "consts")

    def __init__(self, addr, size):
        self.addr, self.size = addr, size
        self.strings, self.calls = [], []
        self.fingerprint = self.shape = None
        self.ninsn = 0
        self.getter = None  # string returned by `mov eax, offset str; ret`
        self.consts = []  # compiler-independent constants: "f:<float>" from data, "i:<int>" immediates


def float_const(b, va, size):
    """A floating-point constant stored in data, as a token ("f:0.0174533"), or None."""
    raw = b.read(va, size)
    if not raw:
        return None
    v = struct.unpack("<f" if size == 4 else "<d", raw)[0]
    if v != v or v in (0.0, 1.0, -1.0, 0.5) or not (1e-7 < abs(v) < 1e9):
        return None
    return f"f:{v:.6g}"


def stack_adjust(ins):
    """sub/add esp, N and similar frame-size immediates differ between compilers."""
    return ins.mnemonic in ("sub", "add", "lea") and "esp" in ins.op_str or "ebp" in ins.op_str


def analyze(b, starts, max_size=0x8000):
    """Disassembles every function once. Returns {addr: Function}."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    funcs = {}
    lo_hi = b.code_ranges()
    ordered = sorted(starts)
    for i, a in enumerate(ordered):
        s = b.section_at(a)
        if not s or not s[4]:
            continue
        end = min(ordered[i + 1] if i + 1 < len(ordered) else s[1] + s[2], s[1] + s[2], a + max_size)
        code = s[3][a - s[1]:end - s[1]]
        f = Function(a, end - a)
        norm = bytearray()
        shape = []
        for ins in md.disasm(code, a):
            if ins.id == 0:  # skipdata
                break
            f.ninsn += 1
            raw = bytearray(ins.bytes)
            mnem = ins.mnemonic
            if mnem in ("int3",) and f.ninsn > 1:
                break
            if ins.id in (x86.X86_INS_CALL, x86.X86_INS_JMP) and ins.operands and ins.operands[0].type == x86.X86_OP_IMM:
                t = ins.operands[0].imm & 0xFFFFFFFF
                if ins.id == x86.X86_INS_CALL:
                    f.calls.append(t)
                if len(raw) >= 5 and not (a <= t < end):
                    raw[-4:] = b"\0\0\0\0"
            for op in ins.operands:
                v = None
                if op.type == x86.X86_OP_IMM:
                    v = op.imm & 0xFFFFFFFF
                    off = ins.imm_offset
                elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0:
                    v = op.mem.disp & 0xFFFFFFFF
                    off = ins.disp_offset
                if v is None:
                    continue
                is_ptr = b.section_at(v) is not None and (v > 0xFFFF)
                if is_ptr:
                    st = b.cstring(v)
                    if st is not None:
                        f.strings.append(st)
                    elif op.type == x86.X86_OP_MEM and b.is_data(v) and op.size in (4, 8):
                        c = float_const(b, v, op.size)
                        if c is not None:
                            f.consts.append(c)
                elif op.type == x86.X86_OP_IMM and 0x100 <= v <= 0xFFFFFF00 and not stack_adjust(ins):
                    f.consts.append(f"i:{v:X}")
                    if off and off + 4 <= len(raw):
                        raw[off:off + 4] = b"\0\0\0\0"
            norm += raw
            shape.append(mnem)
            if mnem in ("ret", "retn") and not any(x > ins.address for x in ()):
                # keep going: functions may have several exits; stop only at padding
                pass
        f.fingerprint = hashlib.sha1(bytes(norm)).hexdigest()[:16]
        f.shape = hashlib.sha1(" ".join(shape).encode()).hexdigest()[:16]
        # `mov eax, offset "Name"; ret`
        head = code[:6]
        if len(head) == 6 and head[0] == 0xB8 and head[5] == 0xC3:
            st = b.cstring(struct.unpack_from("<I", head, 1)[0])
            if st is not None:
                f.getter = st
        funcs[a] = f
    return funcs


def string_index(funcs):
    """{string: [function addresses that reference it]}"""
    idx = {}
    for f in funcs.values():
        for s in set(f.strings):
            idx.setdefault(s, []).append(f.addr)
    return idx
