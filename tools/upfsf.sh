#!/bin/sh

oa='Foundation, Inc., .* USA.'
na='Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.'

grep -l -r "$oa\$" * | while read fn; do
echo -n "Processing: $fn ..."
sed "s/$oa/$na/" < "$fn" > "$fn.tmp"
if cmp -s "$fn" "$fn.tmp"; then
    echo " unchanged"
    rm "$fn.tmp"
else
    echo " changed"
    mv "$fn.tmp" "$fn"
fi
done
