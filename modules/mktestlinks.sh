#!/bin/sh

tests=""
if [ "$1" = "-d" ]; then
    for f in $tests; do rm $f.yate; done
else
    for f in $tests; do ln -s ../test/$f.yate $f.yate; done
fi
