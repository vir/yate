#! /bin/bash

# sctp_linux.sh
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


# This script configures Linux SCTP parameters for use with SIGTRAN

if [ ! -f /etc/sysctl.conf -o ! -d /proc/sys/net ]; then
    echo "Your system does not look like Linux!" >&2
    exit 1
fi

modfile="/etc/modprobe.preload"
if [ ! -f /etc/modprobe.conf ]; then
    modfile="/etc/modules"
    if [ ! -f "$modfile" ]; then
	echo "Cannot identify modules preload file!" >&2
	exit 1
    fi
fi

if [ x`id -u` != x0 ]; then
    echo "You must run this command as root!" >&2
    exit 1
fi

grep -q sctp "$modfile" || echo -e '\n# SCTP must be loaded early for sysctl\nsctp' >> "$modfile"
grep -q 'SCTP tweaking' /etc/sysctl.conf || echo -e '\n# SCTP tweaking for SIGTRAN\nnet.sctp.rto_min=200\nnet.sctp.rto_initial=400\nnet.sctp.rto_max=400\nnet.sctp.hb_interval=5000' >> /etc/sysctl.conf
