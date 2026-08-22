# Automated Input Navigation — xdotool Guide for Future Agents

This document records the working recipe for driving `maniac_rebuild.exe` under Wine/Xorg without manual interaction. Use it to reproduce crashes, verify menu flows, and smoke-test new states. It complements `docs/12-input.md` (original DirectInput) and `src/input.c:35` / `src/maniac.c:32` (rebuild window-message path).

## 1. How rebuild input actually works

* `WindowProc @0x4161b0` in `src/maniac.c:32` maps `WM_KEYDOWN/WM_KEYUP` to a 7-entry byte array:
  `Right 0 (VK_RIGHT) / Left 1 (VK_LEFT) / Up 2 (VK_UP) / Down 3 (VK_DOWN) / Space 4 (VK_SPACE) / Enter 6 (VK_RETURN) / Escape 7 (VK_ESCAPE)` → `g_abInputKeyHeld[key] = 0x80` on down, `0` on up (`src/input.h:27`).
  `WM_CHAR @0x102` is forwarded immediately as `g_pStateFunc(1, wParam, 0)` — only used by `stateQuitConfirm @0x4200b0` for `J/Y`.

* `pollKeyboard @0x416a10` in `src/input.c:35` is called from `gameFrameUpdate @0x41a8c0` after the `PeekMessage` batch. It debounces `g_abInputKeyHeld` against `g_nBtnDebounceTick* @0x459d4c..0x459d64` (200 ms) and dispatches `dispatchKeyEvent @0x41ade0` → `g_pStateFunc @0x45a6f8 (1, nKey, 2)`.

* Active state `g_pStateFunc` has signature `int (*)(int nType,int nKey,int nKeyType)` (`src/menu.h:9`): `nType 0 = frame, nType 1 + nKeyType 2 = keydown`. Each state decides `g_pStateFunc = nextState;` and sets `g_nMenuFadeTarget @0x45a6f0`. Do **not** send `nKeyType 0` — it is ignored except for quit confirm.

* Logs: every state logs to `rebuild.log` in the game directory (`/home/wasd/MallManiacsUnmodified/rebuild.log`) via `appLog()` in `src/custom_helpers.c`. Always `cat rebuild.log` after a run. `run.sh` does `rm -f rebuild.log; timeout 5 wine ./maniac_rebuild.exe`.

## 2. Environment assumptions (this workspace)

* Xorg on `:0` (`ps aux | grep Xorg` → `Xorg :0`), `DISPLAY=:0` is valid. `xdotool` is installed (`/usr/bin/xdotool`).
* Wine 9.0, window class `"Mall Maniacs"` title `CreateWindowExA` `src/maniac.c:109` `0xcf0000 640x480`. Search with `xdotool search --name "Mall Maniacs"`.
* Working dir for the exe **must** be `/home/wasd/MallManiacsUnmodified` (data files, `DRIVERS\GXSOFT.DLL`, `menu\*.tpg`). `build.sh` writes to `…/maniac_rebuild.exe`, `run.sh` `cd`s there before `wine`.
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
# safe exit — do not send 'j' in automation (see §6)
kill $WINEPID; wait $WINEPID; echo $?
# if you see stuck 'j' on host after a run, clear it:
# DISPLAY=:0 xdotool keyup j; DISPLAY=:0 xdotool keyup J; DISPLAY=:0 xdotool keyup Escape
```

Key names for `xdotool key`: `space`, `Return` (Enter), `Escape`, `Right`, `Left`, `Up`, `Down`, `j`/`y` (quit confirm — see §6 warning). Always use `DISPLAY=:0`, `--window $WIN` **and** `--clearmodifiers`. Without `--window`/`--clearmodifiers` keys go to the focused window and modifiers can stick on the host, spamming `jjjj...` in your editor.

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
Right, Right, Right, Left, Return   # cycle 0→1→2→3→2 then Enter → stateCharSelectOk @0x41ef40 → stateLevelSelect @0x41b900 (weak stub)
```
Check: `cat rebuild.log | grep charselect` should show `Right -> char 1/2/3`, `SUSANNE00/OKE00/AGATA00` loads, no `page fault`.

