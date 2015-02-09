#!/bin/sh

# play.sh
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


# Bourne shell test script for the Yate external module interface
# Plays sound using the "play" program (from the sox package)
# To test add a route to: external/record/play.sh

read -r REPLY
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

echo "=================== play ====================" >&2
play -t raw -c 1 -f s -r 8000 -s w - <&3
echo "================= play done =================" >&2
