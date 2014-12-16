#!/bin/sh

# echo.sh
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
# Loops back audio data
# To test add a route to: external/playrec/echo.sh

prompt=""
# put here a proper wave/play file to play before starting echoing
# prompt="share/sounds/tone.wav"

read -r REPLY
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

if [ -n "$prompt" -a -f "$prompt" ]; then
    echo "================ play prompt ================" >&2
    echo "%%>message::"`date +%s`":chan.attach::single=true:override=wave/play/$prompt"
fi
echo "=================== play ====================" >&2
(sleep 1; cat) <&3 >&4
echo "================= play done =================" >&2
