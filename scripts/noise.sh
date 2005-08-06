#!/bin/sh

# Bourne shell test script for the Yate external module interface
# Generates 2 seconds of white noise
# To test add a route to: external/play/noise.sh

read
echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:true:/;'

echo "=================== noise ===================" >&2
dd if=/dev/urandom bs=320 count=100 >&4
echo "================= noise done ================" >&2
