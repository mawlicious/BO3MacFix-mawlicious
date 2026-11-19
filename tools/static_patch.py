#!/usr/bin/env python3
"""Build the static (Rosetta-AOT-preserving) patch for CoDBlkOps3_Exe.

Rosetta 2 drops the ahead-of-time translation of the whole game image as soon
as any __TEXT page is made writable at runtime. This tool bakes every patch
the mod would otherwise apply at runtime into a copy of the executable:

  * byte patches are written directly;
  * each inline hook site becomes `jmp rel32` to a stub in a __TEXT cave:
        cmp qword [rip+slot], 0 ; je trampoline ; jmp [rip+slot]
    The slot is an 8-byte pointer in the zero-filled slack at the end of
    __DATA, written by the dylib at runtime (data, not code). The trampoline
    holds the relocated original instructions and jumps back to the function.

Inputs:  the original executable and a recording made with
         BO3MACFIX_RECORD_PATCHES=<file> (see source/patch_layer.c).
Outputs: source/static_manifest.h (compiled into the dylib),
         assets/static-patch.txt (applied by the installer/launcher),
         optionally the patched executable itself (--write-exe, dev only).

Development-time only: needs Xcode's llvm-objdump.
"""
import argparse
import hashlib
import re
import struct
import subprocess
import sys

BASE = 0x100000000
TEXT_CAVE = (0x1964080, 0x1965000)       # zero slack after __eh_frame
SLOT_AREA = (0x1a268710, 0x1a269000)     # zero slack after __common (VM only)
MARKER_PREFIX = b"BO3MACFIX-MAWLICIOUS-STATIC-1:"
ORIGINAL_SHA256 = "932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0"


