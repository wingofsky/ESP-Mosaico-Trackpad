#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
trackpad_test_dir=$(mktemp -d)
trap 'rm -rf "$trackpad_test_dir"' EXIT
for name in gesture command_line; do
    cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -g \
       -Imain "main/$name.c" "tests/${name}_test.c" -o "$trackpad_test_dir/$name"
    "$trackpad_test_dir/$name"
done
python3 -m unittest discover -s tests -p 'test_*.py'
python3 -O -m unittest discover -s tests -p 'test_*.py'
python3 tools/compare_gestures.py
