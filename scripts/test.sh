#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Isource tests/test_havok_convert.c source/havok_convert.c -o "$work/test-convert"
"$work/test-convert"
python3 tests/test_havok_schema.py
python3 tests/test_launcher.py
python3 tests/test_install.py
