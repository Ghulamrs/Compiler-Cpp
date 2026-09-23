#!/bin/sh
# **Every build on every box, against one reference.** Reads the out-<box>.tgz
# that crossbox.sh leaves on each box and says, per build, how many of the
# case outputs match Windows' cl /O2 build, listing any that do not.
#
# Usage: compare-boxes.sh DIR   (DIR holding out-windows.tgz, out-linux.tgz, out-mac.tgz)
set -u
D=$1; cd $D
for b in windows linux mac; do
    [ -f out-$b.tgz ] || { echo "no out-$b.tgz"; continue; }
    rm -rf $b && mkdir $b && tar xzf out-$b.tgz -C $b
done
REF=windows/out/native-O2
total=$(ls $REF | wc -l)
echo "reference: windows native-O2 (cl /O2), $total outputs"
for box in windows linux mac; do
    for b in native-O1 native-O2 cx-O0 cx-O1 cx-O2; do
        O=$box/out/$b; [ -d $O ] || continue
        same=0; differ=""
        for f in $REF/*; do n=${f##*/}
            if cmp -s $f $O/$n; then same=$((same + 1)); else differ="$differ $n"; fi
        done
        printf '%-8s %-10s %4d / %d identical' $box $b $same $total
        [ -n "$differ" ] && printf '   differ:%s' "$(echo $differ | cut -c1-200)"
        echo
    done
done
