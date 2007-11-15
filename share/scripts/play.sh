#!/bin/sh

# Bourne shell test script for the Yate external module interface
# Plays sound using the "play" program (from the sox package)
# To test add a route to: external/record/play.sh

read
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

echo "=================== play ====================" >&2
play -t raw -c 1 -f s -r 8000 -s w - <&3
echo "================= play done =================" >&2
