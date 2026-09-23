#!/bin/sh
# **One box's leg of the three-box comparison.** Compiler++ built by the box's
# own compiler at O1 and O2 and by cxx1 at O0, O1 and O2; each build's four
# suites; every case's output and exit status kept, so the boxes can be laid
# side by side afterwards; and the bench, best of five.
#
# Usage: crossbox.sh BOX ROOT CPP CXX1 BENCH PHASE
#   BOX    windows, linux or mac - which native compiler and which tools
#   ROOT   a scratch directory for the builds and the outputs
#   CPP    a Compiler++ tree;  CXX1 the cxx1 binary;  BENCH bench.cpp
#   PHASE  build, test, all or bench - the Linux nano needs build and test apart
set -u
BOX=$1 ROOT=$2 CPP=$3 CXX1=$4 BENCH=$5 PHASE=${6:-all}
BUILDS="native-O1 native-O2 cx-O0 cx-O1 cx-O2"
R=$ROOT/summary-$BOX.txt
# link.exe ahead of Git bash's coreutils link.
[ $BOX = windows ] && export PATH="$(cygpath -u "$VCToolsInstallDir")bin/Hostx64/x64:$PATH"
mkdir -p $ROOT

say() { echo "$@" | tee -a $R; }
now() { perl -MTime::HiRes=time -e 'printf "%d\n", time * 1000'; }
# The Mac has no timeout(1); perl's alarm is the same thing.
limit() { perl -e 'alarm shift; exec @ARGV' "$@"; }
path() { if [ $BOX = windows ]; then cygpath -w "$1"; else echo "$1"; fi; }
units() { for u in $CPP/Compiler++/*.cpp; do printf '%s ' "$(path $u)"; done; }
exe() { if [ $BOX = windows ]; then echo $ROOT/$1/compilerpp.exe; else echo $ROOT/$1/compilerpp; fi; }

text() {
    case $BOX in
    windows) hx=$(dumpbin -headers "$(cygpath -w $1)" | tr -d '\r' | grep -A1 ' .text name' | awk '/virtual size/{print $1}')
             echo $((16#${hx:-0})) ;;
    linux)   size -A "$1" | awk '$1 == ".text" {print $2}' ;;
    mac)     size -m "$1" | awk -F': ' '/Section __text/ {print $2; exit}' ;;
    esac
}

build() {
    b=$1; L=${b#*-}; W=$ROOT/$b; rm -rf $W; mkdir -p $W; s=$(now)
    case $b in
    native-*) case $BOX in
              windows) (cd $W && cl -nologo -EHsc -W3 -$L -Fe:compilerpp.exe $(units)) ;;
              linux)   (cd $W && g++ -std=c++98 -$L $(units) -o compilerpp) ;;
              mac)     (cd $W && clang++ -std=c++98 -$L $(units) -o compilerpp) ;;
              esac ;;
    cx-*)     (cd $W && "$CXX1" -nologo -$L $(units) -o "$(path $(exe $b))") ;;
    esac > $W/build.log 2>&1 || { say "$b: build FAILED: $(tail -3 $W/build.log | tr -d '\r')"; return; }
    say "$b: built in $(( $(now) - s )) ms, .text $(text $(exe $b))"
}

test_build() {
    b=$1; e=$(exe $b); [ -x $e ] || return
    for t in run_tests run_exec run_roundtrip run_driver; do
        say "$b $t: $(cd $CPP && limit 900 sh tests/$t.sh "$e" 2>&1 | tr -d '\r' | tail -1)"
    done
    # Every case, compiled and run, kept whole: the cross-box comparison reads these.
    O=$ROOT/out/$b; rm -rf $O; mkdir -p $O
    for c in $CPP/tests/cases/*.cpp; do n=$(basename $c .cpp); in=/dev/null
        [ -f $CPP/tests/input/$n.txt ] && in=$CPP/tests/input/$n.txt
        for m in compile run; do flag=; [ $m = run ] && flag=-run
            (cd $CPP/tests && limit 60 $e $flag cases/$n.cpp < $in 2>&1; echo "exit=$?") | tr -d '\r' > $O/$n.$m
        done
    done
    bench $b
}

bench() {
    e=$(exe $1); best=999999
    for i in 1 2 3 4 5; do t0=$(now); limit 120 $e -run -q "$(path $BENCH)" > /dev/null 2>&1
        t=$(( $(now) - t0 )); [ $t -lt $best ] && best=$t; done
    say "$1 bench best of 5: $best ms"
}

case $PHASE in bench) for b in $BUILDS; do bench $b; done ;; esac
case $PHASE in build|all) : > $R; say "== $BOX, $(date)"; for b in $BUILDS; do build $b; done ;; esac
case $PHASE in test|all)
    for b in $BUILDS; do test_build $b; done
    for b in $BUILDS; do [ -d $ROOT/out/$b ] || continue
        same=$(cd $ROOT/out && for f in native-O2/*; do cmp -s $f $b/${f#native-O2/} && echo x; done | wc -l)
        say "$b against native-O2 on this box: $same of $(ls $ROOT/out/native-O2 | wc -l) outputs identical"
    done
    (cd $ROOT && tar czf out-$BOX.tgz out summary-$BOX.txt) ;;
esac
say "== $BOX $PHASE done"
