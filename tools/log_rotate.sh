#! /bin/sh

# log_rotate.sh
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


# Sample log and CDR rotator that creates files based on date and hour
# Assumes Yate writes to /var/log/yate and /var/log/yate-cdr.tsv

# You can place this script in /etc/cron.hourly and not use the system logrotate
# NOTE: Files are never deleted! Periodic cleanup is required

base="/var/log"
old="$base/yate-old"
ym=`date '+%Y-%m'`
d=`date '+%d'`
h=`date '+%H'`

mkdir -p "$old/$ym/$d"
mv "$base/yate" "$old/$ym/yate-$d-$h.log"
mv "$base/yate-cdr.tsv" "$old/$ym/$d/yate-cdr-$h.tsv"

/bin/kill -HUP `/bin/cat /var/run/yate.pid`
