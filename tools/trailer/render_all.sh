#!/bin/sh
# Render every clip tools/trailer/edit.txt uses (Trailer.md). Drafts by
# default; pass --final for the motion-blurred final render. Needs the
# cinematic build (cmake -S . -B build-cine -DMANIAC_CINEMATIC=ON) and a
# display (DISPLAY=:0). Run from the repository root.
set -e
: "${DISPLAY:=:0}"
export DISPLAY
mkdir -p build/trailer/clips build/trailer/clips_preview
for shot in $(awk '$1 == "video" && $3 != "black" { print $3 }' tools/trailer/edit.txt | sort -u); do
    ./build-cine/maniac_rebuild --cinematic "tools/trailer/shots/$shot.txt" "$@" 2>&1 | grep -v '^\[platform'
done
