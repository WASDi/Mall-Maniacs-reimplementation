# Automated Input Navigation — xdotool Guide for Future Agents

This document records the working recipe for driving `maniac_rebuild.exe` under Wine/Xorg without manual interaction. Use it to reproduce crashes, verify menu flows, and smoke-test new states. It complements `docs/12-input.md` (original DirectInput) and `src/input.c:35` / `src/maniac.c:32` (rebuild window-message path).

## 1. How rebuild input actually works

* `WindowProc @0x4161b0` in `src/maniac.c:32` maps `WM_KEYDOWN/WM_KEYUP` to a 7-entry byte array:
  `Right 0 (VK_RIGHT) / Left 1 (VK_LEFT) / Up 2 (VK_UP) / Down 3 (VK_DOWN) / Space 4 (VK_SPACE) / Enter 6 (VK_RETURN) / Escape 7 (VK_ESCAPE)` → `g_abInputKeyHeld[key] = 0x80` on down, `0` on up (`src/input.h:27`).
  `WM_CHAR @0x102` is forwarded immediately as `g_pStateFunc(1, wParam, 0)` — only used by `stateQuitConfirm @0x4200b0` for `J/Y`.

* `pollKeyboard @0x416a10` in `src/input.c:35` is called from `gameFrameUpdate @0x41a8c0` after the `PeekMessage` batch. It debounces `g_abInputKeyHeld` against `g_nBtnDebounceTick* @0x459d4c..0x459d64` (200 ms) and dispatches `dispatchKeyEvent @0x41ade0` → `g_pStateFunc @0x45a6f8 (1, nKey, 2)`.

* Active state `g_pStateFunc` has signature `int (*)(int nType,int nKey,int nKeyType)` (`src/menu.h:9`): `nType 0 = frame, nType 1 + nKeyType 2 = keydown`. Each state decides `g_pStateFunc = nextState;` and sets `g_nMenuFadeTarget @0x45a6f0`. Do **not** send `nKeyType 0` — it is ignored except for quit confirm.

* Logs: every state logs to `rebuild.log` in the game directory (`/home/wasd/MallManiacsUnmodified/rebuild.log`) via `appLog()` in `src/custom_helpers.c`. Always `cat rebuild.log` after a run.

## 2. Environment assumptions (this workspace)

* Xorg on `:0` (`ps aux | grep Xorg` → `Xorg :0`), `DISPLAY=:0` is valid. `xdotool` is installed (`/usr/bin/xdotool`).
* Wine 9.0, window class `"Mall Maniacs"` title `CreateWindowExA` `src/maniac.c:109` `0xcf0000 640x480`. Search with `xdotool search --name "Mall Maniacs"`.
* Working dir for the exe **must** be `/home/wasd/MallManiacsUnmodified` (data files, `DRIVERS\GXSOFT.DLL`, `menu\*.tpg`). `make` writes to `…/maniac_rebuild.exe`.
* The 25 ms frame gate in `gameFrameUpdate` means input is sampled once per frame; `pollKeyboard` needs wall time (`g_nLastFrameTime @0x45a65c`, `g_flFrameDelta @0x45a6cc`).

## 3. Minimal automation recipe

```bash
cd /home/wasd/MallManiacsUnmodified
rm -f rebuild.log /tmp/wine_out.log
timeout 8 wine ./maniac_rebuild.exe > /tmp/wine_out.log 2>&1 &
WINEPID=$!
# wait for window (DirectDraw palette needs a frame)
for i in $(seq 1 20); do
  WIN=$(DISPLAY=:0 xdotool search --name "Mall Maniacs" 2>/dev/null | head -n1)
  [ -n "$WIN" ] && break
  sleep 0.25
done
echo "WIN=$WIN PID=$WINEPID"
sleep 0.5
DISPLAY=:0 xdotool key --clearmodifiers --window $WIN space   # skip intro (introUpdate @0x41ae50, key 4)
sleep 0.8
DISPLAY=:0 xdotool key --clearmodifiers --window $WIN Return  # Spela (menuUpdate @0x41b0b0 row 0 → stateGameTypeSelect @0x41c010)
sleep 0.8
DISPLAY=:0 xdotool key --clearmodifiers --window $WIN Return  # Varujakten (modeInitVarujakten @0x41bf60 → stateCharacterSelect @0x41efa0)
sleep 1.0
# now in character select — check rebuild.log
cat rebuild.log
# safe exit — send Alt+F4 to close the window
DISPLAY=:0 xdotool key --clearmodifiers --window $WIN alt+F4
sleep 0.5; kill $WINEPID 2>/dev/null; wait $WINEPID; echo $?
```

