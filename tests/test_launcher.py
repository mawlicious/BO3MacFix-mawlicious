"""Exercise the real launcher against a temporary stand-in game, not BO3."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="bo3macfix-launcher-") as directory:
    fixture = pathlib.Path(directory)
    binaries = fixture / "Game.app/Contents/MacOS"
    binaries.mkdir(parents=True)
    fix = fixture / "BO3MacFix-mawlicious"
    fix.mkdir()
    launcher = binaries / "AppBundleExe"
    subprocess.run(["clang", "-arch", "x86_64", "-framework", "Foundation",
                    str(root / "launcher/launcher.m"), "-o", str(launcher)], check=True)
    source = fixture / "game.c"
    source.write_text('''#include <stdio.h>
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) printf("ARG:%s\\n", argv[i]);
    fprintf(stderr, "game stderr\\n");
    return 17;
}
''')
    subprocess.run(["clang", "-arch", "x86_64", str(source), "-o",
                    str(binaries / "CoDBlkOps3_Exe")], check=True)
    # A real empty dylib lets dyld load cleanly without injecting the mod.
    source.write_text("int launcher_test_fixture;\n")
    subprocess.run(["clang", "-arch", "x86_64", "-dynamiclib", str(source),
                    "-o", str(fix / "BO3MacFix-mawlicious.dylib")], check=True)
    for _ in range(2):
        result = subprocess.run([str(launcher), "--password", "test-secret",
                                 "--friendsonly", "+set", "logfile", "1"],
                                capture_output=True, text=True)
        assert result.returncode == 17, result.stderr
    logs = list((fix / "logs").glob("session-*.log"))
    assert len(logs) == 2, logs
    for log in logs:
        content = log.read_text()
        assert "Network password: enabled; friends-only setting: true" in content
        assert "ARG:+set\nARG:logfile\nARG:1\n" in content
        assert "game stderr" in content
        assert "game exit status 17" in content
        assert "test-secret" not in content
        assert "ARG:--password" not in content
        assert "ARG:" + str(launcher) not in content
        assert log.stat().st_mode & 0o777 == 0o600
    print("Launcher session capture and argument forwarding tests passed")

# Static executable: the launcher builds a patched, ad hoc signed copy from the
# original + static-patch.txt, launches it, reuses it, and falls back safely.
import hashlib
with tempfile.TemporaryDirectory(prefix="bo3macfix-static-") as directory:
    fixture = pathlib.Path(directory)
    binaries = fixture / "Game.app/Contents/MacOS"
    binaries.mkdir(parents=True)
    fix = fixture / "BO3MacFix-mawlicious"
    fix.mkdir()
    launcher = binaries / "AppBundleExe"
    subprocess.run(["clang", "-arch", "x86_64", "-framework", "Foundation",
                    str(root / "launcher/launcher.m"), "-o", str(launcher)], check=True)
    source = fixture / "game.c"
    source.write_text('#include <stdio.h>\nconst char marker[] = "ORIGINAL";\n'
                      'int main(void) { puts(marker); return 3; }\n')
    original = binaries / "CoDBlkOps3_Exe"
    subprocess.run(["clang", "-arch", "x86_64", str(source), "-o", str(original)], check=True)
    data = original.read_bytes()
    at = data.index(b"ORIGINAL")
    patched = bytearray(data)
    patched[at:at + 4] = b"PATC"           # written bytes
    patched[at + 4:at + 8] = data[at:at + 4]  # copied from the original ("ORIG")
    (fix / "static-patch.txt").write_text(
        "# test\n"
        f"original-sha256 {hashlib.sha256(data).hexdigest()}\n"
        f"patched-sha256 {hashlib.sha256(bytes(patched)).hexdigest()}\n"
        f"w {at:x} {b'PATC'.hex()}\nc {at + 4:x} {at:x} 4\n")
    source.write_text("int launcher_test_fixture;\n")
    subprocess.run(["clang", "-arch", "x86_64", "-dynamiclib", str(source),
                    "-o", str(fix / "BO3MacFix-mawlicious.dylib")], check=True)
    def run(env=None):
        import os
        e = dict(os.environ); e.update(env or {})
        r = subprocess.run([str(launcher)], capture_output=True, text=True, env=e)
        log = sorted((fix / "logs").glob("session-*.log"), key=lambda p: p.stat().st_mtime)[-1].read_text()
        return r.returncode, log
    code, log = run()
    assert code == 3, log
    assert "PATCORIG" in log and "built CoDBlkOps3_Exe_mawlicious" in log, log
    assert (binaries / "CoDBlkOps3_Exe_mawlicious").exists()
    code, log = run()
    assert "PATCORIG" in log and "verified CoDBlkOps3_Exe_mawlicious" in log, log
    code, log = run({"BO3MACFIX_STATIC": "0"})
    assert "ORIGINAL" in log and "disabled by BO3MACFIX_STATIC=0" in log, log
    # tampered static copy is rebuilt
    (binaries / "CoDBlkOps3_Exe_mawlicious").write_bytes(b"junk")
    code, log = run()
    assert code == 3 and "PATCORIG" in log and "built" in log, log
    # a changed original (e.g. Steam update) falls back to runtime patching
    original.write_bytes(data + b"\0")
    code, log = run()
    assert "ORIGINAL" in log and "changed" in log, log
    print("Launcher static executable build, reuse, tamper and fallback tests passed")
