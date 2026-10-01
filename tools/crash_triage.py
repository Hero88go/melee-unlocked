#!/usr/bin/env python3
"""Groups Melee Unlocked crash reports by where they crashed.

A report is the zip the launcher builds (melee_port_crash.txt, melee_port_crash.dmp, melee_port.log,
lobby.log) or a folder holding those files. The tool reads the crash line ("CRASH: exception
C0000005 at ... (module+0xOFFSET), version X" or "FATAL: game stopped at file:line: ..., version X"),
the Source Port's unwound game stack in
melee_port.log ("stack: melee_game.dll+0x..."), and the Static Recomp's last guest function, names
the addresses with the symbols of the build that crashed, and groups reports whose top frames match.

    py -3.12 tools/crash_triage.py REPORT_OR_FOLDER... [--symbols DIR]... [--top 3] [--json out.json]
                                   [--extract DIR]
    py -3.12 tools/crash_triage.py --self-test

Symbols (--symbols DIR, repeatable: a release folder, or build-release-080/port/Release):
  melee_game.dll     melee_game.dbg, through GCC addr2line (--addr2line, MELEE_ADDR2LINE, PATH, or an
                     MSYS2 or ~/toolchains install)
  MSVC executables   <name>.pdb beside <name>.exe through DbgHelp, else the <name>.map /MAP file
A folder names only reports of its own version (the FileVersion of its melee_source.exe or
melee_port.exe), so an old report is never named with new symbols; --any-version overrides that.
A frame without symbols stays module+offset, which still groups exactly. System DLLs are not named
here: open the minidump (--extract) in a debugger with the Microsoft symbol server for those.

Limits, so a flood of reports cannot hang it: --max-reports (newest first), --max-zip-bytes (the
relay accepts 8 MB), --max-member-bytes (read per text file), --max-addresses and --timeout per
symbolizer call. Zips are read in memory; --extract writes only the four known file names.
"""
import argparse
import bisect
import dataclasses
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

KNOWN = ("melee_port_crash.txt", "melee_port_crash.dmp", "melee_port.log", "lobby.log")
TEXT_MEMBERS = ("melee_port_crash.txt", "melee_port.log")
GAME_DLL = "melee_game.dll"
GAME_BASE = 0x82800000   # sourceport/game/CMakeLists.txt --image-base; read from the .dbg when it has one
VERSIONED_EXES = ("melee_source.exe", "melee_port.exe", "melee_port_playback.exe", "melee_port_compat.exe",
                  "MeleeUnlockedLauncher.exe")
MAX_MEMBERS = 16

