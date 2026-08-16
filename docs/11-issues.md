# Mall Maniacs (maniac.exe) — 11. Unknowns / next steps

[Back to README](README.md)

## 14. Unknowns / hypotheses (open)
- None blocking. All 1178 functions in maniac.exe are documented/named (0
  undocumented). Major subsystems renamed (game flow, networking, config,
  renderer, sound, file formats, input); MSVC CRT region 0x43c850-0x44966c fully
  mapped and renamed. Data surface fully named (pass 5c): 605 globals renamed,
  571 typed; only documented aliases/artifacts remain as DAT_*. Remaining work
  is semantics/struct detail rather than naming: verify remaining hypotheses,
  tighten types/structs/enums, and cross-reference with the game data files in
  /home/wasd/MallManiacsUnmodified/.
- Open [HYPOTHESIS] plate comments: g_nScoreTableTick @0x45d43c (per-frame
  counter in stateHighScoreTable @0x41dfd0). Convert to [VERIFIED] on next
  revisit. (g_bSceneNameTableDirty was resolved — see §16 pass 5b: it is the
  scene-directory path buffer g_szSceneDir @0x45e950.)

## 15. Next steps (priority order)
1. (Optional cleanup) The 34 below-image-base `DAT_*` operand-artifact labels
   (DAT_00000002, DAT_0004d2fc, ...) — Ghidra auto-created for absolute operand
   refs; harmless but could be deleted for a clean symbol table. Verify each has
   no logical meaning before deleting.
2. Semantics polish: review the "hypothesis" plate comments for the last CRT
   passes (see §16) and convert to [VERIFIED] where decompilation confirms them.
3. Struct/type tightening: game/scene/net/sound/input/movie globals + 9 UI
   string literals now typed (see §16 global-typing pass). CRT cleanup largely
   done (pass 5): file count @0x462b20, TLS index @0x452400, codepages,
   mb-bytes @0x452788, heap @0x462a0c, io write buf @0x45493c, lock critical
   sections @0x4527b0..f4, tz std/dst name buffers @0x454c8c/90 + wide sources,
   ctype sources @0x44bdb0/b4, movie frame prefixes @0x44f488..498. Still to
   do (low priority, mostly aliases or string-interior): DAT_00452130/00452150
   FILE structs, DAT_00462640 stream buffers, _environ DAT_00462464/68, remaining
   timezone scalars. Intentionally left untyped (aliases of typed arrays — do
   NOT re-type): g_apPlayers @0x456360 (typed void*[8]; Player view at +0x150 of
   g_playerRecords SceneObject[8]), g_bSceneTextAnimActive @0x45ecd0 /
   g_apSceneTextAnimGlyphs @0x45ec90 (overlap g_apSndBank bank 0), plus the 160
   protected-region aliases in 0x456210-0x457db0 (g_playerRecords) and
   0x45ec90-0x45ecd0 (see pass 5c).
4. Update the docs/ files with each step's findings; save program regularly.
5. Optional: archive_ingest_program to push the completed documentation set into
   the cross-version archive (re_kb).

