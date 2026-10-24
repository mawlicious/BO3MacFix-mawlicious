#!/bin/bash
set -euo pipefail
if (( $# > 1 )); then
    echo "Usage: $0 [private-script-fixture]" >&2
    exit 2
fi
if (( $# == 1 )); then
    fixture="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
fi
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource tests/test_havok_convert.c source/havok_convert.c -o "$work/test-convert"
"$work/test-convert"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource tests/test_stats_sync.c source/stats_sync.c source/stats_sync_runtime.c -o "$work/test-stats"
"$work/test-stats"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource tests/test_script_fixes.c source/script_fixes.c -o "$work/test-scripts"
if (( $# == 1 )); then
    "$work/test-scripts" "$fixture"
else
    "$work/test-scripts"
fi
python3 tests/test_havok_schema.py
python3 tests/test_launcher.py
python3 tests/test_install.py
python3 tests/test_nav_cache.py
