"""Optional integration test: python3 tests/test_nav_cache.py input.clump.

Uses a local game capture; no proprietary data is checked in.
"""
import pathlib
import struct
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
raw = pathlib.Path(sys.argv[1]).read_bytes()
with tempfile.TemporaryDirectory(prefix="bo3macfix-nav-") as directory:
    work = pathlib.Path(directory)
    executable = work / "convert-clump"
    subprocess.run(["clang", "-std=c11", "-D_DARWIN_C_SOURCE", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", "-g", "-I" + str(root / "source"),
                    str(root / "tests/convert_clump.c"), str(root / "source/nav_convert.c"),
                    str(root / "source/havok_convert.c"), "-o", str(executable)], check=True)
    prefix = work / "cache"
    mesh = work / "cache_navmesh.hkt"
    volume = work / "cache_navvolume.hkt"
    mesh.write_bytes(b"existing mesh")
    volume.write_bytes(b"existing volume")
    sizes = struct.unpack_from("<12i", raw)
    assert sizes[0] == 1 and sizes[10] > 0, "Capture must include navvolume buffers"
    damaged = bytearray(raw)
    offset = 48 + sum(sizes[1:10])
    damaged[offset:offset + 4] = b"FAIL"
    cases = [bytes(48), struct.pack("<12i", 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0) + b"x", damaged]
    for index, data in enumerate(cases):
        capture = work / f"invalid-{index}.clump"
        capture.write_bytes(data)
        result = subprocess.run([str(executable), str(capture), str(prefix)], capture_output=True)
        assert result.returncode != 0, result.stderr
        assert mesh.read_bytes() == b"existing mesh"
        assert volume.read_bytes() == b"existing volume"
        assert not list(work.glob("*.??????")), "Temporary output leaked"
    subprocess.run([str(executable), sys.argv[1], str(prefix)], check=True)
    assert mesh.stat().st_size > 64 and volume.stat().st_size > 64
    assert not list(work.glob("*.??????"))
    print("Native conversion and cache failure cleanup passed")