Key names for `xdotool key`: `space`, `Return` (Enter), `Escape`, `Right`, `Left`, `Up`, `Down`, `alt+F4` (close window — preferred exit for automation). Always use `DISPLAY=:0`, `--window $WIN` **and** `--clearmodifiers`. Without `--window`/`--clearmodifiers` keys go to the focused window and modifiers can stick on the host.

## 4. Timing — the 200 ms debounce will bite you

`pollKeyboard` only emits once per press for Enter, and every 200 ms for Up/Down/Space/Escape. If you send keys too fast they are coalesced or dropped. Use `sleep 0.8` between distinct logical presses; `sleep 0.5` after window creation; `sleep 1.0` after a state transition that lazily loads textures (`charselect.c` gfx00 + char tpg). The game loop itself sleeps ~25 ms, so a single `xdotool key` press (key down + up) is well within one frame — no need for `--delay`.

If you need to hold a direction, loop with `sleep 0.8` per repeat. Do **not** use `xdotool keydown` without a matching `keyup` — `g_abInputKeyHeld` will stay `0x80` and `pollKeyboard` will repeat every 200 ms indefinitely.

## 5. Canonical flows (copy-paste)

**Intro → Game-type select → Character select (crash repro):**
```
space, Return, Return   # as above
# expect rebuild.log: "[charselect] gfx00.tpg loaded", "menu\ROLAND00.TPG loaded"
```

**Character-cycle stress:**
```
space, Return, Return
Right, Right, Right, Left, Return   # cycle 0→1→2→3→2 then Enter → stateCharSelectOk @0x41ef40 → stateLevelSelect @0x41b900
```
Check: `cat rebuild.log | grep charselect` should show `Right -> char 1/2/3`, `SUSANNE00/OKE00/AGATA00` loads, no `page fault`.

**Character select → level select → gameplay smoke test:**
```
space, Return, Return, Return, Return
```
The first two `Return` keys enter character select; the third enters level select and the fourth confirms its currently selected map. Wait `0.8` seconds between keys, then check `rebuild.log` for one `[gameplay] round <level> initialized` entry; the game must remain responsive with no Wine page fault.

**Back out (safe for automation — exit via Alt+F4):**
```
Escape  # charselect → stateGameTypeSelect
Escape  # game-type → menuUpdate
Escape  # menu → stateQuitConfirm @0x4200b0
# For automated runs, exit by sending Alt+F4:
DISPLAY=:0 xdotool key --clearmodifiers --window $WIN alt+F4
sleep 0.5; kill $WINEPID 2>/dev/null; wait $WINEPID
```

**Rekord table (no gfx fault):**
```
space
Down, Down, Down, Return  # Rekord row 3 → stateHighScoreTable @0x41dfd0
Left/Right  # cycle g_nResultsLevel, Up/Down toggle g_nRecordsRow
Escape      # back to menu
```

## 6. Troubleshooting

* **No window found:** `xdotool search` needs the exact title. Retry 20× with 0.25 s sleep; DirectDraw creation is ~400 ms. If still missing, `cat /tmp/wine_out.log` often shows `gxLoadDriver failed` or `MERGED00.TPG missing` — check `make` succeeded and `DRIVERS\GXSOFT.DLL` exists.
* **Keys go to Ghidra, not the game:** You forgot `--window $WIN` or `DISPLAY=:0`. The game window is not focused after `wine` start.
* **Page fault / `wine: Unhandled page fault`:** Usually `g_hMenuTexGfx @0x45a6bc == NULL` path in new states (see `charselect.c:252` lazy-load fix). Check `rebuild.log` last line before crash, add `appLog()` guards, rebuild, rerun with lazy-load for `menu\gfx00.tpg` and `menu\char*.tpg`. Under Wine `fopen("menu\\gfx00.tpg")` is translated by msvcrt, but case matters on Linux — try both `gfx00.tpg` and `GFX00.TPG`; Wine is case-insensitive but `fileExists @0x408f60` via `fopen` may not be, so the code now tries both.
* **Texture not found:** `find menu -iname "*.tpg" | sort` lists the real files (`ROLAND00.TPG`, `KAJSA00.TPG` …). The char-select mapping is at `charselect.c: kCharTpg[10]` indexed by `g_nCharSelIdx @0x45d480`. If you see `char tex ... missing, using TOM`, the mapping is wrong — fix the table, not the fallback.
* **Exit code 143 vs 0:** `timeout` kills with SIGTERM → 143, or `PostQuitMessage(0)` → 0. Both are clean. `124` is `timeout 5` expiring with no input.
* **Window will not close:** If `Alt+F4` does not terminate the game, the message pump may be blocked. Fall back to `kill $WINEPID` and confirm `ps aux | grep wine` shows no remaining `maniac_rebuild.exe`.
* **Stale log:** Always `rm -f rebuild.log` before launching; `gameFrameUpdate` appends, and multiple `wine` runs in the same dir share the log.
