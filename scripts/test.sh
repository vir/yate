#!/bin/sh

# Bourne shell test script for the Yate external module
# To test add in extmodule.conf
#
# [scripts]
# test.sh=test param

gen_message()
{
    t=`date +%s`
    echo "%%>message:$$-$RANDOM:$t:$*"
}

if [ "$1" = "--generator" ]; then
    exec </dev/null
    echo "================ generator ==================" >&2
    echo "%%>install:10:engine.timer"
#    echo "%%>install:10:testmsg"
    for ((i=1; i<10; i++)); do
	sleep 1
	gen_message "testmsg:retval:val=$i:random=$RANDOM"
    done
    echo "%%>uninstall:engine.timer"
    echo "============== generator done ===============" >&2
    exit
fi

echo "=============================================" >&2
echo "\$0=$0 \$*=$*" >&2
echo "=============================================" >&2

$0 --generator &

echo "================= reporter ==================" >&2
while read; do
    echo "Reporter: $REPLY" >&2
    case "$REPLY" in
	%%\>message:*)
	    echo "Reporter: seen incoming message" >&2
	    echo "$REPLY" | sed 's/^[^:]*:\([^:]*\):.*$/%%<message:\1:false:/;'
	    ;;
	%%\<message:*)
	    echo "Reporter: seen message answer" >&2
	    ;;
	%%\<install:*)
	    echo "Reporter: seen install answer" >&2
	    ;;
	%%\<uninstall:*)
	    echo "Reporter: seen uninstall answer" >&2
	    ;;
    esac
done
echo "============== reporter done ================" >&2
