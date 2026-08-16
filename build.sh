#!/bin/bash
# Compile maniac_rebuild.exe (Mall Maniacs replacement) per Rebuild.md.
# 32-bit Windows, original system libraries only: KERNEL32, USER32, GDI32, WINMM.
set -e
cd "$(dirname "$0")"

OUT=/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe

i686-w64-mingw32-gcc -m32 -O2 -g -Wall -Wextra \
    -mwindows \
    maniac.c gx.c util.c stubs.c \
    -lkernel32 -luser32 -lgdi32 -lwinmm \
    -o "$OUT"

echo "built: $OUT"
ls -la "$OUT"