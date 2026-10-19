#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
name=BO3MacFix-mawlicious
mkdir -p staging release
stage=$(mktemp -d "staging/${name}.XXXXXX")
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name" "$stage/tools"
cp "$name.dylib" launcher/AppBundleExe "$stage/$name/"
cp tools/install_native.py "$stage/tools/"
cp LICENSE.txt README.md "$stage/"
cat > "$stage/install-mawlicious.command" <<'INSTALL'
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
exec python3 tools/install_native.py "$@"
INSTALL
chmod 0755 "$stage/install-mawlicious.command" "$stage/$name/AppBundleExe"
COPYFILE_DISABLE=1 tar --no-mac-metadata --disable-copyfile --exclude='.DS_Store' -czf "release/$name-macos-x86_64.tar.gz" -C "$stage" .
shasum -a 256 "release/$name-macos-x86_64.tar.gz" > release/SHA256SUMS
