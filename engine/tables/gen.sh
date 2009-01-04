#! /bin/sh

awk -f "$1/gen.awk"
sc="sox -r 8000 -c 1 -t raw"
b=-b
w=-w
if sox --help 2>&1 | grep -q /-4; then
    b=-1
    w=-2
fi

$sc $w -s 16b.raw $b -U -t raw s2u
$sc $w -s 16b.raw $b -A -t raw s2a

$sc $b -U 08b.raw $w -s -t raw u2s
$sc $b -A 08b.raw $w -s -t raw a2s

$sc $b -U 08b.raw $b -A -t raw u2a
$sc $b -A 08b.raw $b -U -t raw a2u

gcc -o gen "$1/gen.c"

for i in ?2?; do
    case "$i" in
	*2s)
	    ./gen w "$i" <"$i" >"$i.h"
	    ;;
	*)
	    ./gen b "$i" <"$i" >"$i.h"
	    ;;
    esac
    echo "#include \"$i.h\""
done >all.h

rm *.raw ?2? gen
