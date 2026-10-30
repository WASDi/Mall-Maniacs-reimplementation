#!/bin/sh
# Build the 32-bit rebuild driver for the differential tests.
# Game-logic sources only (no SDL/GL backends); symbols those backends
# would provide stay unresolved, which is fine as long as a tested
# function never reaches them.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build-difftest"
mkdir -p "$OUT/obj0"

WRAP="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=rand,--wrap=srand"
CFLAGS="-m32 -malign-double -O0 -g -I$ROOT/src -DMANIAC_DATA_DIR=\"$ROOT\""
SRCS=$(sed -n '/MANIAC_LIB_SOURCES/,/^)/p' "$ROOT/CMakeLists.txt" \
       | grep -o 'src/[a-z0-9_]*\.c' | grep -v -e sdl2 -e 'src/maniac\.c' | sort -u)

OBJS=""
for f in $SRCS; do
    o="$OUT/obj0/$(basename "$f" .c).o"
    if [ ! -f "$o" ] || [ "$ROOT/$f" -nt "$o" ] || \
       [ -n "$(find "$ROOT/src" -name '*.h' -newer "$o" | head -1)" ]; then
        gcc $CFLAGS -c "$ROOT/$f" -o "$o"
    fi
    OBJS="$OBJS $o"
done

# Symbols the excluded backends would provide become trap stubs: a
# tested function that reaches one aborts with the symbol name.
UNRES=$(gcc $CFLAGS -no-pie -rdynamic "$ROOT/tests/difftest/rebuild_driver.c" $OBJS \
            $WRAP -lm -ldl -o /dev/null 2>&1 \
        | sed -n "s/.*undefined reference to \`\([A-Za-z0-9_]*\)'.*/\1/p" | sort -u)
{
    echo '#include <stdio.h>'
    echo '#include <stdlib.h>'
    for s in $UNRES; do
        echo "void $s(void) { fprintf(stderr, \"difftest trap: $s\\\\n\"); abort(); }"
    done
} > "$OUT/trap_stubs.c"

gcc $CFLAGS -no-pie -rdynamic "$ROOT/tests/difftest/rebuild_driver.c" $OBJS \
    "$OUT/trap_stubs.c" $WRAP -lm -ldl -o "$OUT/rebuild_driver"
echo "$OUT/rebuild_driver"

# Rebuild addresses change with every link: refresh the globals map.
"$ROOT/tests/difftest/.venv/bin/python" "$ROOT/tests/difftest/gen_globals.py" | head -1
