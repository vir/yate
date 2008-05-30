#!/bin/sh

cpy="$1"
test -n "$cpy" || cpy="2004-2008"

grep -l -r '^ \* Copyright (C) .* Null Team$' * | while read fn; do
echo -n "Processing: $fn ..."
sed 's/^\( \* Copyright (C) \).*\( Null Team\)$/\1'"$cpy"'\2/' < "$fn" > "$fn.tmp"
if cmp -s "$fn" "$fn.tmp"; then
    echo " unchanged"
    rm "$fn.tmp"
else
    echo " changed"
    mv "$fn.tmp" "$fn"
fi
done

