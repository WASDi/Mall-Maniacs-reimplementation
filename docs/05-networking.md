# Mall Maniacs (maniac.exe) — 05. Networking

[Back to README](README.md)

Status:

The networking subsystem is statically reconstructed and its principal
functions and protocol values have been renamed in Ghidra. It is not part of
the current rebuild scope: `docs/16-rebuild.md` defers networking while the
offline GUI and single-player path are implemented.

## Purpose and architecture

Networking provides server/client startup and shutdown, UDP transport,
connection management, lobby state, and multiplayer gameplay synchronization.
The implementation has two layers:

- The game-facing `net*` layer around `0x426xxx` owns lifecycle, queues, and
  message exchange. `netGameUpdate` (`0x414fa0`) integrates the network tick
  with the main game loop when `netIsActive()` is true.
- The lower `mnet*` socket layer (`0x439730–0x43c640`) owns Winsock, UDP
  sockets, packet construction, sequence counters, ping timeouts, client
  lists, and send/receive worker threads.

## Verified findings

The UDP datagram type is `packet[0]`. The identified `GsMsgType` values include
ping and ping reply (`0x14`/`0x15`), ID and ID reply (`0x28`/`0x29`), connect
deny (`0x2a`), client/server quit (`0x2d`/`0x2e`), user message (`0x3c`), and
verification messages (`0x4b`/`0x4c`). Dispatch occurs inside the server and
client receive threads (`mnetSrvRecvThread`, `0x43ac50`, and
`mnetCliRecvThread`, `0x43bf60`), not in the `0x43c850` stream/queue library.
The server assigns client IDs, relays user messages, and acknowledges them;
the client handles assignment, delivery, denial, and ping timing.

## Gameplay synchronization [VERIFIED]

The gameplay layer is a reliable-ish per-tick protocol on top of `GS_*`. The
main loop calls `netGameUpdate` (`0x414fa0`) when networking is active; it
coordinates client send/receive and server receive/flush phases. Sequence
numbers use a 16-bit wraparound comparison (`netSeqNewer`, `0x414f70`), while
the server queues one relay slot per player before broadcasting it.

The compact gameplay header distinguishes transform, animation, and
subcommand messages, with a player index and remove-object bit. Verified
behavior includes packed remote transforms, selected animation states, round
start/end and winner notifications, ready synchronization, remote actions,
and round item-list updates. The principal entry points are
`netClientSendPlayerState` (`0x415000`), `netClientReceiveGameMsg`
(`0x415420`), `netServerReceiveGameMsg` (`0x415a40`),
`netServerFlushQueue` (`0x4159d0`), and `netClientSendSubCmd` (`0x415cb0`).

## Network menu and lobby [VERIFIED]

`stateNetworkMenu` (`0x420190`) leads to separate host and client setup
states, a connecting state, and the client lobby. The host setup selects
character, level, and game mode before `startServer`; the client setup accepts
a player name and server address before `netStartClient`. The lobby exchanges
player names, count, level, and game mode, then acknowledges the host's start
message and transitions to `stateStartGame` (`0x422840`). Escape exits through
`netExitToMainMenu` (`0x422800`).

## Limitations and next direction

The static analysis establishes control flow and packet behavior but does not
demonstrate a working network session in the rebuild. Winsock, network menu
states, lobby integration, and multiplayer gameplay remain deferred by the
rebuild scope. When networking is resumed, validate the reconstructed UDP
transport first, then exercise host/client lobby handshakes and gameplay
relay against the verified `GS_*` and per-tick message findings above.
