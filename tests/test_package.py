"""Verify the exact release archive installs on a clean game without Python."""
import pathlib
import subprocess
import tarfile
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
archive = root / 'release/BO3MacFix-mawlicious-macos-x86_64.tar.gz'
with tempfile.TemporaryDirectory(prefix='bo3-standalone-package-') as temporary:
    game = pathlib.Path(temporary) / 'Clean Steam game'
    game.mkdir()
    launcher = game / 'CoDBlkOps3.app/Contents/MacOS/AppBundleExe'
    launcher.parent.mkdir(parents=True)
    launcher.write_bytes(b'stock test launcher')
    with tarfile.open(archive) as bundle:
        names = bundle.getnames()
        assert all(name == 'BO3MacFix-mawlicious' or name.startswith('BO3MacFix-mawlicious/') for name in names)
        assert not any(name.endswith(('.py', '.exe', '.dll')) or '/cod2map/' in name or '/pfx/' in name for name in names)
        bundle.extractall(game, filter='data')
    folder = game / 'BO3MacFix-mawlicious'
    assert (folder / 'Physics/cp_mi_cairo_infection2_navvolume.hkt').is_file()
    # An empty PATH prevents accidental Python/developer-tool dependencies.
    subprocess.run(['/bin/bash', str(folder / 'Install.command')], cwd='/', env={'PATH': '/usr/bin:/bin'}, check=True)
    assert launcher.read_bytes() == (folder / 'AppBundleExe').read_bytes()
    subprocess.run(['/bin/bash', str(folder / 'Uninstall.command')], cwd='/', env={'PATH': '/usr/bin:/bin'}, check=True)
    assert launcher.read_bytes() == b'stock test launcher'
    print('Standalone release clean install/uninstall passed')
