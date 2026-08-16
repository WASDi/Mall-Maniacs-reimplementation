# Mall Maniacs (maniac.exe) — 15. Undefined-type retyping plan

[Back to README](README.md)

Source of the census: `FindUndefinedTypes.java` (see ../Ghidra_scripts.md) run on
maniac.exe. It lists every function/global whose return type / data type is
still `undefined*`, excluding the CRT code region 0x43c850-0x44966c (functions)
and the CRT data region 0x454c00-0x456210 (globals). Full output saved to
`/home/wasd/.local/share/opencode/tool-output/tool_00abf1baf001wGqmiSVW2hgQDk`.

## 1. Scope

Retype every remaining undefined-typed item in the census EXCEPT:

- **Dispatch table** 0x44b320-0x44b43c (console command dispatch: {handler fn,
  name string} pairs; indexed via g_pfnCmdAction @0x44b308 / g_pCmdNameAction
  @0x44b30c). Leave untyped.
- **Switch/jump tables** 0x41bf3c, 0x41ef20, 0x41ff98 (Ghidra switch data).
  Leave untyped. (Stored type is `undefined *`/`pointer` PointerDB; this is the
  "addr" the GUI displays — resolved rendering of pointer data.)
- **CRT** (ignore entirely): the 148 `Unwind@0044xxxx` EH stubs at
  0x4497a0-0x44a2f0, and the CRT `.rdata` pointer/string tables
  (0x44b82c/0x44b87c/0x44b91c/0x44c0a8/0x44c1e0/0x44c20c CRT printf/locale
  tables, 0x44e004/0x44e028 C++ static-init/atexit tables, g_crtFscanfCore
  @0x45257a/57b). These are statically-linked MSVC CRT — not game code.

### Functions (non-CRT, non-Unwind): 578
| return type | count | likely resolution |
|---|---|---|
| undefined | 276 | mostly `void` (init/update/render/dtor that fall through); verify per-function |
| undefined4 | 215 | `int` status/flag or `void`; check decompiler + callers |
| undefined4 * | 29 | pointer return (ctor/alloc/getter) → retype pointee |
| undefined2 * | 12 | pointer return (dataReadU8-style byte-stream) |
| undefined8 | 8 | `long long`/`double` pair (sceneNodeGetPos*, playerCheckTurn) |
| undefined * | 4 | `void *` |
| void * | 4 | keep / retype to typed struct ptr |
| undefined1 | 3 | `char`/`uchar` flag |
| bool | 3 | keep |
| undefined2 | 2 | `short`/`ushort` |
| int * | 2 | retype to typed ptr |
| short * | 2 | fontLoad/fontParse → `short *` is real |
| float10 | 2 | `double` (configEnvGetDouble, navPointRelaxCosts) |
| undefined1 * | 2 | byte-stream ptr |
| undefined1[10] | 2 | **BUG**: mathSinDeg/mathCosDeg — should be `float` |
| MCIERROR | 1 | keep (mciPlayCdaudio) |
| float * | 1 | retype pointee |
| uint | 1 | keep (mnetSrvSendToAll) |

Param counts: 360 no-params, 152 × 2-params, 29 × 3-params, 14 × 4-params,
7 × 7-params, 6 × 6-params, 5 × 5-params, plus a handful of 8/9/10/16-param
thunks (objShotAdd/objShotCtor 15-param). 218 functions carry `undefined*`
params.

### Globals (non-CRT, non-table): 2
- 0x44f0e8 and 0x44fdc0 — game string-descriptor tables:
  - 0x44f0e8: `{ char* →0x44f0f0 ("scene_future","scene_aqua",...), count, ...}`
    = scene-name table (game data, in .rdata).
  - 0x44fdc0: `{ char* →0x44f0f0, count, "%s\hud\froge00.tpg" path template }`
    = HUD/path string descriptor.
  - These are the ONLY non-CRT, non-table undefined globals after exclusion.

## 2. Method

