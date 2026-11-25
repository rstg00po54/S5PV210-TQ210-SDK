#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p out
"${CC:-cc}" -O2 -Wall -Wextra -Werror -Iinclude \
    mm/pages.c tests/pages_test.c -o out/pages_test
./out/pages_test
