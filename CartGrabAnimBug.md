# CartGrabAnimBug — hand animation when player grabs a cart

Handoff for next agent. Goal: fix bugged upper-arm orientation when the player is holding a cart; verify via in-game `xdotool` navigation and `rebuild.log`.

## 1. Project context

- Original binary: `/home/wasd/MallManiacsUnmodified/maniac.exe` (image base `0x400000`), Ghidra project `/home/wasd/ghidra/MMUnmod` program `maniac.exe` + `gxSoft.dll`.
- Rebuild: `src/maniac.c` + per-subsystem files, builds 32-bit Windows `i686-w64-mingw32-gcc` with `KERNEL32 USER32 GDI32 WINMM` + `DRIVERS\GXSOFT.DLL` via GX. Output `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe`, `make` in `/home/wasd/auto-ghidra`.
- Preserved call hierarchy: reimplemented functions may only call functions the original called (`src/stubs.c` for TODO stubs, `src/custom_helpers.c` for debug/logging utils).
- Docs: `docs/README.md`, `docs/16-rebuild.md` (current status, not work log), `XDOTOOL_NAVIGATION.md` (mandatory input method), `Ghidra_scripts.md` (file+manual javac+`ghidra_run_ghidra_script` — `ghidra_run_script_inline` is broken).
- Ghidra MCP: pass `program=maniac.exe` when multiple programs open. Save with `ghidra_save_all_programs`.

## 2. Bug as reported

- With a cart grabbed, the upper arm (sub-meshes 6/3, driven by `pOutAngles` from `playerAnimOrientFromDir`) points **backwards**; the lower arm/hand (sub-meshes 7/4, driven by `pOutWalk`) points forwards and tracks the cart vertically. If the upper arm pointed forwards the whole arm would connect to the cart. Moving the cart up a ramp changes arm direction roughly correctly but not fully.
- Prior to this work arms appeared folded near shoulders.

## 3. Relevant code and addresses

### 3.1 Call sites — `src/gameplay.c:1016` (`playerAnimSfxUpdate @0x40c800`, verified `0x40cb48-0x40ccff`)

```c
// src/gameplay.c:1021 — first limb pair (left/right split by roll +-16000)
sceneSetCurrentObj(pRec->pCharSceneObj, 1);                         // @0x430d98 @0x40cb5c
sceneNodeGetPos(pRec->pCartChildB, 0, anCart, 6);                   // @0x431270 @0x40cb70
sceneNodeGetPos(pRec->pCharSceneObj, 6, anCart + 3, 2);            // @0x40cb85  — overwrites anCart[3],anCart[4] (mode 6)
playerAnimOrientFromDir(anCart[0]-anCart[3], anCart[1]-anCart[4], anCart[2]-anCart[5],
                        anAngles, anWalk, NULL, &g_awWalkAnimTable[0][0], 0x1ea, (short)-16000); // @0x4336b0 @0x40cbdb
sceneObjSetSubPos(pRec->pCharSceneObj, 6, anAngles[0],anAngles[1],anAngles[2], 2); // @0x430a90 @0x40cbfd
sceneObjSetSubPos(pRec->pCharSceneObj, 7, anWalk[0],anWalk[1],anWalk[2], 2);       // @0x40cc1c

sceneNodeGetPos(pRec->pCartChildA, 0, anCart, 6);                   // @0x40cc30
sceneNodeGetPos(pRec->pCharSceneObj, 3, anCart + 3, 2);            // @0x40cc48
playerAnimOrientFromDir(anCart[0]-anCart[3], anCart[1]-anCart[4], anCart[2]-anCart[5],
                        anAngles, anWalk, NULL, &g_awWalkAnimTable[0][0], 0x1ea, (short)16000); // @0x40cc98
sceneObjSetSubPos(pRec->pCharSceneObj, 3, anAngles[0],anAngles[1],anAngles[2], 2); // @0x40ccb7
sceneObjSetSubPos(pRec->pCharSceneObj, 4, anWalk[0],anWalk[1],anWalk[2], 2);       // @0x40ccd9
```