## 16. Completed work (short summaries — detail in the linked docs)
- Global-naming pass 5c (CLOSED): bulk-typed and named the last 608 non-
  protected `DAT_*` globals via a generated Java script (ApplyDatTypes.java,
  see ../Ghidra_scripts.md). Final counts: **605 renamed, 571 typed** (37
  create-fails are string-interior byte aliases whose parent data item already
  covers the address — renames still applied; 2 bogus addresses 0x80000009/6f
  and the in-.text item 0x43f90d intentionally left). 160 items in the
  g_playerRecords / sceneTextAnim / sndbank0 regions were deliberately NOT
  re-typed (documented aliases, see below). Remaining `DAT_*` in the program =
  196 = the 160 protected aliases + 34 low-address operand artifacts (e.g.
  DAT_00000002, DAT_0004d2fc — below-image-base labels Ghidra auto-creates for
  `mov [imm],...` operands; not real globals) + 2 bogus. Categories:
  - 178 float/double consts → `g_fl_*`/`g_dbl_*` value-named; semantic
    overrides: `g_dblTrigScaleInv` @0x44b780 (10430.378 = 32768/π),
    `g_dblTrigScale` @0x44b788 (9.587e-05 = π/32768), `g_flZero`/`g_flOne`/
    `g_flMinusOne`/`g_flHalfPi`/`g_flPi`/`g_flPi2`/`g_flMinusPi`/`g_flTwoPi`,
    `g_dblFourThirds` @0x44b798.
  - 65 strings → `g_sz_*` content-named (incl. `g_szCrtErrPrefix` @0x44bd94).
  - 46 command-token strings + interior char bytes (`g_cCmdPlay` etc.; movie
    play/rec/stop @0x44ecdc/f0/fc, actionCmd "jump" @0x44e718, eload/esave
    "x"/"y"/"h", enameCmd "v", logCmd "file", consoleHandleKey " ").
  - 163 read-only data, 73 mutable globals, 27 float/double data globals, 21
    rdata ptr-or-const, 10 binary blobs, 7 int consts, 7 CRT scalars, 4 rdata
    ptrs, 2 GUIDs (`GUID_SysMouse` @0x44b730, `GUID_SysKeyboard` @0x44b740),
    2 DIDATAFORMAT (`c_dfDIMouse` @0x44b750, `c_dfDIKeyboard` @0x44b768).
  - L2 sign-wobble doubles verified from levelEventDirector_L2 @0x418000:
    `g_dblL2PhaseX/AmpX/PhaseZ/AmpZ/PhaseY/AmpY` @0x459e38..0x459eb0.
  - CRT: `g_pfnAtexit` slots, `g_crtHeapMutex` @0x4624f8, `g_crtFatalExit`
    @0x462490, `g_crtTableTail` @0x454dd0, `g_crtZeroPad` @0x4627a8,
    `g_flPhysZero` @0x44e320.
- Global-naming pass 5b (CLOSED): final drive-down to a fully-named data surface.
  Every defined program global now carries a semantic `g_*` name; the only
  remaining `DAT_*` addresses are single-byte string-interior char-locators
  (bytes inside string literals reached via computed offsets), structure-boundary
  / zero-pad bytes, and documented bogus/stale-xref items — none are logical
  globals. Added in this pass:
  - Win32/OS API dispatch table @0x44b000-0x44b310 fully named as g_pApi*
    (~29 entries: RegOpenKeyExA/RegCloseKey/GetProcAddress/GetLastError/QPC/
    CloseHandle/SetEvent/ReleaseMutex/Sleep/WaitForSingleObject/ExitThread/
    Init/Enter/LeaveCriticalSection/HeapAlloc/Free/ReAlloc/InterlockedInc/Dec/
    TlsSetValue/VirtualAlloc/Free/WideCharToMultiByte/MultiByteToWideChar/
    LCMapStringW/wsprintfA/PostMessageA/MessageBoxA/htons/WSAGetLastError),
    console command-table slots (g_pfnCmdAction @0x44b308, g_pCmdNameAction
    @0x44b30c joining g_pCmdNameAi/Collision), g_pfnConfigNodeTypeString
    @0x44b7c4, and the CRT type_info vftable g_pVftableTypeInfo @0x44b928.
  - heap2 region-list globals named: g_pCrtHeap2RegionHead/Tail @0x452878/7c,
    g_pCrtHeap2RegionLink1/2 @0x452880/84, g_pCrtHeap2RegionEnd @0x45288c,
    g_pCrtHeap2RegionCur @0x454898 (roles HYPOTHESIS, sentinel self-loops
    verified in crtHeap2AllocRegion).
  - Scene: g_nSceneLoadCount @0x45e938 (.sen load counter, incremented in
    sceneLoadSen @0x4329df, reset to 0 by scenNameTableInit @0x431dc0),
    g_szSceneDir
    @0x45e950 CORRECTED (scenSetDir copies the directory PATH into this buffer;
    prior g_bSceneNameTableDirty mislabel fixed — it is a path string, not a
    boolean), g_nSceneSortBufUsed/Cap @0x45e834/38, g_afSceneConsts float[7]
    @0x450f88 + g_flSceneScaleOne @0x450f84 (constant-pool floats),
    g_nRoundPhase extended to int[8] @0x458390 (round-blink obj-id array,
    absorbs former 0x458394).
  - Camera obj-name strings: g_szObjIdCamDi @0x44e204 CORRECTED from
    g_nObjIdCameraAvoid (it is the "c_di" name string, not an id),
    g_szObjIdCamAc @0x44e20c ("c_ac"), g_szObjIdCamNc @0x44e214 ("c_nc") —
    objFindById args in cameraFollowUpdate/zoneAvoidWalls.
  - Menu player-record init constant blocks: g_anMenuRecInitA int[15] @0x45c030,
    g_anMenuRecInitB int[15] @0x45c16c, g_anMenuRecInitC int[27] @0x45c2c8
    (copied into the menu player records @0x456220 by menuInit).
  - Misc singletons (HYPOTHESIS plates): g_textMetricsA -> g_abTextMetricsA
    @0x459cf0 (TEXTMETRICA via GetTextMetricsA, byte[60]), g_sMenuSpriteGridEnd
    @0x45a92a + grid meta/tail shorts, g_nScenePathStrLen @0x4580e0 (MString.len
    of g_pScenePathString), g_nConsoleScrollLine @0x4580c8, g_nNetLayerReset
    @0x459cb0, CRT TLS access trio g_pCrtTlsBase/Fd/Ptd @0x4520f0/f4/f8,
    g_apCrtLoaderMsgs @0x454d20, g_nCrtLocaleCpInit @0x46260c,
    g_nSceneryDefClass @0x450f0c, g_nStreamOutOffs @0x44b7d4.
  - g_apNavPtRectList extended to void*[8] @0x45d4e0 (absorbs 0x45d4e4).
