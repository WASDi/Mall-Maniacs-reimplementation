#!/bin/bash
# Run maniac_rebuild.exe from the game-data directory with a 5s timeout
# as a crash smoke test (Rebuild.md). Logs go to rebuild.log in the game dir.
set -e
cd /home/wasd/MallManiacsUnmodified
rm -f rebuild.log
timeout 5 wine ./maniac_rebuild.exe
ec=$?
echo "exit code: $ec"
if [ -f rebuild.log ]; then
    echo "----- rebuild.log -----"
    cat rebuild.log
else
    echo "WARNING: no rebuild.log produced"
fi
exit 0