- `sceneNodeGetPos @0x431270` mode 6 = `wmat*(posNode-posHead)` relative to `g_pSceneNodeHead @0x45e810` / `g_nSceneCurrentObj @0x45e608` set by `sceneSetCurrentObj @0x430d98` (see `src/scene_transform.c:270`). Mode 4 walk, mode 2 raw local. Verified via disasm `0x40cb6b-0x40cc5d` pushes and ESP offsets — original and rebuild match including `dirZ = anCart[2]-anCart[5]` (cart posZ minus cart local Z), with `anCart+3` overlap for slots 3/4.
- `sceneObjSetSubPos @0x430a90` mode 2 is raw `rot[3]` assignment; mode 5 is look-at composition — cart limbs use mode 2.
- `sceneChannel` layout in `src/scene.h`: `rot[3]@0, fUnk6@6, bFlagA@0xa, bFlagB@0xb, nIdx@0xc, x,y,z@0x10, matr[9]@0x1c, wmat[9]@0x40, wx,y,z@0x64`, packed `0x70`. Ghidra frame at `0x4336b0` confirmed: `local_30 @-0x30` is `afM[0]`, size `60`, params at `+4..+0x24`.

### 3.2 Target function — `playerAnimOrientFromDir @0x4336b0` (`src/anim.c:911`)

- Signature 9 cdecl args `(nDirX,nDirY,nDirZ,pOutAngles,pOutWalk,pUnused,pWalkTable,nWalkGeom,nRoll)` returns 1. `pUnused` is pushed but never read (compiler reuses arg slots 1..3 as float scratch). Callers use `nWalkGeom=0x1ea`, `nRoll=±16000`.
- Helpers: `mathAtan2Deg @0x42d010` (`FLD y; FLD x; FPATAN; FMUL [0x44b780=10430.378...]; JMP __ftol`), `mathSinDeg @0x42d030` / `mathCosDeg @0x42d050` (`movsx; FILD; FMUL [0x44b788=9.5873e-05]; FSIN/FCOS`), `__ftol @0x43dd10` (`FISTPLL`), constants `127.0 @0x44b7b0` / `1.0 @0x44b288`, `g_dblBdgToRad @0x44b788`, `g_dblRadToBdg @0x44b780`.
- Walk table `g_awWalkAnimTable @0x458138`: `short [128][2]` = `{orient, walkAngle}`; filled in `roundStartInit @0x40a4d0` at `0x40a941-0x40a981` via `walkAnimTableEntryCalc @0x433980` with center `0xd2` radius `0x118`.
- Four matrix loops (verified ESP-accurate vs `0x43377c-0x4338ab`):
  - Init `afM[0..5] = {0,0,1, 0,1,0}` (`+Z` forward, `+Y` up).
  - `flLen = sqrt(dx²+dy²+dz²)`, `nI = (int)(flLen*127.0/nWalkGeom)` clamped `0x10..0x7f`, `sWalk = pWalkTable[1+nI*2]` → `pOutWalk[0]`, `flSin/CosWalk = sin/cos(pWalkTable[nI*2]+0x4000)`.
  - Loop1 `0x43377c`: swing seed rows into `afM[6..11]`.
  - Loop2 `0x4337cf`: roll fold `afM[0]=cosR*m6-sinR*m7` etc.
  - Loop3 `0x433829`: tilt toward dir (`fA=nDirY/flLen, fB=sqrt(x²+z²), fC=fB/flLen`).
  - Loop4 `0x433873`: finish from `z/x` over `fB`.
  - Tail `0x4338ad-0x43396c`: `inv=1/sqrt(m0²+m2²)` (`FDIVR 1.0`), `fA=inv*m0, fB=inv*m2, fU=fB*m2+fA*m0` (=`s`), `yaw=atan2(-m1,fU) @0x433903`, `pitch=atan2(fA,fB) @0x433919`, `roll=atan2(fA*m5-fB*m3, sinR*(fB*m5+fA*m3)+fU*m4) @0x433960` (with `FCHS`).

### 3.3 Scene/view helpers

