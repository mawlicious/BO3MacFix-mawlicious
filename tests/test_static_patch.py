"""Unit tests for tools/static_patch.py instruction relocation (no game files)."""
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "tools"))
import static_patch as sp  # noqa: E402

# plain instruction: copied verbatim
assert sp.relocate((0x1000, b"\x55", "pushq %rbp"), 0x5000) == b"\x55"
# rel32 call keeps its absolute target
raw = b"\xE8" + struct.pack("<i", 0x100)
out = sp.relocate((0x1000, raw, "callq 0x1000105"), 0x9000)
assert out[0] == 0xE8 and 0x9000 + 5 + struct.unpack("<i", out[1:])[0] == 0x1000 + 5 + 0x100
# short jcc becomes a near jcc with the same target
out = sp.relocate((0x2000, b"\x74\x10", "je 0x2012"), 0x7000)
assert out[:2] == b"\x0F\x84" and 0x7000 + 6 + struct.unpack("<i", out[2:])[0] == 0x2012
# short jmp becomes jmp rel32
out = sp.relocate((0x2000, b"\xEB\xFE", "jmp 0x2000"), 0x7000)
assert out[0] == 0xE9 and 0x7000 + 5 + struct.unpack("<i", out[1:])[0] == 0x2000
# RIP-relative load keeps its absolute data target
disp = 0x1234
raw = b"\x48\x8B\x05" + struct.pack("<i", disp)            # mov rax, [rip+disp]
target = 0x3000 + len(raw) + disp
out = sp.relocate((0x3000, raw, f"movq {disp:#x}(%rip), %rax  # 0x{sp.BASE + target:x}"), 0x8000)
assert out[:3] == raw[:3] and 0x8000 + len(raw) + struct.unpack("<i", out[3:])[0] == target
# unsupported relative forms are refused, not guessed
try:
    sp.relocate((0x4000, b"\xE2\x10", "loop 0x4012"), 0x8000)
    raise AssertionError("loop must be refused")
except ValueError:
    pass
print("Static patch relocation tests passed")
