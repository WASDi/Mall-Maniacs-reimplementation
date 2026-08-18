# Mall Maniacs (maniac.exe) — 06. Config / save / filesystem

[Back to README](README.md)

Status:

The configuration subsystem is statically mapped and renamed. Its DFF-style
parser, tree model, environment queries, serializer, XOR save path, and console
dispatch contract are understood from decompilation and on-disk evidence. The
rebuild does not yet implement this subsystem; the console remains out of scope
until a config-driven single-player state needs it.

## Purpose and scope

Configuration is a shared text-tree service used by `maniac.cfg`, save data,
`.ai` navigation files, and `.eo` event-object files. It also supplies the
developer-command layer used by startup and level setup. Detailed `.ai`, `.eo`,
and `maniac.cfg` schemas are in [10-fileformats.md](10-fileformats.md); the
underlying statically linked CRT and iostream implementation is summarized in
[13-msvc-crt.md](13-msvc-crt.md).

## Verified implementation

- **Save/load:** `config.mm` is the obfuscated on-disk save and `sommar.sol` is
  a temporary plaintext working copy. `loadCmd` (`0x406270`) XOR-copies
  `config.mm` to `sommar.sol`, parses it, and deletes the temporary file;
  `saveCmd` (`0x406410`) serializes the tree, XOR-copies the temporary file
  back to `config.mm`, and deletes it. XOR-decoding the 4,027-byte
  `config.mm` on disk produced flat `key = value` text, including high-score
  keys, `toplevel`, and the graphics `driver`; `config.mm XOR sommar.sol` is
  `0x55` throughout.
- **Command dispatch:** the 26-entry table at `0x44b308` stores handler, name,
  and help-description pointers. `commandDispatch` (`0x408b60`) and
  `commandDispatchForPlayer` (`0x4089e0`) parse a command plus arguments,
  accept name prefixes, and invoke the selected handler. The decoded commands
  cover environment access, save/load, level setup, event objects, navigation,
  gameplay control, movie recording, and shutdown. `configGetValue`
  (`0x408c60`) is used during `gameInit` to read `driver` before
  `gxLoadDriver`; `configSetValueDispatch` (`0x408c90`) serves options exit.
- **File wrappers:** the `file*` layer (`0x43e126`–`0x43e68a`) wraps CRT I/O;
  scene loading has small open/close/seek helpers at `0x408cd0`–`0x408d30`.
  The CRT and C++ stream implementation below these wrappers is fully mapped,
  but is supporting infrastructure rather than configuration-specific logic.
- **Config tree:** `g_pConfigEnv` (`0x455d38`) points to a 20-byte `ConfigEnv`
  containing the environment name, token-list bounds, and root node. The parser
  (`configParseFile`, `0x435890`) tokenizes quoted strings, braces, brackets,
  equals signs, commas, and `//` comments; `configBuildTokenTree`
  (`0x435b80`) nests blocks before `configParseTokensToTree` (`0x435c30`)
  creates typed nodes.
- **Node semantics:** block, numeric value, and string nodes are linked as
  parent/child/sibling trees. `[N]` declares a block/array count, while
  `$DFF_VALUE` and `$DFF_STRING` mark anonymous array elements and are omitted
  from serialized keys. `configSerializeTree` (`0x436320`) emits the DFF text
  format; `configSaveToFile` (`0x436230`) writes through the mapped stream
  layer. This behavior is verified by the parser, destructor, query, and
  serializer call paths.
- **Queries and lifecycle:** `configFindNode` (`0x4367e0`), typed getters,
  `configSetValue` (`0x436b50`), and the environment add/find helpers provide
  the shared lookup interface. The master environment is initialized and
  destroyed by the `configMasterEnv*` chain (`0x409b80`–`0x409bb0`), with
  `g_pConfigEnv` referencing it at runtime.
- **Related users:** level setup consumes `maniac.cfg` and invokes `nload`/
  `eload` for `.ai`/`.eo`; the movie database is a `ConfigEnv`-style object
  behind the `movie` command. These consumers are documented in their
  subsystem/file-format notes rather than enumerated here.

## Limitations and next direction

The static analysis is complete enough for format and call-topology work, but
remaining uncertainty is semantic/type tightening in consumers rather than
missing config symbols. The rebuild intentionally defers the console and the
original CRT; when a config-backed single-player state is implemented, first
add the minimal environment/query and `maniac.cfg` load path it needs, then
verify it against the on-disk files and keep the corresponding Ghidra and
rebuild signatures synchronized.
