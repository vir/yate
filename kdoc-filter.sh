#!/bin/sh

# Filter the Yate header files so they can be parsed by kdoc

filter='s/FORMAT_CHECK(.*)//'

test -f "$2" && sed "$filter" "$2"
