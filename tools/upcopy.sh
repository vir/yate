#! /bin/sh

# upcopy.sh
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


editone()
{
    fn="$1"
    tf="$fn.tmp"
    echo -n "Processing: $fn ..."
    (
    inside=n
    while read -r; do
	case "$REPLY" in
	    *Copyright*Null\ Team*)
		cpy=`echo "$REPLY" | sed -n 's/^.* \([0-9]\{4\}\(-[0-9]\{4\}\)\?\) .*$/\1/p'`
		rep=""
		case "X$cpy" in
		    X)
			;;
		    X????-????)
			if [ "X${cpy:5}" != "X$year" ]; then
			    rep="${cpy:0:4}-$year"
			fi
			;;
		    X????)
			if [ "X$cpy" != "X$year" ]; then
			    rep="$cpy-$year"
			fi
			;;
		esac
		if [ -n "$rep" ]; then
		    echo "$REPLY" | sed "s/$cpy/$rep/"
		else
		    echo "$REPLY"
		fi
		;;
	    *This\ program\ is\ free\ software\;\ you\ can\ redistribute*)
		inside=y
		cat <<EOF
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
EOF
		;;
	    *Foundation,\ Inc*)
		inside=n
		;;
	    *)
		if [ "X$REPLY" = "X */" ]; then
		    inside=n
		fi
		if [ "X$inside" != "Xy" ]; then
		    echo "$REPLY"
		fi
		;;
	esac
    done
    ) < "$fn" > "$tf"
    if cmp -s "$fn" "$tf"; then
	echo " unchanged"
	rm "$tf"
    else
	echo " changed"
	mv "$tf" "$fn"
    fi
}

year=`date +%Y`
if [ "X$*" = "X-r" ]; then
    grep -l -r '^ \* Copyright (C) .* Null Team$' * | grep -v '\.svn\|\.html' | while read fn; do
	editone "$fn"
    done
    exit
fi

while [ -n "$1" ]; do
    editone "$1"
    shift
done
