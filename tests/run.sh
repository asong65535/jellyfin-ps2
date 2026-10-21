#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
    -I"$repo_dir/tests/stubs" -I"$repo_dir/src" \
    "$repo_dir/tests/test_hdd_settings.c" "$repo_dir/src/hdd.c" \
    "$repo_dir/src/settings.c" -o "$test_dir/test_hdd_settings"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
    -I"$repo_dir/tests/stubs" -I"$repo_dir/src" \
    "$repo_dir/tests/test_mods.c" "$repo_dir/src/mods.c" \
    -o "$test_dir/test_mods"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE \
    -I"$repo_dir/tests/stubs" "$repo_dir/tests/test_hdd_probe.c" \
    -o "$test_dir/test_hdd_probe"
cd "$test_dir"
./test_hdd_settings
./test_mods
mkdir probe
cd probe
../test_hdd_probe