**Back out (safe for automation — avoids host key-stuck):**
```
Escape  # charselect → stateGameTypeSelect
Escape  # game-type → menuUpdate
Escape  # menu → stateQuitConfirm @0x4200b0
# For automated runs DO NOT send 'j' — just kill the process:
kill $WINEPID; wait $WINEPID
# If you must test the WM_CHAR quit path manually, use:
# DISPLAY=:0 xdotool key --clearmodifiers --window $WIN j; sleep 0.2; DISPLAY=:0 xdotool keyup j
# and never send 'j' after the Wine window is already gone.
```

**Rekord table (no gfx fault):**
```
space
Down, Down, Down, Return  # Rekord row 3 → stateHighScoreTable @0x41dfd0
Left/Right  # cycle g_nResultsLevel, Up/Down toggle g_nRecordsRow
Escape      # back to menu
```

## 6. Troubleshooting

* **No window found:** `xdotool search` needs the exact title. Retry 20× with 0.25 s sleep; DirectDraw creation is ~400 ms. If still missing, `cat /tmp/wine_out.log` often shows `gxLoadDriver failed` or `MERGED00.TPG missing` — check `build.sh` succeeded and `DRIVERS\GXSOFT.DLL` exists.
* **Keys go to Ghidra, not the game:** You forgot `--window $WIN` or `DISPLAY=:0`. The game window is not focused after `wine` start.
* **Log shows no state change:** You sent `Escape` as `nKeyType 2` but `stateQuitConfirm` expects `WM_CHAR` with `nKeyType 0`. Use `xdotool key --clearmodifiers --window $WIN j` (and then `xdotool keyup j` on the host) — but prefer `kill $WINEPID` in automation (see stuck-key warning below).
* **Page fault / `wine: Unhandled page fault`:** Usually `g_hMenuTexGfx @0x45a6bc == NULL` path in new states (see `charselect.c:252` lazy-load fix). Check `rebuild.log` last line before crash, add `appLog()` guards, rebuild, rerun with lazy-load for `menu\gfx00.tpg` and `menu\char*.tpg`. Under Wine `fopen("menu\\gfx00.tpg")` is translated by msvcrt, but case matters on Linux — try both `gfx00.tpg` and `GFX00.TPG`; Wine is case-insensitive but `fileExists @0x408f60` via `fopen` may not be, so the code now tries both.
* **Texture not found:** `find menu -iname "*.tpg" | sort` lists the real files (`ROLAND00.TPG`, `KAJSA00.TPG` …). The char-select mapping is at `charselect.c: kCharTpg[10]` indexed by `g_nCharSelIdx @0x45d480`. If you see `char tex ... missing, using TOM`, the mapping is wrong — fix the table, not the fallback.
* **Exit code 143 vs 0:** `timeout` kills with SIGTERM → 143, or `PostQuitMessage(0)` → 0. Both are clean. `124` is `timeout 5` expiring with no input — expected for `run.sh`.
* **Stuck `jjjjjjjj` on host after exit (critical):** Previous versions sent `xdotool key --window $WIN j` then killed Wine before the X key-up was delivered. The host then sees a held `j`. **Always** use `--clearmodifiers` and kill instead of `j` for scripts. If stuck, run: `DISPLAY=:0 xdotool keyup j; DISPLAY=:0 xdotool keyup J; DISPLAY=:0 xdotool keyup Escape; DISPLAY=:0 xdotool keyup Return` and avoid sending WM_CHAR keys after `WIN` is gone. Check `ps aux | grep wine` — no `maniac_rebuild.exe` should remain.
* **Stale log:** Always `rm -f rebuild.log` before launching; `gameFrameUpdate` appends, and multiple `wine` runs in the same dir share the log.

## 7. One-liners for agents

*Smoke (no input, just verify palette/fonts load):*
```bash
./build.sh && ./run.sh 2>&1 | tail -n 20
```

*Full Spela → char-select → select → level-select stub:*
```bash
/tmp/test_charselect_enter.sh  # or paste the 3-key recipe above
cat rebuild.log | grep -E "menu|charselect|LevelSelect"
```

*Quick fault isolation (adds verbose logs):*
Append `appLog("[where] x=%d", val);` in the suspect state, `build.sh`, rerun the recipe, `cat rebuild.log | tail -n 50` — last line before `Unhandled` is the fault site.

Keep this file alongside `docs/12-input.md` and `docs/16-rebuild.md`. Update it when new states add new keys or when DirectInput is re-enabled.
