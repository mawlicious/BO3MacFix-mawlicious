#!/usr/bin/env python3
"""Install the native upgrade over existing BO3MacFix assets, preserving rollback."""
import datetime
import subprocess
import pathlib
import shutil
import sys
import tempfile

NAME = 'BO3MacFix-mawlicious'

def clear_quarantine(path):
    # macOS Python builds do not consistently expose os.removexattr.
    attributes = subprocess.check_output(['/usr/bin/xattr', str(path)], text=True).splitlines()
    if 'com.apple.quarantine' in attributes:
        subprocess.run(['/usr/bin/xattr', '-d', 'com.apple.quarantine', str(path)], check=True)


def install(game, payload):
    game = game.resolve()
    launcher = game / 'CoDBlkOps3.app/Contents/MacOS/AppBundleExe'
    target = game / NAME
    original = game / 'BO3MacFix'
    assets = target if (target / 'zone/zm_patch.ff').is_file() else original
    if not launcher.is_file() or not (assets / 'zone/zm_patch.ff').is_file():
        raise RuntimeError('A working BO3MacFix installation with zone assets is required')
    for filename in ['AppBundleExe', NAME + '.dylib']:
        if not (payload / filename).is_file():
            raise RuntimeError('Missing release file: ' + filename)
    # Complete staging before changing the installed mod or launcher.
    with tempfile.TemporaryDirectory(prefix=NAME + '-install-', dir=game) as directory:
        staged = pathlib.Path(directory) / NAME
        staged.mkdir()
        for folder in ['zone', 'natives', 'Physics', 'logs']:
            if (assets / folder).is_dir():
                shutil.copytree(assets / folder, staged / folder)
        for filename in ['AppBundleExe', NAME + '.dylib']:
            shutil.copy2(payload / filename, staged / filename)
            clear_quarantine(staged / filename)
        (staged / 'AppBundleExe').chmod(0o755)
        (staged / (NAME + '.dylib')).chmod(0o644)
        backups = game / (NAME + '-backups')
        backups.mkdir(exist_ok=True)
        backup = pathlib.Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-'), dir=backups))
        shutil.copy2(launcher, backup / 'AppBundleExe')
        if target.exists():
            target.rename(backup / NAME)
        try:
            staged.rename(target)
            replacement = pathlib.Path(directory) / 'AppBundleExe'
            shutil.copy2(target / 'AppBundleExe', replacement)
            clear_quarantine(replacement)
            replacement.replace(launcher)
        except BaseException:
            if target.exists():
                shutil.rmtree(target)
            if (backup / NAME).exists():
                (backup / NAME).rename(target)
            shutil.copy2(backup / 'AppBundleExe', launcher)
            raise
    print('Installed ' + NAME)
    print('Backup: ' + str(backup))
    print('Logs: ' + str(target / 'logs'))

if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: install-mawlicious.command "/path/to/Call of Duty Black Ops III"')
    payload = pathlib.Path(__file__).resolve().parent.parent / NAME
    try:
        install(pathlib.Path(sys.argv[1]), payload)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
