# Mall Maniacs (maniac.exe) — 03. Game state machine / flow

[Back to README](README.md)

## 5. Game state machine (DAT_0045a6f8 dispatch) [VERIFIED]
State fns called with (type,a,b): type 0 = frame update, type 1 = key event.
`dispatchKeyEvent` @0x0041ade0 [renamed] forwards (1,key,type) to current state.
Transitions write DAT_0045a6f8 (40+ writers). Global renamed `g_pStateFunc`
@0x0045a6f8 (typed `PStateFunc*`).

Single-player flow:
- `introUpdate` @0x0041ae50 [renamed] — intro timeline (_DAT_0045d444 time),
  shows intro logos in sequence (threshold constants 0x44b660-0x44b684), any key
  or ENTER (key 7,type 2) skips -> `menuUpdate` @0x0041b0b0 [renamed].
- `menuUpdate` @0x0041b0b0 — main menu, 5 options (labels @0x450804-0x450828:
  Spela/Nätverk/Alternativ/Rekord/Avsluta). Selection DAT_0045d448 (0-4, wraps);
  ENTER (key 6) writes g_pStateFunc = transition[idx]. Mapping [VERIFIED from
  decompile]: idx0 Spela -> `stateGameTypeSelect` @0x41c010, idx1 Nätverk ->
  `stateNetworkMenu` @0x420190, idx2 Alternativ -> `gotoOptions` @0x41d300
  (Options screen), idx3 Rekord -> `stateHighScoreTable` @0x41dfd0, idx4
  Avsluta -> `gameUpdate` @0x4200b0 (quit-confirm "Ja" -> FUN_00420160 teardown).
  ESC (key 7) also -> gameUpdate @0x4200b0.
- `stateGameTypeSelect` @0x0041c010 [renamed] — game-type select (menu "Spela"):
  4 modes Varujakten @0x4504ac, Matkrig @0x4504a4, Frögesporten @0x4504b8,
  Vagnrace @0x450498. Left/right (keys 2/3) cycle g_nGameTypeSel @0x45d454
  (0-3 wrap); ENTER (key 6) -> fade + g_pStateFunc = modeInit*[sel]; ESC ->
  menuUpdate. Keys 0/1 no-op (row value pointers NULL).
- Mode-init stubs (set mode config then -> `stateCharacterSelect` @0x41efa0):
  `modeInitVarujakten` @0x41bf60 (g_nPlayerCount=0 auto, g_nGameMode=2),
  `modeInitMatkrig` @0x41bf90 (0 auto, mode 3), `modeInitFrogesport` @0x41bfc0
  (player count 1 single-player, mode 1), `modeInitVagnrace` @0x41bfe0
  (0 auto, mode 4). Globals: g_nGameMode @0x458120 (1=Frögesporten,
  2=Varujakten, 3=Matkrig, 4=Vagnrace), g_nPlayerCount @0x458108 (0 = derive
  from table DAT_0044b610[difficulty+level*3] in playerSetupCharacters; note
  stateOptions writes 8 here as a no-op tail), g_nLocalPlayerIdx @0x458104,
  g_nLevelIdx @0x458100.
- `gotoOptions` @0x0041d300 [renamed] — stub -> `stateOptions` @0x41c6a0
  [renamed]; copies [0x4580c4] into g_nRendererMode @0x45a390.
- `stateOptions` @0x0041c6a0 [renamed] — Options screen ("Alternativ"): rows
  "Svårighetsgrad" (Difficulty, value DAT_004580fc, name array @0x450230) and
  "Grafik" (Graphics, g_nRendererMode); left/right adjust focused row
  (DAT_0045d45c -> g_nOptionsRow @0x45d45c); renderer desc 1="3DFX Voodoo" @0x450858, 2="Mjukvara"
  @0x450864, else "En gammal matrix skrivare?" @0x450870; ESC -> FUN_0041c630.
