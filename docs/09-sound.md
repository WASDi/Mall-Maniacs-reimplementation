# Mall Maniacs (maniac.exe) — 09. Sound subsystem

[Back to README](README.md)

## 11. Sound subsystem (DSOUND + software mixer) [VERIFIED]
- Init `sndInitSystem` @0x00437a30 [renamed]: `sndResetBanks` (clears sample
  pointers banks 1-16), `dsoundInitMixer` @0x00439050 (DirectSoundCreate via
  Ordinal_1 thunk + SetCooperativeLevel + 6 secondary mix buffers, 8-bit mono
  @ 22050/44100 Hz), `sndCreateMixBuffer` @0x00437ef0 (software mix buffer
  0x10200 bytes + rate table), `sndInitVoices` @0x00438160 (0x100 voice
  entries of 0x2e bytes @0x45f0e0). Called with (2,4,10) from menuInit
  @0x419c20 and scene setup @0x0040a4d0. g_nSoundInit @0x4511cc: -1=uninit,
  1=init; g_bSoundMute @0x45f0d8=1 on DSOUND failure. Shutdown `sndShutdown`
  @0x00437cb0 -> `dsoundRelease` @0x00438ff0 (releases DSound buffers).
- Sample bank: g_apSndBank @0x45ec94 void*[272] (banks 1-16 @[16..271] =
  0x45ecd4..0x45f0d4, 16 samples each, indexed bank*16+smp; bank 0 @[0..15]
  aliases the sceneTextAnim glyph array + active flag 0x45ec90..0x45ecd0 and
  is never used for samples — the code's bank base is 0x45ec94). The old
  mislabel g_abSndSlotUsed @0x45ecd4 ("used-flags") was deleted: that region
  IS the banks-1-16 pointer table; the "free group" auto-select scans the
  pointers themselves (free = all 16 null). `sndLoadWav` @0x00437420 parses
  RIFF/WAVE (PCM mono
  8/16-bit; "rb" mode, fileOpen/Read/Seek/Close wrappers), `sndRegisterSample`
  @0x00437850 stores in bank, `sndGetSample` @0x004377d0(bank,idx) lookup.
  `sndLoadBankFromDir` @0x00437170 enumerates digit-prefixed .wav files and
  loads them by leading index; `sndFreeBank` @0x00437120 frees a bank.
  - Enumerate logic: FindFirstFileA wrapper FUN_0043f5b4 over "sound\*",
    accept if not hidden/system (attr & 0x16 == 0) and first char is
    '0'..'9'; parse leading decimal digits -> sfx index; require extension
    equal (case-insensitive) to ".wav" @0x4511b4; build path
    "sound\<name>" (inserts '\' if dir lacks trailing sep); then
    sndLoadWav(path, bank, idx). Any load error aborts the whole bank.
- Playback: `sndPlaySfx` @0x00437cf0 [renamed] queues up to 16 requests
  (g_pSndQueue @0x461ee8, count g_nSndQueueCount @0x4622e8); entry dwords:
  [0] sample, [3] volume, [4] pitch (cap 160000), [5] loop count
  (0x7fffffff if loop flag 0x200), [6] flags, [8] param, [9] rate-divisor.
  Per-frame drain `sndMixTick` @0x00437c50 [renamed] from gameFrameUpdate
  @0x41a8c0 + gameRunFrame @0x40ad80. Observed gameplay sfx: bank 1, ids
  8 (goal), 0x16 (pickup = 22_Throw-Hit), 0x14 (throw bounce = 20_Bounce),
  0x1c (doorbell = 28_Dorrbell), 0x21/0x22 (countdown = 33_Red1/34_Green1),
  0x23 (Orient gong = 35_Gong2).
- Sound dirs (on-disk evidence, /home/wasd/MallManiacsUnmodified/):
  sound\ = gameplay bank (index == leading filename number), loaded by
  roundStartInit @0x40a9a6 via sndLoadBankFromDir(1,"sound\"); sound\menu\ =
  menu bank, loaded by menuInit @0x419df8 via sndLoadBankFromDir(1,
  "sound\menu\"). Both use bank 1 — menu phase and gameplay phase are
  disjoint, so no clash (round load calls sndFreeBank(1) first).

  ### Gameplay sfx table (bank 1, index == NN)
  | idx | file | idx | file |
  |----|----|----|----|
  | 1 | 01_Vagnkoll3.wav (cart bump) | 2 | 02_Vagnkoll2.wav |
  | 3 | 03_Vagnkoll1.wav | 4 | 04_Gha.wav |
  | 5 | 05_wind2.wav | 6 | 06_Computerloop.wav |
  | 7 | 07_Blast.wav | 8 | 08_goal.wav (goal) |
  | 9 | 09_Flag.wav | 10 | 10_Steg2.wav (steps) |
  | 11 | 11_Vagnrull1.wav (cart roll) | 12 | 12_Vagnrull2.wav |
  | 13 | 13_Vagnrull3.wav | (14 missing) | 15_Mini.wav |
  | 15 | 15_Mini.wav | 16 | 16_Woods.wav (L1 ambience) |
  | 17 | 17_Asia.wav (L2 ambience) | 18 | 18_Aqua.wav (L3 ambience) |
  | 19 | 19_Future.wav (L4 ambience) | 20 | 20_Bounce.wav (throw bounce) |
  | 21 | 21_Throw.wav | 22 | 22_Throw-Hit.wav (item pickup) |
  | 23 | 23_Miss1.wav | 24 | 24_Miss2.wav |
  | 25 | 25_Porl.wav | 26 | 26_Plask.wav |
  | 27 | 27_Fors1.wav | 28 | 28_Dorrbell.wav (doorbell) |
  | (29 missing) | 30_yummie.wav | 30 | 30_yummie.wav |
  | 31 | 31_Birdsloop.wav | 32 | 32_Birdsloop2.wav |
  | 33 | 33_Red1.wav (countdown red) | 34 | 34_Green1.wav (countdown green) |
  | 35 | 35_Gong2.wav (Orient gong) | | |
  NOTE: no 14/29 on disk — sndGetSample returns 0 for those indices and
  sndPlaySfx silently drops the request.

  ### Menu sfx table (bank 1, loaded at boot only)
  | idx | file |
  |----|----|
  | 1 | menu\01_Buttons.wav (UI click) |
  | 2 | menu\02_Boing1.wav |
  | 3 | menu\03_Miss2.wav |
  | 4 | menu\04_kokko.wav |
  | 5 | menu\05_FALSCHE.wav (wrong!) |
  Menu sfx only audible pre-round; roundStartInit overwrites bank 1.
- Software mixer internals [VERIFIED, closed]:
  - `sndInitSystem` @0x437a30: sndResetBanks, dsoundInitMixer (DirectSoundCreate
    + 6 secondary mix buffers; fail -> g_bSoundMute=1), sndCreateMixBuffer
    (0x10200 B software mix buffer + rate table), sndInitVoices(0x45f0e0).
  - `sndInitVoices` @0x438160: seeds the 0x100-voice free-list — 0x2e-byte slots
    at 0x45f0e8, queue pointers written into g_pSndQueue/g_nSndQueueCount
    (0x461ee8/0x4622e8, count starts 0x100).
  - `sndPlaySfx` @0x437cf0 [full decode]: pops a free slot from the queue
    (g_nSndQueueCount--); entry layout (10 dwords): [0]=sample ptr,
    [3]=volume (param_4&0xffff), [4]=pitch (sample freq*(param_5>>4)>>0xc if
    flag 0x800, else param_5; cap 160000), [5]=0x7fffffff if flag 0x200 (loop),
    [6]=flags, [7]=handle counter (+0x10000 per alloc), [8]=param_1 (mixer
    voice id), [9]=&DAT_0046235c rate-divisor if flag 0x400 && param_1==0.
  - `musicMixCb` @0x437c00 [inner mixer logic CLOSED]: voice-finished callback
    (5th arg to musicModuleInit). Scans all 0x100 voice slots at 0x45f0f8
    (0x2e stride) for slots whose +0x10 field == finished voice id (param_1)
    AND active flag (+0x00): clears flag, pushes slot ptr back onto free queue
    (g_nSndQueueCount++), clears +0x10. Returns 1.
  - `sndFreeVoiceByHandle` @0x437e40 [renamed]: release a specific voice —
    idx = handle&0xffff -> slot at 0x45f0f8+idx*0x2e; if slot+0x0c == handle &&
    active, deactivate + return to queue.
  - `sndMixTick` @0x437c50: per-frame drain — repeatedly locks the next free
    write region of the DSound streaming buffer (`sndGetWriteRegion` @0x438cd0,
    module vtable DAT_004623d0/a8; DSERR_BUFFERLOST -0x7787ff6a -> restore),
    then `sndMixBuildVoiceChains` @0x437fe0 (chain active voices per slot via
    +0x2c), `sndMixRenderRegion` @0x438090 (render), `sndMixAdvanceUnlock`
    @0x438f20 (unlock/advance cursor) until no region.
  - Render pipeline [all renamed, CLOSED] `sndMixRenderRegion` @0x438090:
    1. set master volume DAT_0046231c from voice base +0xc83;
    2. `sndVoicePriorityUpdate` @0x4387a0 — per voice: 3D pos -> L/R volume via
       `sndVolFromPos` @0x4389a0 (±0x7c18 hear range, pan mode DAT_004622fc,
       factor 0x640000/(dist+100)), combine master volume + env profile, store
       8-bit L/R at +0x28/+0x29; priority score per voice; if active count >
       cap DAT_00462320, repeatedly stop the lowest-priority voice.
    3. `sndMixScratchReset` @0x4382b0 — lazy zero-fill scratch DAT_00462314
       (2*DAT_004622f8 shorts), set DAT_00462310=1.
    4. `sndVoiceUpdateFinished` @0x4386c0 — stop voices whose position >= sample
       end (calls sndVoiceRender/free), mark scratch dirty.
    5. `sndVoiceAdvancePosition` @0x438730 — fixed-point step via
       `sndFixedStepAdd` @0x437ea0 (int/frac parts at voice+0x04/+0x08, rate
       DAT_00462308); loop wrap at sample+0x0c, end-of-sample at sample+0x04.
    6. `sndVoiceReclaimFinished` @0x438630 — unlink finished voices (+0x10=0)
       back onto the free stack (voice base +0xb82, top count +0xc82).
    7. `sndMixScratchToBuffer` @0x4382f0 x2 (interleaved L/R halves) — 8-bit
       (^0x80) or 16-bit (avg pairs ^0x8000) conversion per DAT_00462338/0x4318;
       silence (0x8000) when scratch uninitialized.
  - Per-voice sample rendering [CLOSED]: `sndVoiceRender` @0x438b00 selects the
    wave-table base (DAT_0046230c + |vol|*0x200, ±half for sign) from +0x28
    vol pair -> `sndMixVoiceCore` @0x4395b0 (chunk count from remaining/rate) ->
    `sndMixStep` @0x4396a6 (fold produced bytes into position/frac) ->
    `sndMixSamples` @0x4396ea (hottest loop: source byte -> triangle table
    index, accumulate into L/R scratch, source += rate step DAT_004511e4/e8).
    Table built by `sndBuildWaveTable` @0x4381c0 (0x10000-byte triangle at
    DAT_0046230c: rising then falling ramp, step 0x20000/DAT_00462320), called
    from sndCreateMixBuffer.
  - Helpers: `sndFixedMul` @0x437ed0 = high-32 of 64-bit product (volume
    scaling); `sndVoiceIsActive` @0x437e10 (slot 0x45f0f8+idx*0x2e, +0x0c ==
    handle, used by sndEmitterFree/sndEmitterUpdateFree); `sndStopAllVoices`
    @0x438100 (clear all 0x100 slots + owner +0x24 flags, from gameFrameRender);
    `sndFreeMixBuffers` @0x438140 (free scratch DAT_0046233c + rate table
    DAT_00462304, from sndShutdown); `sndClearMixBuffer` @0x438bb0 (stop + lock
    + zero-fill + unlock the streaming buffer to silence, from sndInitSystem).
  - `sndBuildEnvProfile` @0x437a00 [renamed]: computes per-block amplitude
    profile (64 blocks; 8-bit or 16-bit sample diff), derives normalization
    shift into +0x17 (+0x16) of the sample header — used by the mixer for
    volume normalization.
- Music/streaming: `musicModuleInit` @0x00437b10 [renamed] calls the module
  entry `musicSlotAlloc` @0x431c30 [created] (allocates 16 slots of 0x1c at
  g_apMusicSlots @0x45e650, MusicSlot[16] — +0 nInUse, +4 pEmitterHead, +8
  pInitCb, +0xc/+0x10/+0x18 cb args, +0x14 pPerFrameCb called by sceneRender)
  with 5 callbacks + sample rate 15000:
  `musicCbInitEmitter` @0x437b40 [created] (clear emitter +0x24 playing flag),
  `musicCbRet1` @0x437bf0 [created] (stub, x2), `musicModulePosCheck` @0x437b60
  [renamed] (positional trigger: distance check against cached radius; re-cache
  node pos into +0x28/+0x2c/+0x30 when out of range; registered via data ptr at
  0x437b1f), `musicMixCb` @0x437c00 [created]. Handle in g_pMusicModule @0x4622f0
  and g_nMusicModuleHandle @0x4580bc.
- 3D positional SFX: `sndPlaySfx3D` @0x0042bcd0 [renamed] links a 1c-byte
  emitter node (list g_pSndEmitterHead/Tail @0x45e5f0/f4) then registers a
  music-module emitter via `musicEmitterAlloc` @0x00431b00 [renamed]
  (0xa8-byte struct, free list @0x45e8c0, slot table @0x45e654) and queues
  the sample through sndPlaySfx. Flags: bit 4 dedupe-by-distance, bit 8
  replace-old, bit 0x10 loop, bit 0x20 + rand pitch. Called from object/AI
  update paths (FUN_00405680/5e10/c800/f950/175e0/186c0/18f30) + net
  (FUN_00426fd0/27730).

Music = CD audio via MCI [VERIFIED]: there are no music files on disk; the
game plays audio tracks from the CD.
- `mciPlayCdaudio` @0x00416cc0 [renamed]: MCI_OPEN "cdaudio" (s_cdaudio
  @0x50084) -> g_nMciDeviceId @0x459d30; MCI_SET ms format; MCI_PLAY with
  dwCallback=hwnd (DAT_00459cd0), dwFrom=track+1, dwTo=track; sets
  g_bMciActive @0x459d34. `mciStopCdaudio` @0x00416c80 [renamed] sends
  MCI_STOP 0x808 + MCI_CLOSE 0x804.
- Track stored in g_bMusicTrack @0x4580c0 (byte): menu=7, intro=8; synced to
  network clients via netServerSendSubCmd. Callers: scene setup @0x0040a4d0,
  FUN_004250f0 (return-to-menu), WindowProc (key 0x3b9 replay), introUpdate
  @0x41ae50 (tracks 8 then 7), menuInit.
- WinMM resolved dynamically: `winmmInitTimerRes` @0x0040dfd0 =
  timeBeginPeriod(1); `winmmRestoreTimerRes` @0x0040e030 = timeEndPeriod(1);
  `getGameTime` @0x40dfe0 uses timeGetTime via CALL [0x44b1e4].

Dynamic import slot table @0x44b100-0x44b200 [VERIFIED]: calls go through
`CALL [0x44b1xx]` slots; descriptors (ordinal+name) near 0x44d480
("timeBeginPeriod"=0x94, "timeGetTime"=0x98, "timeEndPeriod"=0x95,
"mciSendCommandA"=0x33, "WINMM.dll", "GetProcAddress", "LoadLibraryA",
"FreeLibrary", "LocalFree", "FormatMessageA"). Known slots: 0x44b1b4=
CreateWindowExA, 0x44b1e4=timeGetTime, 0x44b1e8=timeBeginPeriod,
0x44b1ec=mciSendCommandA, 0x44b1f0=timeEndPeriod, 0x44b1f8/1fc=WSOCK32
(socket/bind), 0x44b1dc=GetProcAddress.

Demo recorder ("movie" console command) [VERIFIED]: NOT video — records/
replays per-player transform keys into the config env, for bug repro.
- `movieCmd` @0x00407a60 [renamed]: subcommands rec/play <name> (set
  g_movieName, toggle g_nMovieRecord @0x455e8c / g_nMoviePlay @0x455e90),
  save/load/clear. Data lives in config env g_pMovieDb @0x455e68 (typed
  ConfigEnv; name field g_movieName @0x455e7c = its +0x14 MString {char*
  pPsz; int nLen}).
- `movieFrameUpdate` @0x0040af80 [renamed], called from gameWorldUpdate
  @0x40b3d0 every frame. Frame node key = "%s[%d]" (g_movieName @0x455e7c,
  g_nMovieFrame @0x455e88); per-player keys "ac%d" (anim state +0x198, 0..7),
  "lr%d"/"fb%d" (+0x190/+0x194) recorded only when != 0.0f sentinel
  (_DAT_0044b244). NOTE: 'l' records +0x194 but plays back to +0x190 (and
  'f' vice versa) — X/Z swapped between record and playback (latent game bug).
- Playback cursor g_pMovieFrameNode @0x455e84; node API: FUN_004365a0 (lookup
  node by key), FUN_00436d50 (advance/create node), FUN_00436eb0 (set value),
  FUN_004369f0 (get float), FUN_004368a0 (clear env).
The console command table (26 cmds, incl. "movie") is documented in
[06-config.md](06-config.md) — table @0x44b308, handlers all renamed &Cmd.
