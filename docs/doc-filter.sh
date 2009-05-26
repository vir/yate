#!/bin/sh

# Filter the Yate header files so they can be parsed by kdoc or doxygen

if [ -n "$2" -a -f "$2" ]; then
    f="$2"
else
    if [ -n "$1" -a -f "$1" ]; then
	f="$1"
    else
	echo "Could not find file to process" >&2
	exit 1
    fi
fi

filter='s/FORMAT_CHECK(.*)//; s/[A-Z]*_API//; s/^.*YCLASS.*)$//'

sed "$filter" "$f"
