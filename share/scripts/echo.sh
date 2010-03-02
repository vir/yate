#!/bin/sh

# Bourne shell test script for the Yate external module interface
# Loops back audio data
# To test add a route to: external/playrec/echo.sh

prompt=""
# put here a proper wave/play file to play before starting echoing
# prompt="share/sounds/tone.wav"

read
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

if [ -n "$prompt" -a -f "$prompt" ]; then
    echo "================ play prompt ================" >&2
    echo "%%>message::"`date +%s`":chan.attach::single=true:override=wave/play/$prompt"
fi
echo "=================== play ====================" >&2
(sleep 1; cat) <&3 >&4
echo "================= play done =================" >&2
