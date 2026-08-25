#!/bin/sh
# Every case compiled for all three targets, stopping at assembly.
#
# No assembler and no linker, so this runs on any machine cxx1 builds on -
# which is the point. A backend that cannot be run here can still be checked
# for having produced something rather than having fallen over.
set -e
cd "$(dirname "$0")/.."
CXX1=./cxx1.exe
OUT=tests/out-emit
rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -f "tests/cases/$base.error" ] && continue
    for target in x86_64-linux x86_64-windows arm64-darwin; do
        if $CXX1 -S -arch "$target" "$src" -o "$OUT/$base.$target.s" \
                 2>"$OUT/$base.$target.err"; then
            pass=$((pass + 1))
        else
            echo "FAIL $base for $target:"
            sed 's/^/      /' "$OUT/$base.$target.err"
            fail=$((fail + 1))
        fi
    done
done

echo "emit.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
