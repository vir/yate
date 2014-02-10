#!/bin/sh

# rmendw.sh
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


# This script removes whitespaces at end of lines of all files in all subdirectories

grep -l -r '[ \t]\+$' * | grep -v '\.svn\|\.html\|\.yhlp\|\.gz\|\.zip\|\.png\|\.gif\|\.jpg\|\.jpeg\|\.ico\|\.wav\|\.au\|\.mp3\|\.ogg\|\.gsm\|\.slin\|\.alaw\|\.mulaw\|Doxyfile\|Makefile\|README' | while read fn; do
echo -n "Processing: $fn ..."
sed 's/[ \t]\+$//' < "$fn" > "$fn.tmp"
if cmp -s "$fn" "$fn.tmp"; then
    echo " unchanged"
    rm "$fn.tmp"
else
    echo " changed"
    mv "$fn.tmp" "$fn"
fi
done