- `chanBuildRotMatrix @0x42f030` builds `matr` from `rot[3]` with same column-major `m[col*3+row]` and `fUnk6` scale (`src/scene_transform.c:654`).
- `chanCalcWorldTransform @0x42f6e0` / `mat3x3Mul @0x42f7d0` used by `sceneNodeGetPos` mode 4/6.

## 4. What was already fixed in this session

### 4.1 Walk-table constant (`src/gameplay.c:226`, `src/anim.c:873`)

- Before: `walkAnimTableEntryCalc(..., (float)i*0.01f, ...)` — wrong step.
- After: `static const float g_flNormSpeedStep = 0.0078740157f; // @0x44b458 = 1/127, bytes 15 62 01 3C` and caller uses `i*g_flNormSpeedStep`. This matches the binary float at `0x44b458`. The `0.01f` value corrupted every table entry (binary-search target `490*i/127` vs `490*i*0.01`) and produced saturated `walk=32767` etc. After fix, first sample gave `walk=0, angles=(312,-13289,9789)` vs prior saturated values.
- Ghidra data at `0x44b458` renamed to `g_flNormSpeedStep`.

### 4.2 `playerAnimOrientFromDir` tail (`src/anim.c:1002`, plate comment at `0x4336b0`)

- Before: used `fC=m0²+m2²`, then `fC*sqrt = s`, `fA=s*m0, fB=s*m2`, yaw `atan2(-m1,s)`, pitch `atan2(fB,fC)` or `atan2(fA?,fB?)`, roll referenced `pOutWalk/pOutAngles` and wrong `m` slots.
- After: exact FPU-matched sequence above (`inv=1/s, fA=m0/s, fB=m2/s, fU=s, yaw=atan2(-m1,fU), pitch=atan2(fA,fB), roll=atan2(fA*m5-fB*m3, sinR*(fB*m5+fA*m3)+fU*m4)`). Loops and `g_dblOne`/`127.0` verified.
- Ghidra plate comment at `0x4336b0` updated and verified; `UNCERTAIN` note removed via `ghidra_set_plate_comment`. Saved with `ghidra_save_all_programs`.

### 4.3 Build and transient debug

- Rebuilt with `make` → `/home/wasd/MallManiacsUnmodified/maniac_rebuild.exe` (currently `1328660` bytes, Aug 31 12:29). Build warnings are unused-stub only.
- Temporary `appLog` throttling in `src/gameplay.c:1044` (`static nLogSkip 120`, `custom_helpers.c:16` → `rebuild.log`): `[dbg-cart] limb6 dir=(x,y,z) angles=(y,p,r) walk=(...) | limb3 roll=+16000 angles=...`. Must be removed before final submit. Previous samples: `dir=(-80,182,-243) angles=(312,-13289,9789) walk=(0,0,0)` then `dir=(-88,209,500) angles=(-356,-18343,20329) walk=(32767,0,0)`; recent run with `dir≈(-80,169,506)` etc. all `walk=32767` when length clamped to 127.

## 5. Current state and open bug

- The four loops + walk-table + call-site push order now match the original disassembly at verification level. Remaining visual bug is isolated to `pOutAngles` (6/3) — `pOutWalk` (7/4) appears correct (forwards, tracks cart vertically). So focus on the `afM→Euler` extraction or the `dir` fed into it, not the table index or `sceneObjSetSubPos` wiring.
- No inverted-dir fix has been applied yet; previous attempt to flip `dir` sign was not tested visually. The mode-6 subtraction order `posCart - posHead` at `src/scene_transform.c:348` and `sceneNodeGetPos` `wmat * diff` should be re-audited with a fresh Ghidra dump of `0x431270`.

## 6. Hypotheses to test next (in priority order)

