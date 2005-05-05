#!/bin/sh
#
# Use:
#    cd base-source-directory
#    mkpatch >file.patch
# Before editing files create copies with .orig extension
#    cp -p somefile.c somefile.c.orig

find . -name '*.orig' -exec echo diff -u {} {} \; | sed 's+\.orig$++' | /bin/sh
