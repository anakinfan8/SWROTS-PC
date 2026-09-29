"""MSVC linker .map files (publics and static symbols) and C++ name demangling."""
import ctypes
import re

LINE = re.compile(r"^\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f?)\s*(i?)\s*(\S*)")
SECTION = re.compile(r"^\s+([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})H\s+(\S+)\s+(CODE|DATA)")


class MapSymbol:
    __slots__ = ("section", "offset", "name", "address", "is_function", "inline", "obj", "static")

    def __init__(self, section, offset, name, address, is_function, inline, obj, static):
        self.section, self.offset, self.name, self.address = section, offset, name, address
        self.is_function, self.inline, self.obj, self.static = is_function, inline, obj, static


class MapFile:
    def __init__(self, path):
        self.path = path
        self.symbols = []
        self.sections = []  # (section number, offset, length, name, class)
        self.timestamp = None
        self.base = None
        static = False
        for line in open(path, encoding="latin1"):
            if "Timestamp is" in line:
                self.timestamp = int(line.split("Timestamp is")[1].split()[0], 16)
            elif "Preferred load address is" in line:
                self.base = int(line.split()[-1], 16)
            elif line.startswith(" Static symbols"):
                static = True
            m = SECTION.match(line)
            if m and not LINE.match(line):
                self.sections.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16), m.group(4),
                                      m.group(5)))
                continue
            m = LINE.match(line)
            if m:
                self.symbols.append(MapSymbol(int(m.group(1), 16), int(m.group(2), 16), m.group(3),
                                              int(m.group(4), 16), m.group(5) == "f", m.group(6) == "i",
                                              m.group(7), static))

    def functions(self):
        """{address: [names]} for code symbols (section 1)."""
        out = {}
        for s in self.symbols:
            if s.section == 1 and s.address:
                out.setdefault(s.address, []).append(s.name)
        return out

    def section_end(self, number):
        ends = [off + length for sec, off, length, name, cls in self.sections if sec == number]
        return self.base + 0x1000 + max(ends) if ends else None


_undname = None


def demangle(name):
    """Readable form of an MSVC decorated name (via dbghelp); undecorated names pass through."""
    global _undname
    if not name.startswith("?"):
        return name
    if _undname is None:
        dbghelp = ctypes.windll.dbghelp
        _undname = dbghelp.UnDecorateSymbolName
        _undname.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_uint32]
    buf = ctypes.create_string_buffer(2048)
    n = _undname(name.encode("latin1"), buf, 2048, 0)
    return buf.value.decode("latin1") if n else name


def short_name(name):
    """Class::Method (no types), for display and matching."""
    if not name.startswith("?"):
        return name
    buf = ctypes.create_string_buffer(2048)
    demangle("?x@@YAXXZ")  # ensure loaded
    UNDNAME_NAME_ONLY = 0x1000
    n = _undname(name.encode("latin1"), buf, 2048, UNDNAME_NAME_ONLY)
    return buf.value.decode("latin1") if n else name
