#!/bin/sh

# Use: addcopy [pattern [year(s)]]
# Examples:
#  addcopy *.php
#  addcopy *.cpp 2009-2011

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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */
EOF
}

pat="$1"
test -n "$pat" || pat="*"
cpy="$2"
test -n "$cpy" || cpy="2011"

grep -L -r '^ \* Copyright (C) .* Null Team$' $pat | (while read fn; do

bn=`basename "$fn"`

notrigger="#$$#$$#"
trigger="$notrigger"
case "X$bn" in
	*.cpp)
		trigger=""
		;;
	*.php)
		trigger="<?"
		;;
esac

if [ "X$trigger" = "X$notrigger" ]; then
    echo "Not handling $bn" >&2
else
    echo "Processing: $fn ..."

    cp -p "$fn" "$fn.tmp"
    (while read; do
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
