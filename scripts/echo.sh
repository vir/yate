#!/bin/sh

# Bourne shell test script for the Yate external module interface
# Loops back audio data
# To test add a route to: external/playrec/echo.sh

read
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

echo "=================== play ====================" >&2
(sleep 1; cat) <&3 >&4
echo "================= play done =================" >&2
