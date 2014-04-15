#! /bin/sh

# yate-config.sh
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


if [ ! -f configure ]; then
    echo "Cannot find configure" >&2
    exit 1
fi

one_param()
{
    case "x$pos$1" in
	x__ac_subst_vars="'"*"'")
	    for p in $1; do
		one_param "$p"
	    done
	    ;;
	x__ac_subst_vars=*)
	    pos=""
	    ;;
	x__*)
	    ;;
	x*"'"*)
	    pos=__
	    ;;
	xPACKAGE_*|xECHO_*|xPATH_SEPARATOR|xCONFIGURE_FILES)
	    ;;
	xMUTEX_HACK|xTHREAD_KILL|xFDSIZE_HACK|xMODULE_*)
	    ;;
	x*_alias|x*_prefix|xprogram_*)
	    ;;
	x[A-Z]*_*)
	    echo "	--param=$1)"
	    echo "	    echo \"@$1@\""
	    echo "	    ;;"
    esac
}

exec > yate-config.in < configure

cat <<"EOF"
#! /bin/sh

# yate-config
# This file is part of the YATE Project http://YATE.null.ro
#
# This is a generated file. You should never need to modify it.
# Take a look at the source file yate-config.sh instead.
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

ustr='Usage: yate-config [--cflags] [--includes] [--c-all]
                   [--ldflags] [--libs] [--ld-all] [--ld-nostrip] [--ld-strip]
		   [--config] [--modules] [--share]
		   [--helpdir] [--scripts] [--skins]
		   [--version] [--release] [--archlib] [--param=...]'
if [ "$#" = 0 ]; then
    echo "$ustr"
    exit 0
fi
prefix="@prefix@"
exec_prefix="@exec_prefix@"
datarootdir="@datarootdir@"
shrdir="@datadir@/yate"
moddir="@libdir@/yate"
confdir="@sysconfdir@/yate"
s1="@MODULE_CPPFLAGS@"
s2="-I@includedir@/yate"
s3="@MODULE_LDFLAGS@"
s4="@MODULE_SYMBOLS@"
s5="-lyate"

while [ "$#" != 0 ]; do
    case "$1" in
	--version)
	    echo "@PACKAGE_VERSION@"
	    ;;
	--release)
	    echo "@PACKAGE_STATUS@@PACKAGE_RELEASE@"
	    ;;
	--revision)
	    echo "@PACKAGE_REVISION@"
	    ;;
	--cflags)
	    echo "$s1"
	    ;;
	--includes)
	    echo "$s2"
	    ;;
	--c-all)
	    echo "$s1 $s2"
	    ;;
	--ldflags)
	    echo "$s3 $s4"
	    ;;
	--libs)
	    echo "$s5"
	    ;;
	--ld-all)
	    echo "$s3 $s4 $s5"
	    ;;
	--ld-nostrip)
	    echo "$s3 $s5"
	    ;;
	--ld-strip)
	    echo "$s4"
	    ;;
	--config)
	    echo "$confdir"
	    ;;
	--modules)
	    echo "$moddir"
	    ;;
	--share)
	    echo "$shrdir"
	    ;;
	--helpdir)
	    echo "$shrdir/help"
	    ;;
	--skins)
	    echo "$shrdir/skins"
	    ;;
	--scripts)
	    echo "$shrdir/scripts"
	    ;;
	--archlib)
	    echo "@ARCHLIB@"
	    ;;
EOF

pos=__
while read REPLY; do
    one_param "$REPLY"
done

cat <<"EOF"
	*)
	    echo "I didn't understand: $1" >&2
	    echo "$ustr" >&2
	    exit 1
	    ;;
    esac
    shift
done
EOF