1. **Evidence-first**: batch-decompile the target function; read the decompiler
   signature + body; cross-check callers/xrefs for the real return semantic
   (status int vs pointer vs void). Mark results [VERIFIED] in plate comments
   only where decompilation confirms; otherwise keep as retype-with-hypothesis.
2. **Apply** via `set_function_prototype` (prototype string; does not rename) and
   `set_parameter_type` / `batch_rename_function_components` for param names +
   locals. Do NOT rename functions (naming already complete).
3. **CRT/table globals**: leave untouched (no re-type; documented above).
4. Order: globals first (2), then function categories by clarity:
   a. mathSinDeg/mathCosDeg float bug
   b. `undefined` → void where body falls through
   c. `undefined4` → int/void per decompile
   d. pointer returns (undefined4*/undefined2*/undefined*/void*)
   e. undefined8/float10 → double/longlong
   f. remaining undefined params (218 funcs)
5. **Verify**: re-run the census script → expect 0 undefined-typed game funcs +
   globals (only dispatch/switch tables + CRT remain). Save program.
6. Update `docs/11-issues.md` §16 with a completed-work summary; no git commit
   unless asked.

## 2a. Execution status (2026-08-16)

Mechanical pass done via `RetypeUndefinedFunctions2.java` (HighFunction
`getFunctionPrototype()` — the recovered prototype, not the stored one).
Results:

- **Globals (2/2)**: 0x44f0e8 → `g_szSceneNameTable` (`char *`), 0x44fdc0 →
  `g_szHudPathDescriptor` (`char *`). All other 137 census globals are
  excluded tables/CRT — global retyping COMPLETE.
- **mathSinDeg/CosDeg (0x42d030/0x42d050)**: corrected to
  `long double __cdecl (short nDeg)` — fsin/fcos return x87 float10, NOT float.
- **Functions**: script processed 577 game funcs; `retApplied=281
  paramsApplied=8`. Census dropped 724 → 526 (incl. the 148 Unwind stubs).
  Game funcs still flagged: **378**.

Remaining 378 breakdown (ClassifyUndefinedRemaining.java):
- 213 × `undefined4 (params=none)` — decompiler's OWN recovered return type is
  genuinely `undefined4` (propagated from dispatch/switch-table callers or
  genuinely ambiguous status/flag). Mechanical pass ceiling reached.
- 82 × `void (params=some)` — return OK, params need manual semantic typing.
- 29 × `undefined4 *`, 12 × `undefined2 *`, 6 × `undefined8`, 4 × `undefined *`,
  3 × `undefined1`, 2 × `undefined1 *`, 1 × `undefined2` (params=none).
- 26 × concrete return with undefined params (8 int, 4 void*, 3 bool, 2 int*,
  2 longlong, 2 short*, 2 float10, 1 float*, 1 uint, 1 MCIERROR).
- 108 funcs carry at least one `undefined*` param.

**Conclusion**: the decompiler-recovered-signature pass is exhausted. What
remains requires per-function semantic analysis (callers, struct layout, game
semantics) — the manual "semantics polish" already tracked in
`docs/11-issues.md` §15. The 213 `undefined4` returns are legitimately left
untyped rather than guessed; `undefined4` is a valid int-sized placeholder.

## 2b. Global data-type pass (2026-08-16, ListUntypedGlobals.java)

Targeted the 50 GAME globals with `undefined`/no type (evidence-first; skip
0-xref, CRT, dispatch/switch tables). Completed items (all applied):

- **Character/scene names**: g_szSceneAxel `char[8]`@0x450308 "AXEL";
  g_szCharBosse `char[16]`@0x450368 "Bosse Bänkpress"; g_szCharAgata
  `char[17]`@0x450394 "Agata von Gördel"; g_szCharAke `char[9]`@0x4503a8
  "Åke Lönn"; g_szCharRoland `char[15]`@0x4503c4 "Roland Blåvind".
- **Net game names**: g_szNetGameIcaSmakop `char[11]`@0x45048c "ICA Småköp"
  (g_szNetGameVagnrace/Matkrig/Varujakten were typed earlier).