- Global-naming pass 5 (CLOSED): final DH-* / surrogate-named drive-down. Cleared
  every remaining auto-named (lp*/CodePage_/hHeap_/param_2_/cbMultiByte_/lpp*)
  global. Full details in the prior session §16; this pass added:
  - Object-id connect/globals (from single-xref evidence): g_nObjIdCameraAvoid
    @0x44e204 (camera clip test in cameraFollowUpdate @0x4020d0), g_nObjIdAiDest
    @0x44f4a0 (AI nav target playerUpdateAI @0x40b510), g_nObjIdL2CashZone
    @0x4500cc (cash-checkout zone in levelEventDirector_L2 @0x418000),
    g_nCameraUpdateTick @0x458948 (gameWorldUpdate @0x40b3d0 frame-throttle mask).
  - CRT stream/mode: g_anCrtStreamModeMask int[3] @0x44b810 (text/binary/shift
    mode masks OR'd + ANDed by streamFilebufOpen @0x43d7ef -> 0x800/0xa00/0xc00/
    0xe00 share flag).
  - CRT final globals typed+named: g_nCrtFileCount @0x462b20 (int, 18 xrefs),
    g_nCrtCodePage @0x4627bc (GetCPInfo) + g_nCrtWideCodePage @0x462634
    (WideCharToMultiByte/MultiByteToWideChar) + g_nCrtMbBytes @0x452788,
    g_hCrtHeap @0x462a0c (Heap size/realloc pool), g_pCrtReallocMem @0x462a04,
    g_dwCrtTlsIndex @0x452400 (TLS errno/locale), g_pCrtIoWriteBuf @0x45493c,
    g_pEnvValue @0x450038, g_pCrtCtypeSrc @0x44bdb0 (GetStringTypeA/LCMapStringA)
    + g_pwCrtCtypeSrc @0x44bdb4 (W variant), g_szProcIsProcFeatPresent @0x44b940
    (GetProcAddress name in crtIsProcessorFeature @0x440f94), lock critical-
    sections g_pCrtLockCsMain/Io/Env/Misc/Time @0x4527b0/b4/d4/e4/f4 (roles
    HYPOTHESIS; all init by crtInitCriticalSections), g_szCrtTzStdName
    @0x454c8c / g_szCrtTzDstName @0x454c90 char[64] + g_pwCrtTzStdNameW
    @0x4626dc / g_pwCrtTzDstNameW @0x462730 (crtTzsetCore @0x4480ad wide<->mb
    converts).
  - Movie-frame format strings (Hypothesis): g_szMovieFrameAcPrefix "ac%d"
    @0x44f488, g_szMovieFrameLrPrefix "lr%d" @0x44f490, g_szMovieFrameFbPrefix
    "fb%d" @0x44f498 (wsprintfA in movieFrameUpdate @0x40b000/0x40b03a).
  - g_apPlayers @0x456360 typed void*[8] (24 xrefs; aliases Player view at
    +0x150 of g_playerRecords).
  - LEFT as-is (correct as-is / still untyped by design): TIB fields at
    ffdff000 (per-thread struct — OS, not game), dwSize_00400000 IMAGE_DOS_HEADER
    (module base), wide-TZ fields are runtime-filled TZ-struct data. All prior
    DAT_*/lp*/param_2_ surrogate names gone.
