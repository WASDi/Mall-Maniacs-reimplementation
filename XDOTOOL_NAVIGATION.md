# Automated Input Navigation — xdotool Guide

How to drive `maniac_rebuild.exe` under Wine/Xorg without manual interaction.
Use this to verify menu flows and smoke-test new states after each rebuild.

## 1. How input works (why keys get lost)

* `WindowProc` (`src/maniac.c`) maps `WM_KEYDOWN/WM_KEYUP` into a 7-entry
  held-byte array `g_abInputKeyHeld`:
  `Right 0 / Left 1 / Up 2 / Down 3 / Space 4 / Enter 6 / Escape 7`
  (`0x80` on down, `0` on up).
* `pollKeyboard` (`src/input.c`) runs once per frame (~25 ms) *after* the
  `PeekMessage` batch and dispatches a key only if the held byte is `0x80`
  **at poll time**. Enter is edge-triggered ("once per press"); the others
  repeat every 200 ms.
* Consequence: `xdotool key` presses and releases within one frame, so the
  byte is set and cleared between two polls and the key is **silently lost**.

## 2. Rules for successful input

1. **Send every key as keydown → hold ~150 ms → keyup**, never as a plain
   `xdotool key`:
   ```bash
   send_key() { xdotool keydown --window $WIN "$1"; sleep 0.15; xdotool keyup --window $WIN "$1"; }
   ```
2. **Always target the window** (`--window $WIN`) and `windowactivate` it
   first, otherwise keys go to whatever has focus. Key names: `space`,
   `Return`, `Escape`, `Right`, `Left`, `Up`, `Down`, `alt+F4`.
3. **Wait for the game to confirm each step in `rebuild.log`** before
   sending the next key (poll with `grep`, 1 s apart). Never sleep more
   than 5 s in a wait loop.
4. **Skip the intro**: sleep 2 s after the window appears, then keep
   pressing SPACE (0.8 s apart) until the log contains
   `[menu] main menu active`. (Skipping via SPACE does *not* log
   `[intro] timeline complete` — that line only appears on natural expiry.)
   The original intro is nearly 20 seconds long.
5. **Check both logs** after a run: `rebuild.log` (game states) and
   `/tmp/wine_out.log` (`wine: Unhandled page fault` = crash).
6. **Stale state kills runs**: `killall -9 maniac_rebuild.exe` before
   launching (never `pkill -f` — it hangs the shell) and
   `rm -f rebuild.log` (the game appends).

## 3. Launch + full menu flow (copy-paste)

```bash
cd /home/wasd/MallManiacsUnmodified
killall -9 maniac_rebuild.exe 2>/dev/null; sleep 1
rm -f rebuild.log /tmp/wine_out.log
wine ./maniac_rebuild.exe > /tmp/wine_out.log 2>&1 &
for i in $(seq 1 20); do WIN=$(xdotool search --name "Mall Maniacs" 2>/dev/null | head -n1); [ -n "$WIN" ] && break; sleep 0.25; done
echo WIN=$WIN
xdotool windowactivate $WIN 2>/dev/null
sleep 2
send_key() { xdotool keydown --window $WIN "$1"; sleep 0.15; xdotool keyup --window $WIN "$1"; }
# --- intro skip ---
for i in $(seq 1 25); do
  grep -qa "main menu active" rebuild.log 2>/dev/null && break
  send_key space; sleep 0.8
done
# --- menu: Spela -> Varujakten -> first character -> first level ---
for k in 1 2 3 4; do
  send_key Return; sleep 1
  grep -qa "round 0 initialized" rebuild.log 2>/dev/null && break
done
grep -qa "round 0 initialized" rebuild.log || { send_key Return; sleep 3; }  # one retry
grep -a "round 0 initialized" rebuild.log
```

Log markers to expect along the way: `[menu] row 0 'Spela' selected`,
`[menu] game type 0 'Varujakten' selected`, `[charselect] Enter ->
stateCharSelectOk`, `[gameplay] round <level> initialized`.

Never sleep more than 5 seconds for anything, it's enough for actions to have an effect.
Please also close the window as soon as you don't need it anymore.

## 4. Other navigations

* **Character select cycling**: `Right`/`Left` (0.8 s apart) cycles the
  character (`[charselect] Right -> char N` + tpg load per character);
  `Return` confirms → level select.
* **Level select**: `Right`/`Left` pick a map, `Return` starts
  the round.
* **Back out**: `Escape` steps back one state (charselect → game type →
  main menu → quit confirm, where `J`/`Y` via `send_key j` quits). For a
  hard exit in automation use `send_key alt+F4` or `killall -9
  maniac_rebuild.exe`.
* **Screenshot** (verify what is actually on screen):
  ```bash
  import -window $WIN /tmp/opencode/FILENAME.png
  ```

## 5. Troubleshooting

* **No window found**: retry the 20×0.25 s search loop; if still missing
  check `/tmp/wine_out.log` (`gxLoadDriver failed`, missing data files) and
  that the exe was built (`make`).
* **Keys do nothing**: forgot `--window $WIN`, forgot `windowactivate`, or
  used `xdotool key` instead of the keydown/hold/keyup helper. A stale
  zombie game process can also hold the window id — kill it and relaunch.
* **Expected log line missing**: the game may be alive waiting for input —
  send the key once more, then check `ps aux | grep maniac_rebuild`.
* **`Unhandled page fault` in `/tmp/wine_out.log`**: real crash — take the
  last `rebuild.log` line as the crash site and instrument there.