- `stateCharacterSelect` @0x0041efa0 [renamed] (chars + stats Snabbhet/Styrka/
  Smidighet) -> `stateLevelSelect` @0x0041b900 [renamed] (level 0-4 within
  mode; ENTER -> stateLevelInit0..4, ESC -> stateCharacterSelect) ->
  `stateLevelInit0..4` @0x41b810/0x41b840/0x41b870/0x41b8a0/0x41b8d0
  [renamed] (each sets g_nLevelIdx @0x458100 = N, calls `playerSetupCharacters`
  @0x0041b6e0 + `unloadGameWorld` @0x41a670, then commandDispatch of fixed
  strings "run 0".."run 4" @0x450830-0x450850) -> `runCmd` starts gameplay.
  Network path: `startServer` @0x420770 [renamed] ("Startar server": netStartServer,
  copies DAT_0045a520 -> DAT_0045a418) -> `stateHostLobby` @0x422ec0 [renamed]
  (host lobby: roster DAT_0045a458 names / DAT_0045a438 selections / DAT_0045a418
  carts / DAT_0045a500 count, mode from DAT_00450240, net player sync every 4 ticks)
  or join lobby (client), then `stateStartGame` @0x00422840 [renamed] copies player
  data from DAT_0045a438 (char) / DAT_0045a458 (name) into the player array
  (charIdx +0x000, cart +0x1b8, name string), unloadGameWorld, commandDispatch
  "run/%d" (@0x450b2c format) with level from arg.
- Deferred action mechanism (`request` console cmd) [VERIFIED, renamed]:
  - `requestCmd` @0x0041a730 [renamed] — handler for "request <sub> <n...>":
    parses "%s %d %d %d %d" (@0x44e7f4), maps subcommand -> fn ptr stored in
    `DAT_0045a710` (deferred-action slot):
      "fshiscore"  -> `gameOverLoadHighScores` @0x41dcb0
      "vahiscore"  -> `stateVahiScore` @0x41de40 [created]
      "play_level" -> `statePlayLevel` @0x41b7d0 [created]
      "endscene"   -> `endScene` @0x424ef0 [created]
  - `menuInit` @0x419c20 runs the deferred action on menu (re)entry: if
    DAT_0045022c==0 (not intro) and not startup, g_pStateFunc = menuUpdate, but
    if DAT_0045a710 != 0 -> g_pStateFunc = DAT_0045a710 (see @0x419e30).
    unloadGameWorld @0x41a684 clears DAT_0045a710. gameFrameUpdate re-calls
    menuInit whenever DAT_0045a658==0 (i.e. after unloadGameWorld).
  - `statePlayLevel` @0x41b7d0: clamps DAT_0045a700 (result level) into
    g_nLevelSel (min(level,4)) and DAT_0045d450 (level+4, max 9), then ->
    stateCharacterSelect (pre-selects char idx DAT_0045d480 = DAT_0045d450).
  - `stateVahiScore` @0x41de40: Varujakten analog of gameOverLoadHighScores —
    copies DAT_0045a700/704/708/70c into score globals (DAT_0045d474/470/468/
    46c), sets DAT_0045023c=0x450a58.
  - `endScene` @0x424ef0 [created]: alloc 0x1080, mciPlayCdaudio(hwnd@0x459cd0,
    track 5), end-scene level DAT_00450244 = clamp(DAT_0045a700,0,9), loads
    menu\end\endscene.sen (@0x4504e8), builds 9 end-scene image handles
    (@0x45a71c..0x45a744, from %d-prefixed names + "end" @0x450b8c), camera aim
    "WWWWENDCAM" @0x450bcc, then g_pStateFunc = `stateEndSceneShow` @0x425190.
  - `stateEndSceneShow` @0x425190 [renamed]: per-frame end-scene credits —
    moves camera via DAT_0045a508, animates end images (0x45a71c list, FUN_
    00434090 spin + fade DAT_0045a3f8), shows level graphics in timeline
    (DAT_0045a748..76c), ESC (key 7) -> `stateEndSceneReturnMenu` @0x4250f0.