- DAT_*/unnamed-global drive-down pass 4 (CLOSED, ~80 globals typed+named):
  - Per-level event-director globals COMPLETE for L0-L4: per level
    g_nL{n}EventActive/Tick/Step/EventObj, g_nL{n}MoveStep, g_pL{n}AttachMesh,
    g_apL{n}EventAnims void*[4] (L0 @0x459d74 block, L1 0x459df0, L2 0x459e44,
    L3 0x459ec0, L4 0x459f28; EventObj is the mascot scene-object id, NOT a
    script index). L0 extras: g_pL0SignObj @0x459d70, g_dL0SignAngle @0x459d68 +
    sub-mesh rotation accumulators (g_dL0SubAngleA/B/C, g_dL0SubOffsetA/B),
    g_pL0TrailAnim/Win @0x459db0/b4, g_flL0SpawnPos float[3] @0x459d90,
    g_nL0TrailTick @0x459de8. All xref-verified against levelEventDirector_L0
    @0x416fd0 / _L{n}_Init (0x416db0/4175e0/417dc0/4186c0/418f30).
  - Sound-mixer / DSOUND streaming globals @0x4622f8-0x4623e0 (~32 named).
    IMPORTANT CORRECTION: this 0x4623xx region is the DSOUND SOFTWARE MIXER,
    not CRT. Decompiled sndGetWriteRegion @0x438cd0 + sndClearMixBuffer @0x438bb0
    + dsoundInitMixer @0x39050 to map every global: g_pDsBufferPrimary/Alt
    (0x4623d0/a8 IDirectSoundBuffer), g_nDsBufferSel (0x462384), g_nDsPlaying
    (0x4623dc), g_nMixActive (0x4623e0), g_nDsBufferSize (0x4623d8),
    g_nDsFrameBytes/Count (0x4623d4/94), g_nDsWriteLimit (0x4623ac),
    g_nDsPlayCursor (0x462380), g_nDsWriteCursor/Base/Len (0x462370/68/6c),
    g_pDsLockPtr(+2) / g_nDsLockSize(+2) (0x462374/b0/378/b4), g_nDsLockResult
    (0x4623b8), g_nMixRegionStart/Size/Div1/Div2 (0x4623c0-c8-cc), g_pDSoundObj
    (0x46237c IDirectSound from DirectSoundCreate), g_nDsSampleRate/Bits/
    Channels1/BlockAlign1/BufferCount/FrameRegions (0x462388/8c/90/98/9c/a0),
    g_nMixScratchReady/Samples (0x462310/2f8), g_pMixScratch(+2) (0x462314/33c),
    g_pMixRateTable (0x462304), g_nMixRateStep (0x462308), g_pMixWaveTable
    (0x46230c), g_nMixMasterVol (0x46231c), g_nMixVoiceCap (0x462320),
    g_nMixFormat/16Bit (0x462338/318), g_nMixRateDivisor (0x46235c).
  - Input/app window cluster: g_pDirectInput @0x459cd0 (DirectInputCreateA),
    g_hAppInstance @0x459cdc (WinMain param), g_hMainWindow @0x459ce0 (HWND from
    CreateWindow; used by renderer + dsoundInitMixer SetCooperativeLevel).
  - Renderer: g_nClearColor @0x45892c (background clear color) + confirmed
    gxFlip @0x433340 / gxClearScreen @0x433350 / gxResetState @0x4333b0.
  - Zone-conn/scene cluster: g_pZoneConnHead/Tail @0x45e5e8/ec (zoneConnUnlink
    @0x42b890), g_pSceneCurrentObj @0x45e608 (sceneSetCurrentObj),     g_nSceneNodeCount
    @0x45e638, g_pSceneNodeHead @0x45e810 (next at +8), g_pSceneNameTable @0x45eac4
    (void*[16] emitter-name lookup table), g_pSceneNameBufPos @0x45e94c (write
    cursor into ONAM name region), g_pSceneNameTableSlot_1 @0x45eac8 (table [1]),
    g_szSceneDir @0x45e950 (scene directory path; filled by scenSetDir @0x432e60 —
    NOT a dirty flag, see pass 5b).
  - High-score/trivia + net-lobby strings (typed char[N]): g_szScoreKeyBug
    @0x450248 ("bug_" g_szScoreKeyPrefix target), g_szTriviaBanner @0x4508a4
    ("Frågesport"), g_szScoreKeyRef @0x4508b0 ("fshi"), g_szScoreCursor @0x4508b8,
    g_szNetCharLabel @0x450af8, g_szNetConnecting @0x450b04, g_szNetServerAddr
    @0x450b20, g_szNetWaitPlayers @0x450b38, g_szLobbyCancel @0x450b50,
    g_szNetWaitStart @0x450b58; g_nScoreTableTick @0x45d43c (per-frame counter in
    stateHighScoreTable @0x41dfd0).
  - Todo 6 menu-record templates CLOSED: g_abMenuRecord0-6 byte[64]
    @0x45c06c/c1a4/c1cc/c208/c248/c288/c334, zeroed templates MOVSD.REP-copied by
    menuInit @0x419c20, stateNetLobby @0x423f20, host-lobby setup @0x422a37.
  - Resolved not-named: 0x45a438 net player-slot array (element 0x45a440
    overlaps g_szPlayerName+0x38; indexed `[ECX*4+0x45a438]` vs g_nNetPlayerId —
    left as-is, plate-documented).
  - Validator quirks: `sz` accepts `char[N]` bracket type (scalar char rejected);
    `byte[N]` needs `ab` prefix; int flags need `n` (g_b* rejected for int);
    `double` uses `d` prefix.
