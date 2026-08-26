#!/bin/sh
# Every case's linkage names, against clang's, for all three ABIs.
#
# This is the suite that says the platform ABI was conformed to rather than
# approximated. It needs clang - which can be asked for Microsoft names on any
# machine, with -target x86_64-pc-windows-msvc - and skips itself where there
# is none, saying so rather than passing quietly.
set -e
cd "$(dirname "$0")/.."

if ! command -v clang++ > /dev/null 2>&1; then
    echo "names.sh: skipped - no clang++ to ask"
    exit 0
fi

pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -f "tests/cases/$base.error" ] && continue

    if out=$(tools/mangled-names "$src" 2>&1); then
        pass=$((pass + 1))
    else
        echo "FAIL $base:"
        echo "$out" | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done

echo "names.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
