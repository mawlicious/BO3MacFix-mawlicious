# Bundled upstream assets

This distribution includes the required non-Wine runtime data from
[BO3MacFix 1.2-beta3](https://github.com/InvoxiPlayGames/BO3MacFix/releases/tag/1.2-beta3),
released by InvoxiPlayGames on 29 March 2025. Original project authors and the
project license are retained in README.md and LICENSE.txt.

Source archive: `BO3MacFix-1.2-beta3.tar.gz`

Source URL: https://github.com/InvoxiPlayGames/BO3MacFix/releases/download/1.2-beta3/BO3MacFix-1.2-beta3.tar.gz

Source archive SHA-256: `aa954df5a912e8c0131a4178c0983e9d65313487fcc9092ea49a78bf6aa2b14c`

- `assets/zone/{core,mp,zm}_patch.ff`: required updated game scripts/assets.
- `assets/Physics/cp_mi_cairo_infection2_{navmesh,navvolume}.hkt`: upstream's
  preconverted campaign navigation data. Native cache validation may regenerate
  these from current source assets; the Physics folder is a mutable cache.

`assets/UPSTREAM_SHA256SUMS` records every bundled upstream file's original hash.
Packaging verifies all five files. `ASSET_SHA256SUMS` covers the three immutable
zone files and is checked by the installer before it changes the game launcher.

The upstream Windows SDK, EXE, DLLs, Wine prefix and compiled upstream binaries
are excluded. The release builds its own native mod, launcher and installer.
No files from a user's game installation or workshop maps are included.
