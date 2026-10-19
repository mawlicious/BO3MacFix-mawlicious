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
