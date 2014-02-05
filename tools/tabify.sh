#!/bin/sh

# tabify.sh
# This file is part of the YATE Project http://YATE.null.ro
#
# Yet Another Telephony Engine - a fully featured software PBX and IVR
# Copyright (C) 2005-2014 Null Team
#
# This software is distributed under multiple licenses;
# see the COPYING file in the main directory for licensing
# information for this specific distribution.
#
# This use of this software may be subject to additional restrictions.
# See the LEGAL file in the main directory for details.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.


# Convert spaces at line start to tabs

cmd=""
case "$1" in
	-h|--help)
		echo "usage: tabify [--size <number>] file [file...]"
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
cmd=": again; s/^\\(	*\\)$cmd/\\1	/; t again"
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