- **Scene text anim**: g_apSceneTextAnimGlyphs `void *[16]`@0x45ec90;
  g_bSceneTextAnimActive → renamed **g_nSceneTextAnimActive** `int`@0x45ecd0
  (disasm `MOV [0x45ecd0],EAX`/`MOV EAX,[0x45ecd0]` — flag, not bool-typed).
- **Menu records**: g_abMenuRecord2 `byte[40]`@0x45c1cc. g_abMenuRecord1
  @0x45c1a4 intentionally SKIPPED — it is the tail sub-symbol of
  `g_anMenuRecInitB int[15]`@0x45c16c (menuInit copies 10 dwords from
  `g_anMenuRecInitB[0xe]` = 0x45c1a4). Siblings 0/3/4/5/6 already `byte[64]`.
- **Player**: g_szPlayerName `char[24]`@0x45a400 (0x45a400..0x45a417, bounded
  by `g_anNetPlayerCarts int[8]`@0x45a418; max 18 chars `CMP ECX,0x12`);
  g_playerRecords `SceneObject[8]`@0x456210 (6112 B; records stride 0x374,
  player view sub-object g_apPlayers @ +0x150).
- **Byte fields**: g_consoleKeydf `byte`@0x4551df; g_netSetupb6 `byte`@0x45a3b6;
  g_netSetupb7 `byte`@0x45a3b7.
- **Misc**: g_sceneRenderT_2 `float`@0x450fa4 (1.0f, tail of 3x3 matrix from
  g_flSceneScaleOne@0x450f84); g_abWsaData `int`@0x4623f8 — **WSA refcount**,
  NOT WSADATA (mnetWsaStartup: `if (g_abWsaData==0) WSAStartup; g_abWsaData++`).

Resolved without typing (sub-symbols of already-typed parents, per convention):
g_netSetupf4/00 (in g_apNetGameNames char*[20]), g_charSelect60 (in
g_apCharNames char*[10]), g_charSelect88 (in g_apCharSceneNames char*[10]),
g_introFade (byte1 of g_menuMode int@0x45022c), g_hiScore49/4a (in
g_szScoreKeyBug char[5]), g_charSelect41/42 (in g_szCharKajsa char[12]),
g_hiScore99/9a (in g_szNetGameVagnrace char[9]), g_cCmdRequest* ×9 (in
g_szReqEndscene/g_szReqPlayLevel/g_szReqVahiScore), g_netSetupf9/fa (in
g_szNetLblKaraktar char[9]), g_levelInitcd (byte1 of g_levelInitcc int@0x4583cc),
g_netSetupfe/ff/01 (strlen/backspace sub-labels of g_szPlayerName), g_netLobbya8
(in g_abMenuRecord1 tail), g_netLobbyd0 (in g_abMenuRecord2 byte[40]),
g_menuFramead/ae + _2 (in g_menuFrameac int@0x45c3ac +0x100 row),
g_netServerState (byte1 of g_netLobbya8_2 int@0x45e4a8), g_sceneSystem1a (in
g_sceneSystem18 int@0x45e818), g_sceneSystem24 (overlaps g_sceneSystem23),
g_apSndBank (overlaps g_apSceneTextAnimGlyphs — shared bank-0 region; both
hard-coded bases in binary).

Deleted: **g_netLobby52** label @0x450b52 — false label inside "Avbryt"
(g_szLobbyCancel char[7]@0x450b50), 6 xrefs.

Remaining 39 GAME untyped globals are ALL sub-symbols/overlaps listed above —
no standalone untyped game globals remain. Note: `apply_data_type` array syntax
needs decimal counts (`byte[40]` not `byte[0x28]`); fails on overlapping defined
items (clear overlaps first via ClearDataItems.java, whose TARGETS list is kept
current).

## 3. Conventions

- Subsystem prefixes already established (gx*, net*, mnet*, config*, scene*,
  snd*, player*, obj*, ai*, state*, levelEventDirector*).
- `__thiscall`/`__fastcall` already set where known; preserve calling convention
  when retyping.
- Hungarian prefixes per repo convention (n* int, f* float, p* pointer, g_* globals).
- No comments in code unless they are plate comments documenting evidence.