HEAD = re.compile(r"CRASH: exception ([0-9A-Fa-f]{8}) at (?:(?:([0-9A-Fa-f]+) \())?(.*?)\+0x([0-9A-Fa-f]+)\)?, version\s*(\S+)")
FATAL = re.compile(r"^FATAL: game stopped(?: at (.+):(\d+)(?::[ \t]*([^\r\n]*?))?)?(?:, version\s*(\S+))?[ \t]*\r?$", re.MULTILINE)
PANIC = re.compile(r"game panic at (.+):(\d+)\s*$")
LAST_GUEST = re.compile(r"last guest function ([0-9A-Fa-f]{8}) ([^\r\n]*?), lr ([0-9A-Fa-f]{8})")
GAME_CRASH = re.compile(r"game crash ([0-9A-Fa-f]{8}) (?:at melee_game\.dll\+0x([0-9A-Fa-f]+)|calling ([0-9A-Fa-f]+))")
PRIVATE_GAME_CRASH = re.compile(r"CRASH: exception ([0-9A-Fa-f]{8}) at melee_game\.dll\+0x([0-9A-Fa-f]+)")
STACK = re.compile(r"stack: melee_game\.dll\+0x([0-9A-Fa-f]+)")
ACCESS = re.compile(r"(?:reading|writing|executing) address [0-9A-Fa-f]+")
FORCED = re.compile(r"test: forced crash at frame (\d+)")
A2L = re.compile(r"^0x([0-9a-fA-F]+): (.*?)(?: at (.*))?$")
MAP_LINE = re.compile(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+f\b")
EXCEPTIONS = {"C0000005": "access violation", "C00000FD": "stack overflow", "C0000409": "fast fail",
              "C000001D": "illegal instruction", "C0000094": "integer divide by zero", "C0000374": "heap corruption",
              "C0000096": "privileged instruction", "80000003": "breakpoint", "E06D7363": "C++ exception",
              "FATAL": "game panic"}


@dataclasses.dataclass
class Limits:
    max_reports: int = 500
    max_zip_bytes: int = 16 << 20
    max_member_bytes: int = 4 << 20
    max_addresses: int = 5000
    timeout: float = 120.0
    max_scan: int = 200000


@dataclasses.dataclass
class Frame:
    module: str              # a module file name, "guest" (a Static Recomp game function) or "?"
    offset: int = 0
    name: str = ""
    where: str = ""

    def key(self):
        # A named frame groups by function, so two builds of the same crash still meet; an unnamed
        # one groups by its exact offset.
        if self.module == "guest":
            return "guest " + self.name if self.name else f"guest+0x{self.offset:X}"
        return f"{self.module}!{self.name}" if self.name else f"{self.module}+0x{self.offset:X}"

    def label(self):
        if self.module == "guest":
            return "guest " + self.name if self.name else f"guest+0x{self.offset:X}"
        text = f"{self.module}+0x{self.offset:X}"
        if self.name:
            text += "  " + self.name
        if self.where:
            text += f"  ({self.where})"
        return text


@dataclasses.dataclass
class Report:
    source: str
    code: str = ""
    module: str = ""
    offset: int = 0
    version: str = ""
    engine: str = ""
    access: str = ""
    fatal_location: str = ""
    fatal_message: str = ""
    forced_frame: int = 0
    frames: list = dataclasses.field(default_factory=list)
    sizes: dict = dataclasses.field(default_factory=dict)
    notes: list = dataclasses.field(default_factory=list)
    symbols: str = ""

    def signature(self, top):
        assertion = ("assert " + short_where(self.fatal_location),) if self.fatal_location else ()
        return (self.code or "?",) + assertion + tuple(f.key() for f in self.frames[:top])


# ---- reading reports ----

def read_member(zf, info, cap):
    """At most `cap` bytes of a member, or None when it is larger. The read itself is bounded too,
    so a member whose header understates its size still cannot run away."""
    if info.file_size > cap:
        return None
    with zf.open(info) as f:
        data = f.read(cap + 1)
    return None if len(data) > cap else data


def safe_folder(root, source):
    stem = re.sub(r"[^A-Za-z0-9._-]", "_", Path(source).stem)[:80].strip(".") or "report"
    folder = Path(root) / stem
    folder.mkdir(parents=True, exist_ok=True)
    return folder


def load_zip(path, limits, extract):
    size = path.stat().st_size
    if size > limits.max_zip_bytes:
        return None, f"{size} bytes, over --max-zip-bytes {limits.max_zip_bytes}"
    report, texts = Report(source=str(path)), {}
    try:
        with zipfile.ZipFile(path) as zf:
            infos = zf.infolist()
            if len(infos) > MAX_MEMBERS:
                report.notes.append(f"{len(infos)} members, only the first {MAX_MEMBERS} looked at")
            for info in infos[:MAX_MEMBERS]:
                # Only the four known names count, wherever the zip put them; a member's own path is
                # never used for anything, so nothing can be written outside --extract.
                name = info.filename.replace("\\", "/").rsplit("/", 1)[-1]
                if name not in KNOWN or info.is_dir():
                    report.notes.append(f"ignored member {info.filename[:80]!r}")
                    continue
                if name in report.sizes:
                    report.notes.append(f"second {name} ignored")
                    continue
                report.sizes[name] = info.file_size
                if name not in TEXT_MEMBERS and not extract:
                    continue
                data = read_member(zf, info, limits.max_member_bytes if name in TEXT_MEMBERS else limits.max_zip_bytes)
                if data is None:
                    report.notes.append(f"{name} ({info.file_size} bytes) over the read limit, not read")
                    continue
                if name in TEXT_MEMBERS:
                    texts[name] = data.decode("utf-8", "replace")
                if extract:
                    (safe_folder(extract, path) / name).write_bytes(data)
    except (zipfile.BadZipFile, zipfile.LargeZipFile, zlib.error, OSError, EOFError, RuntimeError,
            NotImplementedError, ValueError) as error:
        return None, f"unreadable zip ({type(error).__name__}: {str(error)[:120]})"
    parse(report, texts)
    return report, None


def read_tail(path, cap):
    size = path.stat().st_size
    with open(path, "rb") as f:
        if size > cap:
            f.seek(size - cap)
        return f.read(cap).decode("utf-8", "replace")


def load_folder(path, limits, extract):
    report, texts = Report(source=str(path)), {}
    # game.log: test runs name the game's log with --log-file.
    names = {name: path / name for name in KNOWN}
    if not names["melee_port.log"].is_file() and (path / "game.log").is_file():
        names["melee_port.log"] = path / "game.log"
    for name, file in names.items():
        if not file.is_file():
            continue
        size = file.stat().st_size
        report.sizes[name] = size
        if name in TEXT_MEMBERS:
            if size > limits.max_member_bytes:
                report.notes.append(f"{file.name}: only its last {limits.max_member_bytes} bytes read")
            texts[name] = read_tail(file, limits.max_member_bytes)
        if extract and size <= limits.max_zip_bytes:
            shutil.copyfile(file, safe_folder(extract, path) / name)
    parse(report, texts)
    return report, None


def parse(report, texts):
    txt, log = texts.get("melee_port_crash.txt", ""), texts.get("melee_port.log", "")
    head = HEAD.search(txt)
    if not head:
        matches = list(HEAD.finditer(log))
        head = matches[-1] if matches else None
        if head:
            report.notes.append("crash line taken from the log")
    if head:
        report.code = head.group(1).upper()
        report.module = head.group(3) or "?"
        report.offset = int(head.group(4), 16)
        report.version = head.group(5)
        report.frames.append(Frame(report.module, report.offset))
    else:
        fatal = FATAL.search(txt)
        if not fatal:
            matches = list(FATAL.finditer(log))
            fatal = matches[-1] if matches else None
            if fatal:
                report.notes.append("fatal line taken from the log")
        if fatal:
            report.code = "FATAL"
            report.version = fatal.group(4) or ""
            report.fatal_location = f"{fatal.group(1)}:{fatal.group(2)}" if fatal.group(1) else ""
            report.fatal_message = (fatal.group(3) or "").strip()
        else:
            report.notes.append("no CRASH or FATAL line")
    lines = log.splitlines()
    anchors = [i for i, line in enumerate(lines) if GAME_CRASH.search(line) or PANIC.search(line) or
               PRIVATE_GAME_CRASH.search(line)]
    crash_at = anchors[-1] if anchors else None
    # A forwarded report can contain a canonical crash line before the Source stack and another
    # versioned header after it. Prefer the last failure anchor that actually owns a stack.
    for candidate in reversed(anchors):
        found_stack = False
        for line in lines[candidate + 1:candidate + 64]:
            if STACK.search(line):
                found_stack = True
                break
            if HEAD.search(line) or FATAL.search(line):
                break
        if found_stack:
            crash_at = candidate
            break
    if crash_at is not None:
        game = GAME_CRASH.search(lines[crash_at]) or PRIVATE_GAME_CRASH.search(lines[crash_at])
        panic = PANIC.search(lines[crash_at])
        if not report.code and game:
            report.code = game.group(1).upper()
        if panic:
            report.code = report.code or "FATAL"
            report.fatal_location = report.fatal_location or f"{panic.group(1)}:{panic.group(2)}"
        for line in lines[crash_at + 1:crash_at + 64]:
            stack = STACK.search(line)
            if stack:
                report.frames.append(Frame(GAME_DLL, int(stack.group(1), 16)))
                continue
            access = ACCESS.search(line)
            if access:
                report.access = access.group(0)
            if "(leaves the game" in line or HEAD.search(line) or FATAL.search(line):
                break
    guest = LAST_GUEST.search(txt)
    if guest and int(guest.group(1), 16):
        name = guest.group(2).strip()
        if name in ("?", "(symbol omitted)"):
            name = ""
        report.frames.insert(min(1, len(report.frames)), Frame("guest", int(guest.group(1), 16), name=name))
    forced = FORCED.findall(log)
    if forced:
        report.forced_frame = int(forced[-1])
    if report.module == GAME_DLL or report.module == "melee_source.exe" or "source port: melee_game.dll" in log or \
       any(f.module == GAME_DLL for f in report.frames):
        report.engine = "Source Port"
    elif report.module.startswith("melee_port") or "boot: entering __start" in log:
        report.engine = "Static Recomp"
    else:
        report.engine = "?"


def discover(inputs, limits, skipped):
    found, seen, scanned = [], set(), 0
    for raw in inputs:
        path = Path(raw)
        if path.is_file():
            candidates = [path]
        elif path.is_dir() and (path / "melee_port_crash.txt").is_file():
            candidates = [path]
        elif path.is_dir():
            candidates = []
            with os.scandir(path) as entries:
                for entry in entries:
                    scanned += 1
                    if scanned > limits.max_scan:
                        skipped.append((str(path), f"stopped listing after {limits.max_scan} entries"))
                        break
                    try:
                        if entry.is_file() and entry.name.lower().endswith(".zip"):
                            candidates.append(Path(entry.path))
                        elif entry.is_dir() and (Path(entry.path) / "melee_port_crash.txt").is_file():
                            candidates.append(Path(entry.path))
                    except OSError:
                        continue
        else:
            skipped.append((raw, "not found"))
            continue
        for candidate in candidates:
            key = os.path.normcase(os.path.abspath(candidate))
            if key in seen:
                continue
            seen.add(key)
            try:
                stamp = (candidate / "melee_port_crash.txt" if candidate.is_dir() else candidate).stat().st_mtime
            except OSError:
                continue
            found.append((stamp, candidate))
    found.sort(key=lambda item: item[0], reverse=True)
    kept = [path for _, path in found[:limits.max_reports]]
    if len(found) > len(kept):
        skipped.append(("(older reports)", f"{len(found) - len(kept)} not read: over --max-reports {limits.max_reports}"))
    return kept


# ---- symbols ----

def pe_image_base(path):
    try:
        with open(path, "rb") as f:
            head = f.read(4096)
    except OSError:
        return None
    if len(head) < 0x40 or head[:2] != b"MZ":
        return None
    pe = int.from_bytes(head[0x3C:0x40], "little")
    if pe + 24 + 32 > len(head) or head[pe:pe + 4] != b"PE\0\0":
        return None
    optional = pe + 24
    magic = int.from_bytes(head[optional:optional + 2], "little")
    if magic == 0x20B:
        return int.from_bytes(head[optional + 24:optional + 32], "little")
    if magic == 0x10B:
        return int.from_bytes(head[optional + 28:optional + 32], "little")
    return None


def file_version(path):
    """The FileVersion string of a Windows executable's version resource (MELEE_PORT_VERSION)."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return None
    key = "FileVersion".encode("utf-16-le") + b"\0\0"
    at = data.rfind(key)
    while at >= 0 and at % 2:
        at = data.rfind(key, 0, at)
    if at < 0:
        return None
    start = at + len(key)
    while data[start:start + 2] == b"\0\0" and start < at + len(key) + 8:
        start += 2
    end = start
    while end + 2 <= len(data) and data[end:end + 2] != b"\0\0" and end - start < 64:
        end += 2
    value = data[start:end].decode("utf-16-le", "replace").strip()
    return value if re.fullmatch(r"[0-9A-Za-z.+_-]{1,32}", value) else None


@dataclasses.dataclass
class SymbolDir:
    path: Path
    version: str = ""

    @staticmethod
    def open(path):
        folder = SymbolDir(Path(path))
        for exe in VERSIONED_EXES:
            if (folder.path / exe).is_file():
                folder.version = file_version(folder.path / exe) or ""
                if folder.version:
                    break
        return folder

    def describe(self):
        parts = [name for name in ("melee_game.dbg",) if (self.path / name).is_file()]
        parts += sorted(p.name for p in self.path.glob("*.pdb"))[:6] + sorted(p.name for p in self.path.glob("*.map"))[:6]
        return f"{self.path} (version {self.version or 'unknown'}: {', '.join(parts) or 'no symbol files'})"


def find_addr2line(explicit):
    candidates = [explicit, os.environ.get("MELEE_ADDR2LINE"), shutil.which("addr2line")]
    candidates += sorted(glob.glob(str(Path.home() / "toolchains" / "*" / "mingw64" / "bin" / "addr2line.exe")), reverse=True)
    candidates += [r"C:\msys64\mingw64\bin\addr2line.exe", r"C:\msys64\ucrt64\bin\addr2line.exe", r"C:\msys64\usr\bin\addr2line.exe"]
    return next((c for c in candidates if c and Path(c).is_file()), None)


def short_where(where):
    where = re.sub(r"\s*\(discriminator \d+\)$", "", where or "").strip()
    if not where or where.startswith("??"):
        return ""
    file, _, line = where.rpartition(":")
    if not file:
        return Path(where.replace("\\", "/")).name
    name = Path(file.replace("\\", "/")).name
    return name if line in ("?", "0", "") else f"{name}:{line}"


def parse_addr2line(output):
    names = {}
    for line in output.splitlines():
        m = A2L.match(line.strip())
        if not m:
            continue
        function = m.group(2).strip()
        names[int(m.group(1), 16)] = ("" if function.startswith("??") else function, short_where(m.group(3)))
    return names


def run_addr2line(tool, dbg, addresses, timeout):
    request = "".join(f"0x{a:x}\n" for a in addresses)
    try:
        done = subprocess.run([tool, "-a", "-p", "-f", "-C", "-e", str(dbg)], input=request, capture_output=True,
                              text=True, errors="replace", timeout=timeout)
    except (OSError, subprocess.SubprocessError) as error:
        return {}, f"addr2line failed ({type(error).__name__})"
    return parse_addr2line(done.stdout), None


def undecorate(name):
    if not name.startswith("?"):
        return name
    if sys.platform == "win32":
        try:
            import ctypes
            dll = ctypes.WinDLL("dbghelp.dll")
            buffer = ctypes.create_unicode_buffer(512)
            if dll.UnDecorateSymbolNameW(ctypes.c_wchar_p(name), buffer, 512, 0x1000):   # UNDNAME_NAME_ONLY
                return buffer.value
        except (OSError, AttributeError):
            pass
    parts = name[1:].split("@@")[0].split("@")
    return "::".join(reversed(parts))


def load_map(path):
    preferred, symbols = 0x140000000, []
    for line in Path(path).read_text(errors="replace").splitlines():
        m = re.match(r"\s*Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            preferred = int(m.group(1), 16)
            continue
        m = MAP_LINE.match(line)
        if m:
            symbols.append((int(m.group(2), 16), m.group(1)))
    symbols.sort()
    return preferred, symbols


def map_names(path, offsets):
    preferred, symbols = load_map(path)
    starts = [address for address, _ in symbols]
    names = {}
    for offset in offsets:
        i = bisect.bisect_right(starts, preferred + offset) - 1
        # Past the last function by more than any function is long: not in this map's code.
        if i >= 0 and preferred + offset - starts[i] < 0x100000:
            names[offset] = (undecorate(symbols[i][1]), "")
    return names


def dbghelp_names(image, pdb, offsets):
    """PDB symbols through DbgHelp (Windows). Returns ({offset: (name, file:line)}, problem)."""
    if sys.platform != "win32":
        return {}, "DbgHelp needs Windows"
    import ctypes
    from ctypes import wintypes
    try:
        dll = ctypes.WinDLL("dbghelp.dll")
    except OSError:
        return {}, "dbghelp.dll not found"

    class SYMBOL_INFOW(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                    ("Index", wintypes.ULONG), ("Size", wintypes.ULONG), ("ModBase", ctypes.c_uint64),
                    ("Flags", wintypes.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64),
                    ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG), ("Tag", wintypes.ULONG),
                    ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG), ("Name", ctypes.c_wchar * 1)]

    class Symbol(ctypes.Structure):
        _fields_ = [("info", SYMBOL_INFOW), ("room", ctypes.c_wchar * 1024)]

    class LINE(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wintypes.DWORD),
                    ("FileName", ctypes.c_wchar_p), ("Address", ctypes.c_uint64)]

    class MODULE(ctypes.Structure):
        _fields_ = [("SizeOfStruct", wintypes.DWORD), ("BaseOfImage", ctypes.c_uint64), ("ImageSize", wintypes.DWORD),
                    ("TimeDateStamp", wintypes.DWORD), ("CheckSum", wintypes.DWORD), ("NumSyms", wintypes.DWORD),
                    ("SymType", ctypes.c_int), ("ModuleName", ctypes.c_wchar * 32), ("ImageName", ctypes.c_wchar * 256),
                    ("LoadedImageName", ctypes.c_wchar * 256), ("LoadedPdbName", ctypes.c_wchar * 256),
                    ("CVSig", wintypes.DWORD), ("CVData", ctypes.c_wchar * 780), ("PdbSig", wintypes.DWORD),
                    ("PdbSig70", ctypes.c_ubyte * 16), ("PdbAge", wintypes.DWORD), ("PdbUnmatched", wintypes.BOOL),
                    ("DbgUnmatched", wintypes.BOOL), ("LineNumbers", wintypes.BOOL), ("GlobalSymbols", wintypes.BOOL),
                    ("TypeInfo", wintypes.BOOL), ("SourceIndexed", wintypes.BOOL), ("Publics", wintypes.BOOL),
                    ("MachineType", wintypes.DWORD), ("Reserved", wintypes.DWORD)]

    dll.SymSetOptions.argtypes, dll.SymSetOptions.restype = [wintypes.DWORD], wintypes.DWORD
    dll.SymInitializeW.argtypes, dll.SymInitializeW.restype = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.BOOL], wintypes.BOOL
    dll.SymLoadModuleExW.argtypes = [wintypes.HANDLE, wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_uint64,
                                     wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    dll.SymLoadModuleExW.restype = ctypes.c_uint64
    dll.SymGetModuleInfoW64.argtypes, dll.SymGetModuleInfoW64.restype = [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(MODULE)], wintypes.BOOL
    dll.SymFromAddrW.argtypes = [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(Symbol)]
    dll.SymFromAddrW.restype = wintypes.BOOL
    dll.SymGetLineFromAddrW64.argtypes = [wintypes.HANDLE, ctypes.c_uint64, ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(LINE)]
    dll.SymGetLineFromAddrW64.restype = wintypes.BOOL
    dll.SymCleanup.argtypes, dll.SymCleanup.restype = [wintypes.HANDLE], wintypes.BOOL

    handle = wintypes.HANDLE(0x4D55)   # any unique value: no live process is read
    dll.SymSetOptions(0x2 | 0x10 | 0x200 | 0x80000)   # UNDNAME, LOAD_LINES, FAIL_CRITICAL_ERRORS, NO_PROMPTS
    folder = str(Path(pdb).parent)
    if not dll.SymInitializeW(handle, folder, False):
        return {}, "SymInitialize failed"
    try:
        base = (pe_image_base(image) if image else None) or 0x140000000
        loaded = dll.SymLoadModuleExW(handle, None, str(image or pdb), None, base, 0 if image else 0x7FFFFFFF, None, 0)
        if not loaded:
            return {}, "SymLoadModuleEx failed"
        module = MODULE()
        module.SizeOfStruct = ctypes.sizeof(MODULE)
        if not dll.SymGetModuleInfoW64(handle, loaded, ctypes.byref(module)) or module.SymType not in (3, 7):   # SymPdb, SymDia
            return {}, f"{Path(pdb).name} does not match {Path(image).name if image else 'the module'}"
        names = {}
        for offset in offsets:
            symbol = Symbol()
            symbol.info.SizeOfStruct = ctypes.sizeof(SYMBOL_INFOW)
            symbol.info.MaxNameLen = 1024
            displacement = ctypes.c_uint64()
            if not dll.SymFromAddrW(handle, loaded + offset, ctypes.byref(displacement), ctypes.byref(symbol)):
                continue
            name = ctypes.wstring_at(ctypes.addressof(symbol) + SYMBOL_INFOW.Name.offset, min(symbol.info.NameLen, 1024))
            line, line_displacement = LINE(), wintypes.DWORD()
            line.SizeOfStruct = ctypes.sizeof(LINE)
            where = ""
            if dll.SymGetLineFromAddrW64(handle, loaded + offset, ctypes.byref(line_displacement), ctypes.byref(line)) and line.FileName:
                where = f"{Path(line.FileName.replace(chr(92), '/')).name}:{line.LineNumber}"
            names[offset] = (name, where)
        return names, None
    finally:
        dll.SymCleanup(handle)


def choose_symbols(dirs, version, any_version):
    same = [d for d in dirs if d.version and d.version == version]
    if same:
        return same[0], ""
    unknown = [d for d in dirs if not d.version]
    if unknown:
        return unknown[0], "symbols of an unknown version"
    if any_version and dirs:
        return dirs[0], f"symbols are {dirs[0].version}, the report is {version or 'unknown'} (--any-version)"
    if dirs:
        return None, f"no symbols for version {version or 'unknown'} (have {', '.join(sorted({d.version for d in dirs}))})"
    return None, ""


def symbolize(reports, dirs, options, limits, problems):
    wanted = {}   # (folder, module) -> offsets
    for report in reports:
        folder, why = choose_symbols(dirs, report.version, options.any_version)
        if why:
            report.notes.append(why)
        if not folder:
            continue
        report.symbols = str(folder.path)
        for frame in report.frames:
            if frame.module != "guest" and frame.module != "?":
                wanted.setdefault((folder.path, frame.module.lower()), set()).add(frame.offset)
    tool = find_addr2line(options.addr2line)
    resolved = {}
    for (folder, module), offsets in sorted(wanted.items(), key=lambda item: (str(item[0][0]), item[0][1])):
        offsets = sorted(offsets)
        if len(offsets) > limits.max_addresses:
            problems.append(f"{module} in {folder}: {len(offsets)} addresses, only {limits.max_addresses} named")
            offsets = offsets[:limits.max_addresses]
        stem = module.rsplit(".", 1)[0]
        names, problem = {}, None
        if module == GAME_DLL:
            dbg = folder / "melee_game.dbg"
            if not dbg.is_file():
                problem = f"no melee_game.dbg in {folder}"
            elif not tool:
                problem = "no addr2line found (--addr2line or MELEE_ADDR2LINE)"
            else:
                base = pe_image_base(dbg) or GAME_BASE
                by_address, problem = run_addr2line(tool, dbg, [base + o for o in offsets], limits.timeout)
                names = {o: by_address[base + o] for o in offsets if base + o in by_address}
        elif (folder / f"{stem}.pdb").is_file():
            image = folder / module
            names, problem = dbghelp_names(image if image.is_file() else None, folder / f"{stem}.pdb", offsets)
            if problem and (folder / f"{stem}.map").is_file():
                names, problem = map_names(folder / f"{stem}.map", offsets), None
        elif (folder / f"{stem}.map").is_file():
            names = map_names(folder / f"{stem}.map", offsets)
        elif module.endswith(".exe") and module.startswith(("melee", "meleeunlocked")):
            problem = f"no {stem}.pdb or {stem}.map in {folder}"
        if problem:
            problems.append(problem)
        resolved[(folder, module)] = names
    for report in reports:
        if not report.symbols:
            continue
        for frame in report.frames:
            if frame.module in ("guest", "?"):
                continue   # guest frames carry their own names
            frame.name, frame.where = resolved.get((Path(report.symbols), frame.module.lower()), {}).get(frame.offset, ("", ""))


# ---- summary ----

def group(reports, top):
    groups = {}
    for report in reports:
        groups.setdefault(report.signature(top), []).append(report)
    return sorted(groups.values(), key=lambda members: (-len(members), members[0].signature(top)))


def triage(inputs, options, limits):
    skipped, problems, reports = [], [], []
    for path in discover(inputs, limits, skipped):
        if path.is_dir():
            report, why = load_folder(path, limits, options.extract)
        else:
            report, why = load_zip(path, limits, options.extract)
        if report is None:
            skipped.append((str(path), why))
        else:
            reports.append(report)
    dirs = [SymbolDir.open(d) for d in options.symbols or []]
    symbolize(reports, dirs, options, limits, problems)
    return {"reports": reports, "groups": group(reports, options.top), "skipped": skipped,
            "problems": sorted(set(problems)), "symbols": dirs}


def counts(values):
    tally = {}
    for value in values:
        tally[value] = tally.get(value, 0) + 1
    return ", ".join(f"{k} x{v}" for k, v in sorted(tally.items(), key=lambda kv: (-kv[1], kv[0])))


def print_summary(result, top, out=sys.stdout):
    reports, groups = result["reports"], result["groups"]
    print(f"crash_triage: {len(reports)} report(s) read, {len(result['skipped'])} skipped, {len(groups)} group(s)", file=out)
    for folder in result["symbols"]:
        print("  symbols: " + folder.describe(), file=out)
    if not result["symbols"]:
        print("  symbols: none given (--symbols DIR names the frames; they still group without)", file=out)
    for number, members in enumerate(groups, 1):
        first = members[0]
        code = f"{first.code or '?'} {EXCEPTIONS.get(first.code, '')}".strip()
        forced = sum(1 for r in members if r.forced_frame)
        tag = f" | forced test crash x{forced}" if forced else ""
        print(f"\n[{number}] {len(members)} report(s) | {code} | {counts(r.engine for r in members)} | "
              f"{counts(r.version or '?' for r in members)}{tag}", file=out)
        for frame in first.frames[:top] or [Frame("?")]:
            print("      " + frame.label(), file=out)
        if first.access:
            print("      " + first.access, file=out)
        if first.fatal_location:
            message = f": {first.fatal_message}" if first.fatal_message else ""
            print("      assertion: " + first.fatal_location + message, file=out)
        # A folder report shows its parent too: test runs leave many folders with the same name.
        names = [Path(r.source).name if Path(r.source).suffix.lower() == ".zip" else "/".join(Path(r.source).parts[-2:])
                 for r in members]
        more = f" (+{len(names) - 4} more)" if len(names) > 4 else ""
        print("      " + ", ".join(names[:4]) + more, file=out)
        notes = sorted({note for r in members for note in r.notes})
        for note in notes[:4]:
            print("      note: " + note, file=out)
    if result["skipped"]:
        print("\nskipped:", file=out)
        for source, why in result["skipped"][:40]:
            print(f"  {Path(source).name or source}: {why}", file=out)
    if result["problems"]:
        print("\nsymbols:", file=out)
        for problem in result["problems"][:20]:
            print("  " + problem, file=out)


def to_json(result, top):
    def frame(f):
        return {"module": f.module, "offset": f.offset, "name": f.name, "where": f.where, "label": f.label()}
    return {
        "reports": [{"source": r.source, "code": r.code, "version": r.version, "engine": r.engine, "module": r.module,
                     "offset": r.offset, "access": r.access, "fatal_location": r.fatal_location,
                     "fatal_message": r.fatal_message, "forced_frame": r.forced_frame, "sizes": r.sizes,
                     "symbols": r.symbols, "notes": r.notes, "frames": [frame(f) for f in r.frames]}
                    for r in result["reports"]],
        "groups": [{"count": len(m), "code": m[0].code, "signature": list(m[0].signature(top)),
                    "fatal_location": m[0].fatal_location, "fatal_message": m[0].fatal_message,
                    "frames": [frame(f) for f in m[0].frames[:top]], "reports": [r.source for r in m]}
                   for m in result["groups"]],
        "skipped": [{"source": s, "reason": why} for s, why in result["skipped"]],
        "problems": result["problems"],
    }


# ---- self-test (synthetic reports, no game files) ----

def self_test():
    failures = []

    def expect(ok, what):
        print(("ok    " if ok else "FAIL  ") + what)
        if not ok:
            failures.append(what)

    def stored_zip(path, members, method=zipfile.ZIP_STORED):
        with zipfile.ZipFile(path, "w", method) as zf:
            for name, data in members.items():
                zf.writestr(name, data)

    def source_port_log(fault, stack, version="0.8.5", extra=""):
        lines = ["source port: melee_game.dll at 0000000082800000, MEM1 40 MB at 80000000", extra,
                 f"game crash C0000005 at melee_game.dll+0x{fault:X}", "  reading address 0000000000000010",
                 "  rax 0000000000000000 rbx 0000000000000000 rcx 0000000000000000 rdx 0000000000000000"]
        lines += [f"  stack: melee_game.dll+0x{o:X}" for o in stack]
        lines += ["  (leaves the game at 00007FF7C7F5B6FF)",
                  f"CRASH: exception C0000005 at {0x82800000 + fault:016X} (melee_game.dll+0x{fault:X}), version {version}"]
        return "\n".join(lines) + "\n"

    def crash_txt(module, offset, version, guest="00000000 ?"):
        base = GAME_BASE if module == GAME_DLL else 0x140000000
        return (f"CRASH: exception C0000005 at {base + offset:016X} ({module}+0x{offset:X}), version {version}\n"
                f"last guest function {guest}, lr 00000000\nrecent guest functions (oldest first):\n")

    got = parse_addr2line("0x0000000082b157d3: _tyList_80313508 at C:/src/melee/ty/tylist.c:456\n"
                          "0x00000000828274ca: ?? at ledgedash.c:?\n0x0000000082800010: ?? ??:0\n")
    expect(got == {0x82B157D3: ("_tyList_80313508", "tylist.c:456"), 0x828274CA: ("", "ledgedash.c"), 0x82800010: ("", "")},
           "addr2line output parsed (named, file only, unknown)")

    fatal_txt = ("FATAL: game stopped at C:/src/melee/lb/lbarchive.c:94: , version 0.8.62\r\n"
                 "last guest function 00000000 ?, lr 00000000\r\n")
    fatal_log = ("source port: melee_game.dll at 0000000082800000\n"
                 "game panic at C:/src/melee/lb/lbarchive.c:94\n"
                 "  stack: melee_game.dll+0x39B749\n"
                 "  stack: melee_game.dll+0x280E88\n"
                 "FATAL: game stopped at C:/src/melee/lb/lbarchive.c:94: \n")
    fatal_report = Report("fatal")
    parse(fatal_report, {"melee_port_crash.txt": fatal_txt, "melee_port.log": fatal_log})
    expect(fatal_report.code == "FATAL" and fatal_report.version == "0.8.62" and
           fatal_report.fatal_location == "C:/src/melee/lb/lbarchive.c:94" and fatal_report.fatal_message == "" and
           fatal_report.engine == "Source Port" and
           [f.offset for f in fatal_report.frames] == [0x39B749, 0x280E88],
           "fatal panic keeps its version, assertion and Source stack without an exception code")
    fatal_fallback = Report("fatal-log")
    parse(fatal_fallback, {"melee_port.log": fatal_log +
          "FATAL: game stopped at C:/src/melee/lb/lbarchive.c:94: missing public symbol, version 0.8.62\n"})
    expect(fatal_fallback.version == "0.8.62" and fatal_fallback.fatal_message == "missing public symbol" and
           "fatal line taken from the log" in fatal_fallback.notes,
           "fatal panic falls back to the last fatal log line and preserves its message")
    other_panic = dataclasses.replace(fatal_report, fatal_location="C:/src/melee/lb/lbarchive.c:73")
    expect(len(group([fatal_report, fatal_fallback, other_panic], 3)) == 2,
           "different assertions stay separate despite identical panic wrapper frames")

    with tempfile.TemporaryDirectory(prefix="crash-triage-") as temp:
        temp = Path(temp)
        symbols = temp / "symbols-0.8.5"
        symbols.mkdir()
        # Just enough of a version resource for file_version(); symbol files are /MAP text.
        (symbols / "melee_source.exe").write_bytes(b"MZ" + bytes(62) + "FileVersion".encode("utf-16-le") + bytes(4) +
                                                   "0.8.5".encode("utf-16-le") + bytes(2))
        map_text = (" melee_source\n\n Preferred load address is 0000000140000000\n\n"
                    "  Address         Publics by Value              Rva+Base               Lib:Object\n\n"
                    " 0001:00000000       ?host_frame@@YAXXZ 0000000140001000 f   host.obj\n"
                    " 0001:00001000       ?arm_test_crash@@YAX_N@Z 0000000140002000 f   main.obj\n"
                    " 0001:00002000       g_counter 0000000140002800     main.obj\n"
                    " 0001:00003000       ?crash_filter@@YAJPEAU_EXCEPTION_POINTERS@@@Z 0000000140004000 f   main.obj\n")
        (symbols / "melee_source.map").write_text(map_text)
        (symbols / "melee_port.map").write_text(map_text.replace("melee_source", "melee_port"))
        expect(file_version(symbols / "melee_source.exe") == "0.8.5", "version read from the FileVersion resource")

        reports = temp / "reports"
        reports.mkdir()
        same_stack = [0x274CA, 0x7FEB4, 0x800CC]
        for name in ("sp-a.zip", "sp-b.zip"):
            stored_zip(reports / name, {"melee_port_crash.txt": crash_txt(GAME_DLL, 0x3157D3, "0.8.5"),
                                        "melee_port_crash.dmp": b"MDMP" + bytes(64),
                                        "melee_port.log": source_port_log(0x3157D3, same_stack),
                                        "lobby.log": "lobby: online\n"})
        stored_zip(reports / "sp-other.zip", {"melee_port.log": source_port_log(0x1234, [0x5678])})
        stored_zip(reports / "forced.zip", {"melee_port_crash.txt": crash_txt("melee_source.exe", 0x2010, "0.8.5"),
                                            "melee_port.log": "source port: melee_game.dll at 0000000082800000\n"
                                                              "test: forced crash at frame 300\n"})
        stored_zip(reports / "old.zip", {"melee_port_crash.txt": crash_txt("melee_source.exe", 0x2010, "0.8.1")})
        stored_zip(reports / "recomp.zip", {"melee_port_crash.txt": crash_txt("melee_port.exe", 0x4010, "0.8.5",
                                                                               "8006B7F8 ftCo_800693AC")})
        (reports / "not-a-zip.zip").write_bytes(b"this is not a zip file")
        stored_zip(reports / "too-big.zip", {"melee_port_crash.dmp": bytes(300 * 1024)})
        stored_zip(reports / "bomb.zip", {"melee_port_crash.txt": crash_txt("melee_game.dll", 0x10, "0.8.5"),
                                          "melee_port.log": bytes(2 << 20)}, zipfile.ZIP_DEFLATED)
        stored_zip(reports / "odd.zip", {"../escape.txt": "x", "settings/port-settings.ini": "x",
                                         "MeleeUnlocked/melee_port_crash.txt": crash_txt("melee_game.dll", 0x20, "0.8.5")})
        folder = reports / "extracted-report"
        folder.mkdir()
        (folder / "melee_port_crash.txt").write_text(crash_txt("ucrtbase.dll", 0x48AC3, "0.8.5"))
        (folder / "game.log").write_text("source port: melee_game.dll at 0000000082800000\n")
        (reports / "notes.txt").write_text("not a report")

        extract = temp / "extracted"
        options = argparse.Namespace(symbols=[str(symbols)], any_version=False, addr2line=None, top=3, extract=extract)
        limits = Limits(max_zip_bytes=256 * 1024, max_member_bytes=1 << 20)
        from crash_report_privacy import private_text

        # Legacy reports forwarded through the privacy allowlist contain canonical module offsets,
        # no absolute PC or paths, and no binary dump. Stored text members need no external link.
        forwarded = temp / "private-source.zip"
        stored_zip(forwarded, {"melee_port_crash.txt": private_text("melee_port_crash.txt",
                   crash_txt(GAME_DLL, 0x3157D3, "0.8.5").encode()),
                   "melee_port.log": private_text("melee_port.log", source_port_log(0x3157D3, same_stack).encode())})
        private_source, why = load_zip(forwarded, limits, None)
        expect(not why and private_source.code == "C0000005" and private_source.version == "0.8.5" and
               private_source.engine == "Source Port" and
               [f.offset for f in private_source.frames] == [0x3157D3, *same_stack] and
               "melee_port_crash.dmp" not in private_source.sizes,
               "a stored privacy-forwarded legacy Source ZIP keeps module offsets, version and stack")

        forwarded_fatal = temp / "private-fatal.zip"
        stored_zip(forwarded_fatal, {"melee_port_crash.txt": private_text("melee_port_crash.txt", fatal_txt.encode()),
                                    "melee_port.log": private_text("melee_port.log", fatal_log.encode())})
        private_fatal, why = load_zip(forwarded_fatal, limits, None)
        expect(not why and private_fatal.code == "FATAL" and private_fatal.version == "0.8.62" and
               private_fatal.fatal_location == "lbarchive.c:94" and private_fatal.engine == "Source Port" and
               [f.offset for f in private_fatal.frames] == [0x39B749, 0x280E88],
               "a privacy-forwarded fatal ZIP keeps its basename assertion and Source stack")

        forwarded_static = temp / "private-static.zip"
        stored_zip(forwarded_static, {"melee_port_crash.txt":
                   "CRASH: exception C0000005 at melee_port.exe+0x4010, version 0.8.5\n"
                   "last guest function 8006B7F8 (symbol omitted), lr 80000000\n"})
        private_static = triage([str(forwarded_static)], options, limits)["reports"][0]
        expect(private_static.frames[0].name == "crash_filter" and private_static.frames[1].module == "guest" and
               private_static.frames[1].offset == 0x8006B7F8 and private_static.frames[1].name == "" and
               private_static.frames[1].label() == "guest+0x8006B7F8",
               "canonical host module offsets still symbolize and omitted guest symbols retain only the address")

        fatal_problems = []
        symbolize([fatal_report], [SymbolDir.open(symbols)], options, limits, fatal_problems)
        expect(not fatal_report.symbols and all(not f.name for f in fatal_report.frames) and
               any("0.8.62" in note for note in fatal_report.notes),
               "a fatal report's version prevents naming its stack with a different build")
        result = triage([str(reports)], options, limits)
        by_name = {Path(r.source).name: r for r in result["reports"]}
        skipped = dict((Path(s).name, why) for s, why in result["skipped"])

        expect(set(by_name) == {"sp-a.zip", "sp-b.zip", "sp-other.zip", "forced.zip", "old.zip", "recomp.zip",
                                "bomb.zip", "odd.zip", "extracted-report"}, "every readable report was read")
        expect("not-a-zip.zip" in skipped and "unreadable" in skipped["not-a-zip.zip"], "a broken zip is skipped")
        expect("too-big.zip" in skipped and "--max-zip-bytes" in skipped["too-big.zip"], "an oversized zip is skipped")
        first = next((g for g in result["groups"] if Path(g[0].source).name in ("sp-a.zip", "sp-b.zip")), [])
        expect(sorted(Path(r.source).name for r in first) == ["sp-a.zip", "sp-b.zip"], "same stack, one group of two")
        expect([f.key() for f in by_name["sp-a.zip"].frames] == ["melee_game.dll+0x3157D3", "melee_game.dll+0x274CA",
                                                                  "melee_game.dll+0x7FEB4", "melee_game.dll+0x800CC"],
               "game frames: the fault, then the unwound stack")
        expect(by_name["sp-a.zip"].engine == "Source Port" and by_name["sp-a.zip"].access == "reading address 0000000000000010",
               "Source Port crash details")
        expect(by_name["sp-other.zip"].code == "C0000005" and "crash line taken from the log" in by_name["sp-other.zip"].notes,
               "a report without its crash file still reads from the log")
        forced = by_name["forced.zip"]
        expect(forced.forced_frame == 300 and forced.frames and forced.frames[0].name == "arm_test_crash",
               "forced test crash marked and named from the /MAP file")
        expect(by_name["old.zip"].frames[0].name == "" and any("0.8.1" in n for n in by_name["old.zip"].notes),
               "an old report is not named with new symbols")
        recomp = by_name["recomp.zip"]
        expect([f.label() for f in recomp.frames] == ["melee_port.exe+0x4010  crash_filter", "guest ftCo_800693AC"] and
               recomp.engine == "Static Recomp", "Static Recomp: host frame named, guest function kept")
        expect(any("over the read limit" in n for n in by_name["bomb.zip"].notes) and by_name["bomb.zip"].code == "C0000005",
               "an oversized member is not inflated; the rest of the report still reads")
        odd = by_name["odd.zip"]
        expect(sum("ignored member" in n for n in odd.notes) == 2 and odd.code == "C0000005", "unknown members ignored")
        written = sorted(p.relative_to(extract).as_posix() for p in extract.rglob("*") if p.is_file())
        expect(all(p.split("/")[-1] in KNOWN for p in written) and "odd/melee_port_crash.txt" in written and
               not (temp / "escape.txt").exists(), "--extract writes only the known file names, inside its folder")
        expect(by_name["extracted-report"].engine == "Source Port" and by_name["extracted-report"].frames[0].key() ==
               "ucrtbase.dll+0x48AC3", "an extracted report folder reads too (game.log from test runs)")

        capped = triage([str(reports)], options, Limits(max_reports=3, max_zip_bytes=256 * 1024, max_member_bytes=1 << 20))
        expect(len(capped["reports"]) + sum(1 for s, _ in capped["skipped"] if s != "(older reports)") == 3 and
               any("--max-reports" in why for _, why in capped["skipped"]), "--max-reports caps the work")
        text = []
        print_summary(result, 3, out=_Lines(text))
        expect(any("forced test crash" in line for line in text) and any("skipped:" in line for line in text),
               "summary prints groups, forced crashes and skips")
        json.dumps(to_json(result, 3))
    print("self-test: " + ("all checks passed" if not failures else f"{len(failures)} failed"))
    return 0 if not failures else 1


class _Lines:
    def __init__(self, sink):
        self.sink = sink

    def write(self, text):
        self.sink.extend(text.splitlines())


def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="*", help="report zips, folders of them, or extracted report folders")
    parser.add_argument("--symbols", action="append", metavar="DIR", help="a folder with the crashed build's symbols")
    parser.add_argument("--addr2line", metavar="EXE", help="GCC addr2line for melee_game.dbg")
    parser.add_argument("--any-version", action="store_true", help="use symbols even when their version differs")
    parser.add_argument("--top", type=int, default=3, help="frames that decide a group (3)")
    parser.add_argument("--json", metavar="FILE", help="also write the result as JSON")
    parser.add_argument("--extract", metavar="DIR", help="write each report's files to DIR/<report>/")
    parser.add_argument("--max-reports", type=int, default=Limits.max_reports)
    parser.add_argument("--max-zip-bytes", type=int, default=Limits.max_zip_bytes)
    parser.add_argument("--max-member-bytes", type=int, default=Limits.max_member_bytes)
    parser.add_argument("--max-addresses", type=int, default=Limits.max_addresses)
    parser.add_argument("--timeout", type=float, default=Limits.timeout, help="seconds per symbolizer call")
    parser.add_argument("--self-test", action="store_true", help="run on synthetic reports and exit")
    a = parser.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.inputs:
        parser.error("give a report zip, a folder of them, or --self-test")
    a.top = max(1, a.top)
    a.extract = Path(a.extract) if a.extract else None
    limits = Limits(max_reports=max(1, a.max_reports), max_zip_bytes=a.max_zip_bytes, max_member_bytes=a.max_member_bytes,
                    max_addresses=max(1, a.max_addresses), timeout=a.timeout)
    result = triage(a.inputs, a, limits)
    print_summary(result, a.top)
    if a.json:
        Path(a.json).write_text(json.dumps(to_json(result, a.top), indent=2), encoding="utf-8")
    return 0 if result["reports"] else 1


if __name__ == "__main__":
    sys.exit(main())
