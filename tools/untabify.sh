#!/bin/sh

cmd=""
case "$1" in
	-h|--help)
		echo "usage: untabify [--size <number>] file [file...]"
		exit 0
		;;
	-s|--size)
		shift
		while [ ${#cmd} -lt "$1" ]; do cmd=" $cmd"; done
		shift
		;;
esac
test -z "$cmd" && cmd="        "

tmp=".$$.tmp"
cmd=": again; s/^\\(	*\\)	/\\1$cmd/; t again"
if [ "$#" = "0" ]; then
	sed "$cmd"
	exit 0
fi
while [ "$#" != "0" ]; do
	if [ -f "$1" ]; then
		sed "$cmd" <"$1" >"$1$tmp"
		mv "$1$tmp" "$1"
	else
		echo "Skipping missing file '$1'"
	fi
	shift
done
