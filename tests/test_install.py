"""Exercise the real compiled installer against a clean stand-in game."""
import fcntl
import hashlib
import pathlib
import shutil
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
subprocess.run(['make', '-C', str(root / 'installer')], check=True)
with tempfile.TemporaryDirectory(prefix='bo3-native-install-') as directory:
    game = pathlib.Path(directory) / 'Clean game with spaces'
    launcher = game / 'CoDBlkOps3.app/Contents/MacOS/AppBundleExe'
    launcher.parent.mkdir(parents=True)
    launcher.write_bytes(b'original launcher')
    folder = game / 'BO3MacFix-mawlicious'
    (folder / 'zone').mkdir(parents=True)
    executable = folder / 'Install_BO3MacFix-mawlicious'
    shutil.copy2(root / 'installer/Install_BO3MacFix-mawlicious', executable)
    (folder / 'AppBundleExe').write_bytes(b'native launcher')
    (folder / 'BO3MacFix-mawlicious.dylib').write_bytes(b'native mod')
    lines = []
    for name in ['core', 'mp', 'zm']:
        path = folder / f'zone/{name}_patch.ff'
        path.write_bytes((name + ' fixture').encode())
        lines.append(hashlib.sha256(path.read_bytes()).hexdigest() + '  zone/' + path.name)
    manifest = folder / 'ASSET_SHA256SUMS'
    manifest.write_text('\n'.join(lines) + '\n')
    def run(action='--install', success=True):
        result = subprocess.run([str(executable), action], cwd='/', capture_output=True, text=True)
        assert (result.returncode == 0) == success, result.stdout + result.stderr
        return result
    # A simultaneous installer must leave the active launcher unchanged.
    lock = launcher.parent / '.mawlicious-install.lock'
    with lock.open('w') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        run(success=False)
        assert launcher.read_bytes() == b'original launcher'
    # No upstream mod folder exists, and installation must not look for one.
    for name in ['AppBundleExe', 'BO3MacFix-mawlicious.dylib']:
        subprocess.run(['/usr/bin/xattr', '-w', 'com.apple.quarantine', '0081;fixture;test;', str(folder / name)], check=True)
    run()
    backup = launcher.with_name('AppBundleExe_before_mawlicious')
    assert launcher.read_bytes() == b'native launcher'
    assert backup.read_bytes() == b'original launcher'
    for path in [launcher, folder / 'AppBundleExe', folder / 'BO3MacFix-mawlicious.dylib']:
        assert 'com.apple.quarantine' not in subprocess.check_output(['/usr/bin/xattr', str(path)], text=True).splitlines()
    (folder / 'logs').mkdir()
    (folder / 'logs/session.log').write_bytes(b'preserve')
    run()
    assert backup.read_bytes() == b'original launcher'
    assert (folder / 'logs/session.log').read_bytes() == b'preserve'
    run('--uninstall')
    assert launcher.read_bytes() == b'original launcher'
    # Recovery must work even if the active launcher was deleted. Installation
    # still requires an existing launcher so it can preserve a valid baseline.
    launcher.unlink()
    run(success=False)
    assert not launcher.exists()
    run('--uninstall')
    assert launcher.read_bytes() == b'original launcher'
    assert backup.read_bytes() == b'original launcher'
    # Missing/damaged assets must fail before replacing the active launcher.
    patch = folder / 'zone/zm_patch.ff'
    saved = patch.read_bytes()
    patch.unlink()
    run(success=False)
    assert launcher.read_bytes() == b'original launcher'
    patch.write_bytes(b'corrupt')
    run(success=False)
    assert launcher.read_bytes() == b'original launcher'
    patch.write_bytes(saved)
    # A pre-existing upstream backup is the stock baseline for uninstall.
    backup.unlink()
    launcher.write_bytes(b'upstream mod launcher')
    launcher.with_name('AppBundleExe_backup').write_bytes(b'stock launcher')
    run()
    assert backup.read_bytes() == b'stock launcher'
    run('--uninstall')
    assert launcher.read_bytes() == b'stock launcher'
    backup.unlink()
    run('--uninstall', success=False)
    assert launcher.read_bytes() == b'stock launcher'
    assert not list(launcher.parent.glob('.mawlicious-launcher-*'))
    assert not list(launcher.parent.glob('.mawlicious-backup-*'))
    print('Clean native install, reinstall, uninstall, quarantine and asset validation passed')
