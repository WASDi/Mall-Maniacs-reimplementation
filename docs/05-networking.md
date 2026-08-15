# Mall Maniacs (maniac.exe) — 05. Networking

[Back to README](README.md)

## 7. Networking subsystem [VERIFIED] (renamed)
Two layers:
- `net*` protocol layer @0x426xxx: netInit @0x426b00, netExit @0x426b30,
  netStartServer @0x426b60, netShutdown @0x426bc0, netStartClient @0x426c40,
  netServerSendToClient/ToAll/Broadcast, netServerEnqueueMsg, netServerReceiveMsg,
  netSendMsg @0x426e10, netReceiveMsg @0x426e60, netIsActive @0x426ed0 (27 xrefs),
  netMsgQueue* + netQueue* queue helpers (0x426760-0x426a40), netServerObjCtor/
  Dtor, netClientObjCtor/Dtor, netAtExitRegister. (netExit thunk @0x414f60 too.)
- `mnet*` socket library @0x439730-0x43c640: mnetWsaStartup/Cleanup, UDP
  socket create (mnetServerSocketCreate/mnetClientSocketCreate), send/recv
  wrappers (mnetSendTo/mnetRecvFrom/mnetClientSendTo/...), send/receive
  worker threads (netSrvPingThread, mnetSrvRecvThread, mnetSrvSendThread,
  netCliPingThread, mnetCliRecvThread, mnetCliSendThread), client list mgmt
  (mnetSrvFindClientByAddr/Id, mnetSrvRemoveClientById, mnetSrvCloneClientList,
  ping timeout mnetSrvCheckPingTimeout/ClientTimeout), packet builders
  (mnetSrvBuildSendPkt/BroadcastPkt, mnetCliBuildSendPkt), seq counters
  (mnetNextMsgSeq, mnetCliNextSeq), mnetLogInit ("mnet.log").
- GS_* protocol (UDP datagram type byte = packet[0]) [VERIFIED]:
  - Enum `GsMsgType` created: GS_PING 0x14, GS_PING_REPLY 0x15, GS_ID 0x28,
    GS_ID_REPLY 0x29, GS_CONNECT_DENY 0x2A, GS_CLIENT_QUIT 0x2D,
    GS_SERVER_QUIT 0x2E, GS_USER_MSG 0x3C, GS_VERIFY 0x4B, GS_MULTI_VERIFY 0x4C.
  - Dispatch happens INSIDE the recv threads (NOT at 0x43c850 — that region is a
    thread-safe stream/queue library, FUN_0043c974=ostream-write etc.):
    - Server: `mnetSrvRecvThread` @0x0043ac50 — GS_PING->reply GS_PING_REPLY;
      GS_PING_REPLY->ping-time (QueryPerformanceCounter, timestamp in pkt +7/+0xb);
      GS_ID->alloc client id (mnetAllocClientId)+name->GS_ID_REPLY (or GS_CONNECT_DENY
      when server busy); GS_USER_MSG->broadcast to all via mnetSrvBuildBroadcastPkt
      + ack GS_VERIFY to sender; GS_VERIFY->mnetSrvPurgePending.
    - Client: `mnetCliRecvThread` @0x0043bf60 — GS_ID_REPLY->set my id (+5),
      reply GS_VERIFY; GS_CONNECT_DENY->purge+event; GS_USER_MSG->deliver;
      GS_PING->GS_PING_REPLY; GS_PING_REPLY->ping-time.
  - Debug log strings "IN: GS_* - OUT: ...", "ILLEGAL - GS_*", "SENDING A GS_*"
    @0x4516c8-0x451c38 (log via FUN_0043f7cd).
- netSpawnThread @0x43f80e (CreateThread helper). Thread primitives elsewhere.

## 7a. Network gameplay message layer [VERIFIED, renamed]
Reliable-ish per-tick gameplay sync on top of the GS_* transport. Called from
the main game loop when `netIsActive()`.
- `netGameUpdate` @0x00414fa0 [renamed] — per-frame net tick: g_nNetTick++,
  then client-send (0x415000), server-recv (0x415a40), server-broadcast
  (0x4159d0), client-recv (0x415420); copies remote players' fPosX/Z ->
  fPosPrevX/Z (prev-tick snapshot). `netGameLayerReset` @0x00414f10 [renamed]
  clears all queues/flags + netInit. `netSeqNewer` @0x414f70 = 16-bit wraparound
  seq comparison (|a-b| < 0x8000), used by netClientReceiveGameMsg join ordering.
- Message buffer `g_abNetMsgBuf` @0x459aa8 (43 bytes). Header byte:
  bits0-3 = type (1=transform, 2=anim state, 3=subcommand), bits4-6 = player
  idx, bit7 = object-remove. Payload bytes follow (packed pos/angle/bone).
- Client send `netClientSendPlayerState` @0x00415000 [renamed]: packs local
  player transform (ftol'ed to bytes), sends type-1 msg (0x2b bytes, or 0x19
  on remove); throttles when nAnimBusy (@tick%3) or unchanged transform
  (g_bTransformSent @0x459cc0); sends type-2 anim msg when nAnimState in
  {2,5,6}.