- DRIVE-DOWN COMPLETE: 1178/1178 functions documented = 100% (maniac.exe).
  Final pass closed the last 16 scene stragglers (sceneNodeSetPosShorts
  @0x431850 [levelObjectsCartsCameraInit cart/camera pos writes, scaled ×0xb6
  into the node channel buffer at +0x14], sceneNodeGetMesh @0x431ae0 [returns
  mesh ptr at node+0x10 unless sentinel DAT_00450f38; callers aiNavNodeCtorScene
  /zoneConnCollectMeshes/sceneMeshBBox], sceneCreateTextureSurfaces @0x432260
  [gxCreateSurface per name-list entry; callers sceneLoadSen/sceneMeshFixup])
  plus the CRT naming tail — full MSVC CRT detail moved to
  [13-msvc-crt.md](13-msvc-crt.md) (region map, key globals, EH runtime,
  SBH/heap2 allocators, exit/lock/startup clusters, stdio/format/locale/
  strtold/tz/env naming passes, tool pitfalls).
- Global-typing pass (CLOSED): named-undefined backlog cleared — typed ~57
  globals (game-flow ints/flags/float, net menu/lobby ints, input COM ptrs,
  movie ConfigEnv/MString, 9 Swedish UI string literals char[N], music
  MusicSlot[16] struct, snd queue/bank arrays). Key layout corrections:
  sample bank = g_apSndBank @0x45ec94 void*[272] (banks 1-16 @[16..271] =
  0x45ecd4..0x45f0d4; bank 0 @[0..15] aliases sceneTextAnim glyphs
  0x45ec90..0x45ecd0, unused — old g_abSndSlotUsed mislabel deleted),
  g_pSndQueue void*[256] @0x461ee8 (count @0x4622e8), g_playerRecords typed
  SceneObject[8] (record stride 0x374 > struct 0x2FC; g_apPlayers @+0x150 is
  the aliased Player view, plate-documented). Strict-mode naming: use
  strict_mode="off" for value-structs (ConfigEnv/MString) and sz/char arrays.