- `runCmd` @0x004084c0 [renamed] — handler for "run <level>" (help "New game
  (Syntax: run <level>)" @0x44e3a0): errors if DAT_004580f8!=0 ("A game is
  already running, kill it first." @0x44ee98); parses "%d" (@0x44ee94) ->
  g_nLevelIdx=level; calls `FUN_0040a4d0` (scene setup, sole caller), sets
  DAT_0045810c=1, DAT_004580f8=1 (enter gameplay loop), g_nScrollText=0.
- `killCmd` @0x00407870 [renamed] — handler for "kill": if DAT_004580f8!=0 ->
  `FUN_0040aa10` (full game-world teardown: netExit, mciStopCdaudio, sfx/emitters
  freed, per-level FUN_00416f70/17860/17fa0/189d0/19240, player objects freed),
  DAT_004580f8=0, DAT_0045810c=1 (returns to menu/state loop).
- Results-screen key handling: `gameKeyHandler` @0x0040db80 [renamed] (Enter=6 /
  Esc=7 with param_2==2 while g_nResultsScreen!=0):
  - modes 1/4 (Frögesporten/Vagnrace): Enter -> commandDispatch "kill" only.
  - modes 2/3 (Varujakten/Matkrig): if single-player (netIsActive()==0) and
    g_nWinnerIdx==g_nLocalPlayerIdx: level<4 -> "request play_level %d 0 0 0"
    (@0x44f588) else -> "request endscene %d" (@0x44f5a4), then "kill".
    Esc: same + "kill". (Non-winner/single-player: just "kill".)
  - Keys 0/1/2/3 = movement (clear action bit + set dir @player+400/+0x194),
    key 4 -> commandDispatch "action" (@0x44f5b8). Action-timer gate
    player+0x1ac. "J"/"Y"/"j"/"y" -> "kill" when g_bQuitPrompt.
- Names @0x450404-0x450498: characters = Ungkarlen, Marknadsanalytikern,
  Grevinnan, Hemliga agenten, Fotomodellen, Piloten; MAPS = ICA Raketen,
  ICA Sjöhästen, ICA Draken, ICA Eken, ICA Småköp (then "Vagnrace" mode names).

Results / high-score flow (game over) [VERIFIED, renamed]:
- `gameOverLoadHighScores` @0x0041dcb0 [renamed] — copies results record
  [0x0045a700..0x0045a70c] (g_nGameOverRank @0x45a704 = table row achieved,
  g_nGameOverLevel @0x45a70c) into working globals (g_nScoreRow @0x45d470,
  g_nResultsLevel @0x45d46c, DAT_0045d468/DAT_0045d474), sets
  g_szScoreKeyPrefix @0x0045023c = "fshi". If rank<4 shifts lower slots down via
  commandDispatch on "get/set fshi{level}d{name|time|face|diff}{slot}". Clears
  g_szScoreName @0x45d3b8, then g_pStateFunc = `stateHighScoreEntry`.
- `stateHighScoreEntry` @0x0041d320 [renamed] — name entry + row display
  (SHARED by fshi AND vahi tables — the active table is selected by
  g_szScoreKeyPrefix @0x0045023c, set to "fshi" by gameOverLoadHighScores or
  "vahi" @0x450a58 by stateVahiScore):
  (param1=0) draws 5 rows from config ("get %s%dname%d"/"%s%dtime%d" via
  commandDispatch), highlights g_nScoreRow, blinks cursor (g_nFrameCounter
  @0x45d40c); accepts a-z A-Z 0-9 . space _ + aaooe (max 12 chars) into
  g_szScoreName; (param1=1,key6,type2) commits "set fshi{level}d{name,time,
  face,diff}{slot}", fades, g_pStateFunc = `stateHighScoreTable`.
- `stateHighScoreTable` @0x0041dfd0 [renamed] — browse/display screen for the
  fshi/vahi tables (SHARED, prefix via g_szScoreKeyPrefix): (frame) draws 2
  rows per level via config "get %s%d{name,time,face,diff}%d" (time formatted
  "%02d:%02d:%02d"), face polygon + difficulty bar + name; header draws
  mode-name strings (Frögesport @0x4508a4, Vagnrace @0x450498); level selector
  bar at bottom highlighted by g_nResultsLevel.
  Keys: 0/1 up/down cycle g_nResultsLevel (cap min(g_nLevelCount,4)); 2/3
  left/right adjust g_nRecordsRow @0x45d47c (inc-wrap keeps it 0, so cursor
  stays put); 6 ENTER -> fade + g_pStateFunc = {menuUpdate, &DAT_004550d8}
  [g_nRecordsRow] (effective: back to menu); 7 ESC -> fade + g_pStateFunc =
  menuUpdate. Dispatch via jump table PTR_LAB_0041ef20 (8 entries @0x41ef20).
  Also entered directly from menuUpdate ("Rekord").