1. **Inverted `dir`** — original is `wmat*(anPos-anHead)` where `anHead` is `pCharSceneObj` channel 6/3 pos (or `pCartChild`? check `sceneNodeGetPos` chain at `0x431450-0x431570`). Try `-(anCart[0]-anCart[3])` etc. for both limb pairs; run wine harness calling original bytes at `0x4336b0` with synthetic `dir` to confirm sign.
2. **Euler order/sign swap** — `chanBuildRotMatrix` order is `rot[0]=yaw, rot[1]=pitch, rot[2]=roll` with specific `matr` column-major layout. The `yaw/pitch` extraction or `roll` `FCHS` may be swapped/negated vs how `sceneObjSetSubPos` applies `rot`. Brute-force the 4 combos: `pitch=atan2(fB,fA)` vs `atan2(fA,fB)`, swapped `pOutAngles[0]/[1]`, negated `pitch`, and negated roll numerator (`-(fA*m5-fB*m3)`). Each rebuild is one tail tweak in `src/anim.c:1002`.
3. **`nRoll` handedness / `pUnused` reuse** — caller passes `-16000` for mesh 6 and `+16000` for mesh 3; the extraction uses `flSinR` from `nRoll`. Verify the original pushes `0xffffc180` (-16000) for 6/7 and `0x3e80` (+16000) for 3/4 are not swapped in the rebuild, and that the roll blend sign (`flSinR*(fB*m5+fA*m3)+fU*m4`) is not `flSinR*(fB*m5 - fA*m3)` etc.
4. **Channel assignment** — confirm `anAngles` → mesh 6 with walk → mesh 7 (first pair) and `anAngles` → mesh 3 with walk → mesh 4 (second pair) is not crossed with the original's `LEA EDI,[ESP+0x48]` vs `+0x60` buffers (dis `0x40cbb2-0x40cc79`). ESP trace shows rebuild matches, but double-check via a fresh script dump of `0x40c800` frame.
5. **Walk-table entry interpretation** — `pOutWalk[0]=sWalk` is saturated at idx 127 (`32767`). If the visual bug only occurs at saturated lengths, test clamping `nI` to `0x7f` vs not, or whether `pWalkTable[nI*2]` vs `pWalkTable[1+nI*2]` are swapped for the swing.

## 7. How to verify (follow `XDOTOOL_NAVIGATION.md` exactly)

```bash
cd /home/wasd/MallManiacsUnmodified
killall -9 maniac_rebuild.exe 2>/dev/null; sleep 1
rm -f rebuild.log /tmp/wine_out.log
wine ./maniac_rebuild.exe > /tmp/wine_out.log 2>&1 &
for i in $(seq 1 20); do WIN=$(xdotool search --name "Mall Maniacs" 2>/dev/null | head -n1); [ -n "$WIN" ] && break; sleep 0.25; done
xdotool windowactivate $WIN 2>/dev/null; sleep 2
send_key() { xdotool keydown --window $WIN "$1"; sleep 0.15; xdotool keyup --window $WIN "$1"; }
for i in $(seq 1 25); do grep -qa "main menu active" rebuild.log 2>/dev/null && break; send_key space; sleep 0.8; done
for k in 1 2 3 4; do send_key Return; sleep 1; grep -qa "round 0 initialized" rebuild.log 2>/dev/null && break; done
# grab cart: hold Up + Space
for i in $(seq 1 12); do xdotool keydown --window $WIN Up; sleep 0.3; xdotool keyup --window $WIN Up; send_key space; sleep 0.5; grep -qa "dbg-cart" rebuild.log && break; done
grep -a "dbg-cart" rebuild.log | head; grep -a "round 0 initialized" rebuild.log
# screenshot if needed: import -window $WIN /tmp/opencode/cart.png
```

- Keys mapping in `src/maniac.c:WindowProc` → `g_abInputKeyHeld[8]` (`Right0/Left1/Up2/Down3/Space4/Enter6/Escape7`), `pollKeyboard @0x416a10` (menu, Enter edge) vs `pollKeyboardGame @0x416820` (gameplay, per-frame). Always use `keydown --window $WIN` + 150ms hold.
- Logs: `rebuild.log` (game markers `[menu]`, `[gameplay]`, plus temporary `[dbg-cart]`), `/tmp/wine_out.log` (crash faults). `appLog` in `src/custom_helpers.c:16` appends to `rebuild.log`.

## 8. Working practices for this repo