- Software-mixer internals fully decoded + renamed (CLOSED): render pipeline
  (sndMixRenderRegion @0x438090, sndMixBuildVoiceChains @0x437fe0,
  sndMixAdvanceUnlock @0x438f20), per-voice volume/priority + 3D pan
  (sndVoicePriorityUpdate @0x4387a0, sndVolFromPos @0x4389a0, sndFixedMul
  @0x437ed0), fixed-point position stepping + loop wrap (sndVoiceAdvancePosition
  @0x438730, sndFixedStepAdd @0x437ea0), finish/reclaim (sndVoiceUpdateFinished
  @0x4386c0, sndVoiceReclaimFinished @0x438630), scratch->buffer conversion
  (sndMixScratchToBuffer @0x4382f0, sndMixScratchReset @0x4382b0), triangle
  wave-table mixing (sndBuildWaveTable @0x4381c0, sndVoiceRender @0x438b00,
  sndMixVoiceCore @0x4395b0, sndMixStep @0x4396a6, sndMixSamples @0x4396ea),
  lifecycle (sndStopAllVoices @0x438100, sndFreeMixBuffers @0x438140,
  sndVoiceIsActive @0x437e10, sndClearMixBuffer @0x438bb0), music-module
  callbacks (musicModulePosCheck @0x437b60, musicCbInitEmitter @0x437b40,
  musicCbRet1 @0x437bf0) + sample-load pipeline (sndSampleAlloc @0x437830,
  sndSample16To8 @0x4377f0, sndPcmUnsignedToSigned @0x4378a0, mathFixedRecip
  @0x4370a0) + config helpers (configEnvFindValue @0x4365a0,
  configAppendStringNode @0x436b90) — 36 functions — [09-sound.md](09-sound.md),
  [06-config.md](06-config.md).
- On-foot player controller playerUpdateOnFoot @0x427730 (turret-aim easing,
  movement vector from turret inputs, per-level hazard zones L2/L3/L4),
  level-editor mouse input (editorMouseMove @0x426340, editorMouseDown
  @0x426230, lineRecordFindNearest @0x426190, input mode DAT_00458344==2),
  and meshDrawPoly backface-clip helpers (meshDrawTriClip @0x42d070 /
  meshDrawQuadClip @0x42daf0), configStreamObjDtor2 @0x436300 — 7 functions
  renamed — [07-gameplay.md](07-gameplay.md), [12-input.md](12-input.md).
- Scene render system (sceneSystemInit/Close @0x42ed40/0x42f180,
  sceneFreeAllNodes @0x42f150, sceneRender @0x42f1c0, sceneCameraBasisCalc
  @0x42f460, mathSinTreeBuild @0x42efb0), scene-object accessors
  (sceneObjGetPos @0x4317e0, sceneObjSetSubOrient @0x431110,
  sceneSetCurrentObj @0x430d90, sceneFindByName @0x431fd0), config tree
  builders (configNodeAppendBlock @0x436fa0, configBlockNodeNew @0x436d50,
  configNodeFree @0x436a60, configEnvFreeChildren @0x4368a0,
  configEnvGetValueByIndex @0x436550) — 15 functions renamed —
  [04-renderer.md](04-renderer.md), [06-config.md](06-config.md).
- Scene-object anim command stream (sceneObjectAnimStep @0x434540 +
  sceneObjectAnimStepInterp @0x4347c0), playerAnimOrientFromDir @0x4336b0 +
  walkAnimTableEntryCalc @0x433980 + g_awWalkAnimTable @0x458138, GX driver
  registry helpers (gxRegReadBinary/Dword/Float @0x434c10/0x434d40/0x434e00,
  gxRegSplitKeyValue @0x434cd0, gxErrorBox @0x434f10, gxFatalErrorExit
  @0x434ef0), sceneTextAnim manager (Reset/Add/Update/Close @0x434a50-0x434bf0,
  5 globals @0x45ebd0-0x45ecd0), and mString cluster
  (mStringCtorWithCapacity @0x435170, mStringTrim @0x435310,
  mStringEqualsMString @0x435510, mStringAssignFromStr @0x435610,
  configStreamObjDtor @0x435b60) — 19 functions renamed — [04-renderer.md](04-renderer.md),
  [06-config.md](06-config.md), [07-gameplay.md](07-gameplay.md).