- `g_nLevelCount` @0x0045a6f4 [renamed] — number of levels ("toplevel") for
  current mode, set by menuInit @0x419c72 (commandDispatch "get toplevel" +
  int-convert FUN_0043e75c). Caps level cycling in stateHighScoreTable /
  stateLevelSelect / stateCharacterSelect; used by playerSetupCharacters.
- High-score config key scheme: `fshi{level}d{time|name|face|diff}{slot}`,
  e.g. `fshi2dtime0`. Table stored via config set/get commands; keys written to
  config.mm on exit (see section on config).

Network flow: stateNetworkMenu @0x420190 -> create `stateNetHostSetup` @0x420820
[renamed] (host: character/level/game rows + name input, g_nNetHostMenuSelection
@0x45d4ac, name g_szHostPlayerName @0x45a3b8) -> startServer @0x420770 ->
stateHostLobby @0x422ec0 (server lobby), or join `stateNetClientSetup` @0x421a30
[renamed] (client: name g_szPlayerName @0x45a400 + server address g_szServerAddress
@0x45a370 input) -> `stateNetConnectClient` @0x421960 [renamed] (draws "Kopplar
upp mot server", netStartClient, on fail back to setup, on success copy tables
@0x45a520->@0x45a418) -> `stateNetLobby` @0x423f20 [renamed] (client lobby:
sends lobby-join req msg id 8, polls netReceiveMsg 0x9/0xa/0xb, builds
g_playerRecords from the menu-record template @0x45c020, ack slot
g_szServerAddress[0x24], g_nHostJoinedFlag @0x45d418 -> stateStartGame when
host starts); ESC anywhere -> `netExitToMainMenu` @0x422800 [renamed].
Client lobby globals: g_nNetLobbyMenuSelection @0x45d4c0, g_nLobbyPlayerCount
@0x45a500, g_nLobbyPhase @0x45a504, g_abLobbyPlayerNames @0x45a458.

Per-frame gameplay [VERIFIED]:
`gameRunFrame` @0x0040ad80 -> `gameWorldUpdate` @0x0040b3d0 ->
  `playerUpdateDispatch` @0x004010e0 (per player in DAT_00458108; struct base
  DAT_00456360 stride 0x374; gate struct[+0x18c]==2 ->
  `playerAiUpdate` @0x00401160 else FUN_004015a0) + misc update fns
  (0x40af80/0xbeb0/0x14fa0/0xb510 playerUpdateAI/0xc800/0x34b00/0x26ee0) -> render
  `gameFrameRender` @0x0040ae30 [renamed]: world render + `renderGameHud`
  @0x00412810 [renamed] + gxFlip + gxClearScreen.
  Every-other frame: `gameObjectUpdate` @0x0040cf40 [renamed] — gameplay
  interaction/targeting (not results flow): per-player nearest-object targeting
  via objFindById @0x414a90 / objContainsPoint @0x414bb0, cart interaction,
  held-item rendering, grinder/object animation ticks.
- `roundStart` @0x0040bdf0 [renamed] — round init (only caller = scene setup
  FUN_0040a4d0 from runCmd): resets g_flRoundTimer=0, builds 8 "GRIND %d" blink
  objects (loop 2..16), g_nGamePhase=0, g_nResultsScreen=0,
  g_nRoundTimeLimit=7000, then per-level setup FUN_00416db0/004175e0/00417dc0/
  004186c0/00418f30.

In-game HUD (`renderGameHud` @0x00412810) [VERIFIED, renamed]:
- Game-over/results overlay when `g_nResultsScreen` @0x458130:
  switch(g_nGameMode): case 1/4 score tables -> "get fshi%dtime%d" / "get
  vahi%dtime%d", NEW RECORD blink (g_nGameTime%4000<2000) -> "request
  fshiscore/vahiscore %d %d %d %d" (only when `g_bGameRunning` @0x44fdc4,
  cleared here); case 2/3 winner banner ("Vinnare" or name
  &DAT_0045625c + winner*0xdd) + toplevel progression: "get toplevel", if
  level reached -> "set toplevel %d" + "save". Winner idx `g_nWinnerIdx`
  @0x458134 (set by FUN_00415420). Face sprite drawn from player id & 3.