- Always inspect both `ghidra_decompile_function` and `ghidra_disassemble_function` at the same address; decompile is lossy.
- ESP-accurate simulation required: entry `ESP0` is ret addr at `[ESP0]`, args at `ESP0+4*j`, locals at `ESP0-0x38 .. ESP0-1` (frame `60`, param offset `4`). A previous fix was wrong because it used `s*M0` instead of `M0/s`. Keep `simstack.py`-style or `/tmp/esp_trace.py` ESP tracking.
- Use precise types (`short`/`float`/`double`), name everything, create structs instead of raw `+0x..` offsets.
- Preserve call hierarchy — do not invent callees. Stub unresolved deps in `src/stubs.c/.h`.
- Comment reimplemented functions/globals with original address above definition; keep rebuild in `src/anim.c` + `src/scene_transform.c` + `src/scene_system.c` in sync with Ghidra (`set_function_prototype`, `create_struct`, `set_plate_comment`, `rename_data`).
- To run Ghidra scripts: write `.java` to `/home/wasd/ghidra_scripts/`, **manual `javac` required** (`GHIDRA=/home/wasd/Desktop/ghidra_12.1_PUBLIC CP=$(find $GHIDRA -name "*.jar" |tr '\n' ':') javac -proc:none -cp "$CP" -d /tmp/opencode/classdir file.java`), then `ghidra_run_ghidra_script(script_name="file.java", program="maniac.exe")`. Ignore stale `McpInline_*` build-cache noise.
- Runner `TrackRebuildDetailed.java` writes to `/tmp/opencode/tracked-rebuild-detailed.txt`.

## 9. Key files and commands

- `src/anim.c:930-1016` `playerAnimOrientFromDir`, `src/anim.c:883` `walkAnimTableEntryCalc`, `src/gameplay.c:1016` cart limb aim, `src/scene_transform.c:270` `sceneNodeGetPos`, `src/scene_system.c:72` trig, `src/scene.h:SceneChannel/SceneNode`, `src/custom_helpers.c:16` `appLog`.
- Build: `make` (writes `maniac_rebuild.exe`); `python` one-liners for constant checks (`struct.unpack '<f'` of `@0x44b458`), `objdump` for `__ftol @0x43dd10` bytes.
- Git diff currently: `src/anim.c` (tail rewrite + comment) and `src/gameplay.c` (constant + `nLogSkip`). Uncommitted; last commit `de38354 cart grab works`. `~/.config/ghidra/...` cache is poisoned for `McpInline_cdbb6984a57`/`a558382203e` — do not delete, just manual-compile.

## 10. Next steps checklist

- [ ] Remove or gate `src/gameplay.c:1044 [dbg-cart]` before final verification.
- [ ] Run file-script dump of `sceneNodeGetPos @0x431270` mode 6 and confirm `anHead` subtraction sign; if inverted, flip `dir` in both call sites and rebuild.
- [ ] Brute-force tail permutations in a Ghidra file script vs wine harness of original `0x4336b0` bytes, pick the one where `pOutAngles` upper arm points forwards, then port to `src/anim.c:1002`.
- [ ] Re-run xdotool flow, keep game open for user visual confirmation, capture `import -window $WIN` screenshot if available.
- [ ] Clean `make`, update `docs/16-rebuild.md` status, `ghidra_save_all_programs`.

---

NOTE: Code references may be outdated. This was previously at `src/gameplay.c:1044`:

                {   /* TODO: temporary cart-limb debug logging, remove after visual verification */
                    static int nLogSkip = 0;
                    if (nLogSkip-- <= 0) {
                        nLogSkip = 120;
                        appLog("[dbg-cart] limb6 dir=(%d,%d,%d) angles=(%d,%d,%d) walk=(%d,%d,%d) | limb3 roll=+16000 angles=(%d,%d,%d)",
                               anCart[0] - anCart[3], anCart[1] - anCart[4],
                               anCart[2] - anCart[5],
                               anAngles[0], anAngles[1], anAngles[2],
                               anWalk[0], anWalk[1], anWalk[2],
                               anAngles[0], anAngles[1], anAngles[2]);
                    }
                }