def objdump(path, start, length):
    out = subprocess.run(
        ["xcrun", "llvm-objdump", "-d", "--x86-asm-syntax=att",
         f"--start-address={BASE + start:#x}", f"--stop-address={BASE + start + length:#x}", path],
        check=True, capture_output=True, text=True).stdout
    insns = []
    for line in out.splitlines():
        m = re.match(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$", line)
        if not m:
            continue
        addr = int(m.group(1), 16) - BASE
        raw = bytes.fromhex(m.group(2).replace(" ", ""))
        insns.append((addr, raw, m.group(3).strip()))
    return insns


def rel(target, next_ip):
    d = target - next_ip
    if not -2**31 <= d < 2**31:
        raise ValueError("rel32 out of range")
    return struct.pack("<i", d)


def relocate(insn, new_addr):
    """Return bytes for `insn` (addr, raw, text) placed at new_addr."""
    addr, raw, text = insn
    end = addr + len(raw)
    op = raw[0]
    # rel32 call / jmp
    if op in (0xE8, 0xE9) and len(raw) == 5:
        target = end + struct.unpack("<i", raw[1:5])[0]
        return bytes([op]) + rel(target, new_addr + 5)
    # short jmp -> near jmp
    if op == 0xEB and len(raw) == 2:
        target = end + struct.unpack("<b", raw[1:2])[0]
        return b"\xE9" + rel(target, new_addr + 5)
    # short jcc -> near jcc
    if 0x70 <= op <= 0x7F and len(raw) == 2:
        target = end + struct.unpack("<b", raw[1:2])[0]
        return bytes([0x0F, 0x80 + (op - 0x70)]) + rel(target, new_addr + 6)
    # near jcc
    if op == 0x0F and len(raw) == 6 and 0x80 <= raw[1] <= 0x8F:
        target = end + struct.unpack("<i", raw[2:6])[0]
        return raw[:2] + rel(target, new_addr + 6)
    if re.search(r"\b(loop|jrcxz|jecxz|xbegin)\b", text) or (op in (0xE8, 0xE9, 0xEB) or 0x70 <= op <= 0x7F):
        raise ValueError(f"unsupported relative instruction at {addr:#x}: {text}")
    if "(%rip)" in text:
        m = re.search(r"#\s*0x([0-9a-f]+)", text)
        if not m:
            raise ValueError(f"rip-relative without target at {addr:#x}: {text}")
        target = int(m.group(1), 16) - BASE
        disp = struct.pack("<i", target - end)
        hits = [i for i in range(len(raw) - 3) if raw[i:i + 4] == disp]
        if len(hits) != 1:
            raise ValueError(f"cannot locate unique disp32 at {addr:#x}: {text}")
        i = hits[0]
        # displacement is relative to the end of the instruction
        return raw[:i] + rel(target, new_addr + len(raw)) + raw[i + 4:]
    return raw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("original")
    ap.add_argument("recording")
    ap.add_argument("--header", default="source/static_manifest.h")
    ap.add_argument("--patch", default="assets/static-patch.txt")
    ap.add_argument("--write-exe")
    args = ap.parse_args()

    orig = open(args.original, "rb").read()
    if hashlib.sha256(orig).hexdigest() != ORIGINAL_SHA256:
        sys.exit("unexpected executable (sha256 mismatch)")
    buf = bytearray(orig)

    records, seen = [], set()
    for line in open(args.recording):
        parts = line.split()
        if not parts:
            continue
        key = tuple(parts[:3]) + (tuple(parts[3:4]),)
        if key in seen:
            continue  # several launches may append to the same recording
        seen.add(key)
        records.append(parts)

    entries = []          # (kind, offset, length, original_bytes, slot, trampoline)
    copies = []           # (dst, src, len): verbatim original instruction bytes in trampolines
    touched = {}          # offset -> True for all modified bytes (overlap check)

    def mark(off, n, what):
        for i in range(off, off + n):
            if i in touched:
                raise SystemExit(f"overlapping patches at {i:#x} ({what})")
            touched[i] = True

    # byte patches first, in recorded order
    for p in records:
        if p[0] != "code":
            continue
        off, n = int(p[1], 16), int(p[2])
        new, old = bytes.fromhex(p[3]), bytes.fromhex(p[4])
        if bytes(orig[off:off + n]) != old and bytes(buf[off:off + n]) != old:
            raise SystemExit(f"recorded original bytes differ at {off:#x}")
        entries.append((2, off, n, bytes(orig[off:off + n]), 0, 0))
        buf[off:off + n] = new
        mark(off, n, "code")

    cave = (TEXT_CAVE[0] + 15) & ~15
    marker_off = cave
    cave += 64
    slot = (SLOT_AREA[0] + 7) & ~7
    hooks = sorted({int(p[1], 16) for p in records if p[0] == "hook"})
    for site in hooks:
        insns, total = [], 0
        for insn in objdump(args.original, site, 32):
            if insn[0] != site + total:
                break
            insns.append(insn)
            total += len(insn[1])
            if total >= 5:
                break
        if total < 5:
            raise SystemExit(f"could not decode prologue at {site:#x}")
        if bytes(buf[site:site + total]) != bytes(orig[site:site + total]):
            raise SystemExit(f"hook prologue overlaps a byte patch at {site:#x}")
        stub = cave
        tramp = stub + 20
        code = b""
        for insn in insns:
            moved = relocate(insn, tramp + len(code))
            if moved == insn[1]:
                copies.append((tramp + len(code), insn[0], len(moved)))
            code += moved
        code += b"\xE9" + rel(site + total, tramp + len(code) + 5)
        if slot + 8 > SLOT_AREA[1]:
            raise SystemExit("slot area exhausted")
        stub_bytes = (b"\x48\x83\x3D" + rel(slot, stub + 8) + b"\x00" +   # cmp qword [rip+slot], 0
                      b"\x0F\x84" + rel(tramp, stub + 14) +                # je trampoline
                      b"\xFF\x25" + rel(slot, stub + 20))                  # jmp [rip+slot]
        assert len(stub_bytes) == 20
        end = (tramp + len(code) + 15) & ~15
        if end > TEXT_CAVE[1]:
            raise SystemExit("code cave exhausted")
        buf[stub:stub + 20] = stub_bytes
        buf[tramp:tramp + len(code)] = code
        site_bytes = b"\xE9" + rel(stub, site + 5) + b"\xCC" * (total - 5)
        entries.append((1, site, total, bytes(orig[site:site + total]), slot, tramp))
        buf[site:site + total] = site_bytes
        mark(site, total, "hook")
        cave = end
        slot += 8

    layout = hashlib.sha256(bytes(buf)).hexdigest()[:24].encode()
    marker = MARKER_PREFIX + layout + b"\0"
    buf[marker_off:marker_off + len(marker)] = marker

    # Patch list: every byte range that differs from the original.
    diffs, i = [], 0
    while i < len(buf):
        if buf[i] != orig[i]:
            j = i
            while j < len(buf) and buf[j] != orig[j]:
                j += 1
            diffs.append((i, bytes(buf[i:j])))
            i = j
        else:
            i += 1
    patched_sha = hashlib.sha256(bytes(buf)).hexdigest()
    # Original game instructions are never shipped: bytes copied verbatim from
    # the user's own executable are emitted as "c dst src len" directives.
    copied = {}
    for dst, src, n in copies:
        for i in range(n):
            copied[dst + i] = True
    with open(args.patch, "w") as f:
        f.write("# BO3MacFix-mawlicious static executable patch (generated by tools/static_patch.py)\n")
        f.write("# w <offset> <hex>: write bytes; c <dst> <src> <len>: copy bytes from the original executable\n")
        f.write(f"original-sha256 {ORIGINAL_SHA256}\npatched-sha256 {patched_sha}\n")
        for off, data in diffs:
            i = 0
            while i < len(data):
                j = i
                while j < len(data) and (off + j in copied) == (off + i in copied):
                    j += 1
                if off + i not in copied:
                    f.write(f"w {off + i:x} {data[i:j].hex()}\n")
                i = j
        for dst, src, n in copies:
            f.write(f"c {dst:x} {src:x} {n:x}\n")
    # self-check: rebuilding from the patch file reproduces the image
    rebuilt = bytearray(orig)
    for line in open(args.patch):
        p = line.split()
        if p and p[0] == "w":
            o, d = int(p[1], 16), bytes.fromhex(p[2]); rebuilt[o:o + len(d)] = d
        elif p and p[0] == "c":
            d, s_, n = (int(x, 16) for x in p[1:4]); rebuilt[d:d + n] = orig[s_:s_ + n]
    if hashlib.sha256(bytes(rebuilt)).hexdigest() != patched_sha:
        sys.exit("patch file does not reproduce the patched image")

    with open(args.header, "w") as f:
        f.write("/* Generated by tools/static_patch.py -- do not edit by hand. */\n#pragma once\n#include <stdint.h>\n")
        f.write("#define BO3_STATIC_HOOK 1\n#define BO3_STATIC_CODE 2\n")
        f.write("/* Original bytes are read from the user's own CoDBlkOps3_Exe at runtime. */\n")
        f.write("typedef struct {\n    uint8_t kind;\n    uint32_t offset, length;\n"
                "    uint32_t slot, trampoline;\n} bo3_static_entry;\n")
        f.write(f"#define BO3_STATIC_MARKER_OFFSET 0x{marker_off:x}u\n")
        f.write(f"#define BO3_STATIC_MARKER \"{marker[:-1].decode()}\"\n")
        f.write(f"#define BO3_STATIC_PATCHED_SHA256 \"{patched_sha}\"\n")
        f.write(f"#define BO3_STATIC_ENTRY_COUNT {len(entries)}u\n")
        f.write("static const bo3_static_entry bo3_static_entries[] = {\n")
        for n, e in enumerate(entries):
            f.write(f"    {{{e[0]}, 0x{e[1]:x}u, {e[2]}u, 0x{e[4]:x}u, 0x{e[5]:x}u}},\n")
        f.write("};\n")

    if args.write_exe:
        with open(args.write_exe, "wb") as f:
            f.write(bytes(buf))
    print(f"code patches: {sum(1 for e in entries if e[0] == 2)}, hooks: {len(hooks)}, "
          f"cave used: {cave - TEXT_CAVE[0]}/{TEXT_CAVE[1] - TEXT_CAVE[0]} bytes, slots: {len(hooks)}, "
          f"diff ranges: {len(diffs)}, patched sha256 {patched_sha}")


if __name__ == "__main__":
    main()
