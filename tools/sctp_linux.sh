#! /bin/bash

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
grep -q 'SCTP tweaking' /etc/sysctl.conf || echo -e '\n# SCTP tweaking for SIGTRAN\nnet.sctp.rto_min=200\nnet.sctp.rto_initial=400\nnet.sctp.rto_max=800\nnet.sctp.hb_interval=5000' >> /etc/sysctl.conf