- `g_nGamePhase` @0x458124 (-1 "Gå!!" start-go + sfx 0x22, 0 play, 5 finish
  clear) + g_nGameTime vs g_nPhaseStartTime @0x4589a0 sequencing: "Klara!"
  (0..0x2ee), "Färdiga!" (0xabe..0x8ca), both with sfx 0x21; sfx dedup via
  `g_nLastSfx` @0x4589a4.
- `g_bQuitPrompt` @0x45812c -> "Avsluta spelet? (J eller N)" overlay.
- Matkrig (mode 3): "Varor" counter + current item name (string table
  &DAT_0045839c stride 0x2c indexed by `g_nCurrentItemId` @0x458128),
  "x / 5" target; checkout-go ("Mot kassorna!!") when player+8 bit0x20 set
  (sfx via `g_bCheckoutSfx` @0x45899d).
- Inventory bar (modes 1&2, i.e. !=3 && !=4): 10-slot shopping list — item-name
  ids at player.nListItemId[10] (+0x34), collected flags at player.nListCollected[10]
  (+0x5c); animated bar height g_nInvBarTarget @0x458998 -> g_nInvBarCur
  @0x458994; draws names of still-needed items.
- Rank display (modes 2/3/4): local rank vs g_nPlayerCount (Varujakten:
  count nListCollected@0x5c; Matkrig: nItemCount@0x30; Vagnrace: time@0x34),
  "n / count".
- Misc: net-wait "Väntar..." when netIsActive && player+0x1bc==1; player face
  (ptr at player+0x8c) + "J eller N?"; progress bar from player+0x1c; time
  "%02d:%02d:%02d" in modes 1/4 (g_nGameTime). g_nObjUpdateTime @0x4580d0
  (loaded from config [master] obj_update_time; HUD scale use here, time-gate
  multiplier in the level-event directors) converts score -> display. Scroll-text
  overlay when `g_nScrollText`
  @0x4580ec (rows of 100-byte strings from 0x4589a8, count 0x4594fc; status
  line 0x4551e0 + 0x455d34*100); scrollTextClear @0x414550 (g_acScrollLines
  @0x4589a8 + g_nScrollLineCount @0x4594fc).
- Confirmed: 0x4550d8 is a zeroed text/format buffer (drawn empty here, used
  as empty format in stateHighScoreTable); its use as a "state fn" in menu
  transition arrays is a vestigial/dead-option quirk, not real code.

Round logic (`roundLogicUpdate` @0x0040beb0) [VERIFIED, renamed]:
- Round timer g_flRoundTimer @0x455e94 (frames) vs limit g_nRoundTimeLimit
  @0x4588f0: g_nGamePhase = seconds remaining; past limit -> -0x14 then
  overtime (+1 to every player nScore/frame).
- Win detection per mode, on reaching the checkout zone (Kundkort: object id
  g_nCheckoutZoneId @0x44f4e4, located via objFindById @0x414a90 / point test
  objContainsPoint @0x414bb0): modes 1/2 need player+0x28==10, Matkrig needs
  5 items (player+0x28==5), Vagnrace does a 3-checkpoint loop (nListItemId[0..2]
  ++, sfx 0x16) then finish. On win: g_nResultsScreen=1, g_nWinnerIdx,
  sfx 8, "Kundkort" banner, net sub 10 announce by host.
- Matkrig item spawn: g_flItemSpawnTimer @0x458950 drives random item id
  (1-24) into g_nCurrentItemId + all players' nListItemId[0].
- Net ready handshake: players set nActionFlags bit 0x100 ("ready"); when all
  ready host sends netServerSendSubCmd(0xFF, clear flags) + sub 1 (start).
- Ends with per-level scene update switch on g_nLevelIdx ->
  levelEventDirector_L0..L4 (see [07-gameplay.md](07-gameplay.md) §9).