- Client recv `netClientReceiveGameMsg` @0x00415420 [renamed]:
  type1 -> decode packed bytes -> player fPosX/Z + model transforms via
  FUN_00404e00 on handles at +0xd4/+0x114 (attach FUN_0040e040 / detach
  FUN_0040e5b0 on bit7); type2 -> nAnimState = 2|5|6 else 0, clear nAnimBusy;
  type3 subcommands: 1=nNetWait=2, 10=round-end (g_nResultsScreen=1,
  g_nWinnerIdx=server-announced), 30/31=remote actions FUN_0040e910/FUN_0040f3e0,
  32=round-start item list (g_nCurrentItemId + every player nListItemId[0]/
  nListCollected[0]).
- Server recv `netServerReceiveGameMsg` @0x00415a40 [renamed]: seq-checks
  type1/type2 via g_awNetSeq, queues into g_abRelayBuf[player]/g_awRelaySeq,
  sets pending flags; type3 sub-opcodes 0x3C/0x3D/0x3E/0x3F rebroadcast to all
  as 0x1E/0x1F/0x20/10 via `netServerSendSubCmd` @0x00415d20 [renamed] (0xfe/
  0xff clear per-player/all nActionFlags @+0x1c0); other opcodes OR'd into
  nActionFlags.
- Server broadcast `netServerFlushQueue` @0x004159d0 [renamed]: sends all
  queued g_abRelayBuf slots (0x2b bytes) + g_awRelaySeq (2 bytes) to every
  client, clears pending flags.
- Client subcmd sender `netClientSendSubCmd` @0x00415cb0 [renamed] (mirror of
  netServerSendSubCmd, goes to server). Round start: client broadcasts "ready"
  (sub 0x100) when nNetWait==0; when ALL players ready, host clears action
  flags (sub 0xFF) and starts round (sub 1 -> clients set nNetWait=2). Host
  announces winner with sub 10; Matkrig new-item broadcast sub 0x20.
- Queue globals: g_abRelayBuf @0x459900 (byte[344] = 8x0x2b), g_abRelayPending
  @0x459a58, g_awRelaySeq @0x459a78, g_abRelaySeqPending @0x459a88, g_nNetTick
  @0x459ca8, seq slots @0x459cac.

## 7b. Network menu / lobby states [VERIFIED, renamed]
Menu states reached from `stateNetworkMenu` @0x420190 (main menu Nätverk), all
of the (type=0 draw / type=1 input,key,sub-action) state-fn convention, using
the mixed-case font helper (`textDrawMixedCase` @0x41ffc0) and the shared
backdrop panel (`DAT_0045a6bc` texture, scroll angle g_nMenuAnimPhase).
- `stateNetHostSetup` @0x00420820 [renamed] — host create-game menu. Rows
  Karaktär/Bana/Spel (bounded by per-row max table + g_nLevelCount+4), name
  input (g_szHostPlayerName @0x45a3b8, 19 chars), Starta -> `startServer`.
  Selection g_nNetHostMenuSelection @0x45d4ac; g_nPlayerCount forced to 8.
- `stateNetClientSetup` @0x00421a30 [renamed] — client join menu: Ditt namn
  (g_szPlayerName @0x45a400) + Server address (g_szServerAddress @0x45a370)
  inputs, Starta -> `stateNetConnectClient`.
- `stateNetConnectClient` @0x00421960 [renamed] — connecting state: draws
  "Kopplar upp mot server" once (latch g_nNetConnectLatch @0x45d4b0), then
  netGameLayerReset + netStartClient(&g_szServerAddress, &g_szPlayerName);
  on net failure netExit -> back to setup; on success copies
  DAT_0045a520 -> DAT_0045a418 (net config block) -> `stateNetLobby`.
- `stateNetLobby` @0x00423f20 [renamed] — client lobby ("Väntar på spelare..."):
  first frame sends lobby-join req (msg id 8, packed game-type from
  DAT_0045d408) and clears the server-slot ack bytes
  g_szServerAddress[0x24..0x27]; then polls `netReceiveMsg`:
  - 0x9 (host lobby list): copies player count/level/game-mode + name table
    (g_abLobbyPlayerNames @0x45a458, stride 0x15) -> g_nPlayerCount/g_nLevelIdx/
    g_nGameMode, g_nLobbyPhase=2.
  - 0xb (host start): builds each player's g_playerRecords[..] from the
    menu-player-record template @0x45c020 (name, char/cart/anim fields), sets
    g_nLocalPlayerIdx + local flags (player+0x154=1, +0x2dc=0), replies with ack
    byte g_szServerAddress[0x24]=1; when ack-slot set && g_nHostJoinedFlag
    @0x45d418 -> `stateStartGame` @0x422840.
  - 0xa (lobby re-list): re-copy of name table, g_nHostJoinedFlag=1.
  Draws per-player panels (name, game-mode text, level progress bar).
  Input: g_nNetLobbyMenuSelection @0x45d4c0, ESC -> `netExitToMainMenu`.
- `netExitToMainMenu` @0x00422800 [renamed] — netExit() + g_pStateFunc =
  stateNetworkMenu (also used as the lobby input case-7 / option-0 action).
- Shared: g_nLobbyPlayerCount @0x45a500 (net player count for lobby), g_nLobbyPhase
  @0x45a504, g_nNetMenuSelection @0x45d4b8 (client setup row cursor).
