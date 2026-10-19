import importlib.util
import pathlib
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', root / 'tools/install_native.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory() as directory:
    work = pathlib.Path(directory)
    game = work / 'Game with spaces'
    launcher = game / 'CoDBlkOps3.app/Contents/MacOS/AppBundleExe'
    launcher.parent.mkdir(parents=True)
    launcher.write_bytes(b'original launcher')
    assets = game / 'BO3MacFix'
    (assets / 'zone').mkdir(parents=True)
    (assets / 'zone/zm_patch.ff').write_bytes(b'asset fixture')
    (assets / 'pfx').mkdir()
    (assets / 'cod2map').mkdir()
    payload = work / 'payload'
    payload.mkdir()
    (payload / 'AppBundleExe').write_bytes(b'native launcher')
    (payload / (module.NAME + '.dylib')).write_bytes(b'native mod')
    module.install(game, payload)
    target = game / module.NAME
    assert launcher.read_bytes() == b'native launcher'
    assert (target / 'zone/zm_patch.ff').read_bytes() == b'asset fixture'
    assert not (target / 'pfx').exists() and not (target / 'cod2map').exists()
    (target / 'logs').mkdir()
    (target / 'logs/session-old.log').write_bytes(b'old log')
    module.install(game, payload)
    backups = list((game / (module.NAME + '-backups')).iterdir())
    assert len(backups) == 2
    assert any((p / module.NAME / 'logs/session-old.log').is_file() for p in backups)
    assert any((p / 'AppBundleExe').read_bytes() == b'original launcher' for p in backups)
    print('Native upgrade install and rollback backups passed')