- Network menu/lobby states fully named + decoded (host setup, client setup,
  connecting, client lobby, net exit; msg id 8/0x9/0xa/0xb lobby protocol,
  g_szPlayerName/g_szServerAddress/g_szHostPlayerName buffers,
  g_playerRecords built from the 0x45c020 menu-record template in stateNetLobby)
  — [05-networking.md](05-networking.md) §7b.
- SceneObject struct completed in Ghidra (764 B, typed SceneObject[8] at
  g_playerRecords 0x456210): WorldNode sub-objects pWalkNode +0x224 / pPosNode
  +0x264 / pCartNode +0x2a4, cart system (+0x174 nHasCart, +0x17c nHeldItemId,
  +0x180 nMode, +0x184 nCartIdx, +0x270 flCartTurnConst), queued-action anim
  state machine (+0x2e8 nAnimState actionCmd 1-7, +0x2f0 nAnimBusy, +0x2f4
  reload, +0x2f8 timer; 0xf=wait), +0x2d8 nChannelsDirty, +0x2dc nCtrlType
  (2=AI), +0x1f0 flWalkTurnConst — [07-gameplay.md](07-gameplay.md).
- CLASS-0x1f bonus-item pickup VERIFIED: levelEventDirector_L0 @0x416fd0 s9
  spawns EventObject id 0x1f with +0x14 = BURGER scene-object; playerAiGrabItem
  @0x40ea20 takes +0x14 as the held mesh (class 8, y=300), removes the
  EventObject; delivery nAnimState 7, cleanup nAnimState 0xc — [07-gameplay.md](07-gameplay.md).
- AI movement-mesh / zone-graph region 0x428840-0x42a7xx fully renamed +
  decoded: the three per-subobject channel-sync passes (syncPosNodeChannels
  ToMesh / syncWalkNodeChannelsToMesh / syncCartNodeChannelsToMeshes), the
  zone-wall conn merge/link helpers (zoneConnLink, zoneConnMergeDupesInMesh,
  zoneConnMergeDupesCrossMesh), wall-plane/side/circle-hit ray helpers
  (zoneWallCalcPlane, zoneWallPointSide, zoneWallCircleHit), and the
  detail-LOD grid cluster (sceneDetailGridCtor + 6 helpers) — [07-gameplay.md](07-gameplay.md) §9.
- Console command table (26 handlers renamed + mis-name fixes) — [06-config.md](06-config.md).
- Config env system (XOR ^0x55 load/save, parser, serializer, string class,
  movieDb, config-env init chain) — [06-config.md](06-config.md).
- GS_* UDP protocol + net/mnet socket layer + gameplay message layer —
  [05-networking.md](05-networking.md).
- Player + AiController structs, cart system, unified player/scene-object model,
  player-record array, quest subsystem, unit/object subsystem — [07-gameplay.md](07-gameplay.md) §9.
- Game-state flow, deferred "request" actions, high-score states, HUD —
  [03-gameflow.md](03-gameflow.md).
- Sound subsystem (DSOUND mixer, sample banks, 3D emitters, MCI CD music,
  movie recorder) — [09-sound.md](09-sound.md).
- Level-event directors + EventObject/.eo + shopping-zone navigation —
  [07-gameplay.md](07-gameplay.md) §9 + [10-fileformats.md](10-fileformats.md) §12.
- Ordinal imports (WSOCK32 13 + DSOUND 1) — [01-binary.md](01-binary.md) §1/§2.
- .ai navpoints + .tpg layout — [10-fileformats.md](10-fileformats.md) §12.
- Scene-graph / channel / node subsystem + math helpers + font pool —
  [04-renderer.md](04-renderer.md).
- ANM/.tga/.sen helper clusters, file helpers — [10-fileformats.md](10-fileformats.md),
  [06-config.md](06-config.md).
- AI steering + AiController state machine + moveState + camera follow —
  [07-gameplay.md](07-gameplay.md) §9.
- Input/DirectInput subsystem — [12-input.md](12-input.md).
