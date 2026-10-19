#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
name=BO3MacFix-mawlicious
# Required upstream data is versioned here, not downloaded during installation.
(cd assets && shasum -a 256 -c UPSTREAM_SHA256SUMS)
(cd assets && shasum -a 256 -c ../ASSET_SHA256SUMS)
mkdir -p staging release
stage=$(mktemp -d "staging/${name}.XXXXXX")
trap 'rm -rf "$stage"' EXIT
folder="$stage/$name"
mkdir -p "$folder/natives"
cp "$name.dylib" launcher/AppBundleExe "installer/Install_$name" "$folder/"
cp -R assets/zone assets/Physics "$folder/"
cp ASSET_SHA256SUMS ASSETS.md LICENSE.txt README.md "$folder/"
cp -R docs "$folder/"
for action in Install Uninstall; do
    mode=$(echo "$action" | tr '[:upper:]' '[:lower:]')
    cat > "$folder/$action.command" <<INSTALL
#!/bin/bash
set -euo pipefail
cd "\$(dirname "\$0")"
/usr/bin/xattr -d com.apple.quarantine "./Install_$name" 2>/dev/null || true
chmod u+x "./Install_$name"
"./Install_$name" --$mode
INSTALL
    chmod 0755 "$folder/$action.command"
done
chmod 0755 "$folder/AppBundleExe" "$folder/Install_$name"
chmod 0644 "$folder/$name.dylib"
COPYFILE_DISABLE=1 tar --no-mac-metadata --disable-copyfile --exclude='.DS_Store' -czf "release/$name-macos-x86_64.tar.gz" -C "$stage" "$name"
shasum -a 256 "release/$name-macos-x86_64.tar.gz" > release/SHA256SUMS
