#!/bin/sh

# Run this to generate a new configure script

if [ -s tables/a2s.h ]; then
    echo "Good! Tables are generated so we don't need sox."
else
    if [ -z `which sox &>/dev/null` ]; then
	echo "Please install sox to be able to build from CVS version." >&2
	exit 1
    fi
fi

autoconf && echo "Finished! Now run configure. If in doubt run ./configure --help"
