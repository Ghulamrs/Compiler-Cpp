#!/bin/sh
# Every case compiled for all three targets, stopping at assembly.
#
#   tests/emit.sh            compile, and diff against the golden if there is one
#   tests/emit.sh --record   compile, and keep this output as the golden
#
# No assembler and no linker, so this runs on any machine cxx1 builds on -
# which is the point. A backend that cannot be run here can still be checked
# for having produced something rather than having fallen over.
#
# **The golden is what makes "this change emits the same code" a measurement.**
# Counting what compiled says nothing about what came out, so a refactor meant
# to change no behaviour had nothing here to prove it with. Record before it,
# run after it, and the answer is a number of files rather than a claim.
#
# It never fails the run. A changed file may be the whole point of the change,
# and a suite that refused one would be a suite people stopped recording.
set -e
cd "$(dirname "$0")/.."
CXX1=./cxx1.exe
cxx1() { ( ulimit -t 10; $CXX1 "$@" < /dev/null ); }
OUT=tests/out-emit
GOLD=tests/out-emit.golden

# Refused by name, like everything else here: a mistyped flag that ran the
# ordinary suite would look like a recording that quietly did not happen.
record=0
case ${1:-} in
    "")        ;;
    --record)  record=1 ;;
    *)         echo "emit.sh: '$1' is not an option - only --record is" >&2; exit 2 ;;
esac

rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -f "tests/cases/$base.error" ] && continue
    for target in x86_64-linux x86_64-windows arm64-darwin; do
        # A case may name a target it does not compile for yet, one per line in
        # <case>.notarget. **It has to say why in the file**, because a silent
        # exclusion is how a suite stops testing something without anybody
        # noticing. The line is printed on every run for the same reason.
        if [ -f "tests/cases/$base.notarget" ] &&
           grep -q "^$target\b" "tests/cases/$base.notarget"; then
            echo "  skip $base for $target: $(grep "^$target\b" "tests/cases/$base.notarget" | sed "s/^$target[[:space:]]*//")"
            continue
        fi
        if cxx1 -S -arch "$target" "$src" -o "$OUT/$base.$target.s" \
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

# **What was recorded, and when.** A golden with no provenance is a number
# nobody can place, so the commit and the date go in beside it and are printed
# on every run that reads it - a stale golden then says so itself.
if [ "$record" = 1 ]; then
    rm -rf "$GOLD"
    cp -R "$OUT" "$GOLD"
    { echo "recorded $(date -u '+%Y-%m-%d %H:%M UTC')"
      echo "commit   $(git rev-parse --short HEAD 2>/dev/null || echo 'not a git checkout')"
      echo "tree     $(git status --porcelain 2>/dev/null | wc -l | tr -d ' ') file(s) modified when recorded"
    } > "$GOLD/RECORDED"
    echo "emit.sh: golden recorded, $(find "$GOLD" -name '*.s' | wc -l | tr -d ' ') files"
    exit 0
fi

if [ ! -d "$GOLD" ]; then
    # Said out loud rather than skipped, for the reason the .notarget lines are.
    echo "emit.sh: no golden to compare against - tests/emit.sh --record makes one"
    [ "$fail" -eq 0 ]
    exit
fi

changed=0; added=0; removed=0; shown=0; golden=0
for now in "$OUT"/*.s; do
    was="$GOLD/$(basename "$now")"
    if [ ! -f "$was" ]; then
        added=$((added + 1))
        continue
    fi
    if cmp -s "$now" "$was"; then continue; fi
    changed=$((changed + 1))
    if [ "$shown" -lt 12 ]; then
        echo "  changed $(basename "$now" .s)"
        shown=$((shown + 1))
    fi
done
# `[ x ] && echo` would end the run under set -e the moment the test is
# false, which is the ordinary case. An if is not a style choice here.
if [ "$changed" -gt "$shown" ]; then echo "  ... and $((changed - shown)) more"; fi
for was in "$GOLD"/*.s; do
    [ -f "$was" ] || continue     # an unmatched glob is the name of the pattern
    golden=$((golden + 1))
    [ -f "$OUT/$(basename "$was")" ] || removed=$((removed + 1))
done

sed 's/^/emit.sh: golden /' "$GOLD/RECORDED" 2>/dev/null || true
echo "emit.sh: golden - $changed of $golden files changed, $added added, $removed removed"

[ "$fail" -eq 0 ]
