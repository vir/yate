#! /bin/bash

# addcopy.sh
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


# Use: addcopy [pattern [year(s)]]
# Examples:
#  addcopy '*.php'
#  addcopy '*.cpp' 2009-2012

function copyright()
{
cat <<EOF
/**
 * $bn
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * ENTER DESCRIPTION OF $bn HERE
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) $cpy Null Team
 *
 * This software is distributed under multiple licenses;
 * see the COPYING file in the main directory for licensing
 * information for this specific distribution.
 *
 * This use of this software may be subject to additional restrictions.
 * See the LEGAL file in the main directory for details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */
EOF
}

pat="$1"
test -n "$pat" || pat="*"
cpy="$2"
test -n "$cpy" || cpy=`date +%Y`

grep -L -r '^ \* Copyright (C) .* Null Team$' $pat | (while read fn; do

bn=`basename "$fn"`

notrigger="#$$#$$#"
trigger="$notrigger"
case "X$bn" in
	*.cpp)
		trigger=""
		;;
	*.php)
		trigger="<?php"
		;;
esac

if [ "X$trigger" = "X$notrigger" ]; then
    echo "Not handling $bn" >&2
else
    echo "Processing: $fn ..."

    cp -p "$fn" "$fn.tmp"
    (while read -r; do
    if [ "X$trigger" = "X" ]; then
	trigger="$notrigger"
	copyright
	echo ""
    fi
    printf '%s\n' "$REPLY"
    if [ "X$REPLY" = "X$trigger" ]; then
	trigger="$notrigger"
	echo ""
	copyright
    fi
    done) < "$fn" > "$fn.tmp"
    mv "$fn.tmp" "$fn"
fi

done)
