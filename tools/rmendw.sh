#!/bin/sh

grep -l -r '[ \t]\+$' * | grep -v '\.svn\|\.html\|Doxyfile\|Makefile' | while read fn; do
echo -n "Processing: $fn ..."
sed 's/[ \t]\+$//' < "$fn" > "$fn.tmp"
if cmp -s "$fn" "$fn.tmp"; then
    echo " unchanged"
    rm "$fn.tmp"
else
    echo " changed"
    mv "$fn.tmp" "$fn"
fi
done
