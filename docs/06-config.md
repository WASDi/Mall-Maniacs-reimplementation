# Mall Maniacs (maniac.exe) — 06. Config / save / filesystem

[Back to README](README.md)

## 8. Config / save / file system [VERIFIED]
- `file*` wrappers @0x43e126-0x43e68a [renamed]: fileOpen/OpenEx, fileClose,
  fileRead/ReadEx, fileWrite/WriteEx, fileSeek/SeekEx, fileTell/TellEx,
  fileDelete. (These wrap CRT file ops; crtLock/crtUnlock @0x43fc2c/0x43fc7e
  are the locking layer.)
- Statically-linked MSVC CRT layer beneath the file* wrappers [renamed]:
  low-level I/O crtWrite/Read/Open/Close/Lseek/Setmode/IoInit
  @0x43fec3-0x4408c5 (OpenFileA-based FD table @0x452b10), FILE stream layer
  crtFopen/FopenSlot @0x442105/0x442275, crtFgetc/Ungetc @0x442d99/0x442db3,
  crtFilbuf/Flsbuf @0x442029/0x441ee6, crtFflush family @0x43f7cd-0x43facc,
  format engine crtVfprintfCore @0x442ea4 (used by crtSprintf @0x43ee0f) +
  crtFscanfCore @0x44233d + crtPutcCore/PutFill/PutChars @0x4435e5-0x44364b,
  errno/TLS crtGetPtd @0x441ca3, crtErrno/crtDoserrno @0x441ed4/0x441edd,
  crtDosmaperr @0x441e61 (WIN32->errno table @0x452408/0x45240c). Full map in
  the §16 summary of [11-issues.md](11-issues.md).
  sceneLoadSen helper trio [renamed]: fileOpenMode @0x408cd0, fileCloseStream
  @0x408d00, fileSeekTell @0x408d30.
- Save files: `config.mm` + `sommar.sol` (temp) — same content, XOR ^0x55 of
  each other. Plaintext format is flat text:
      `fshi0face0   = "2"`  (high-score tables: <level>hi<N>{face,time,name,diff})
      `toplevel = "4"`      (menu state to boot into)
      `driver = "DRIVERS\GXGLIDE.DLL"`  (graphics driver; escaped backslash)
  [VERIFIED by XOR-decoding config.mm on disk; 4027 bytes, pure key="value"]
- The XOR ^0x55 load/save is IN the command handlers (not separate fns):
  - `loadCmd` @0x00406270 [renamed]: builds config.mm -> sommar.sol XOR-copy,
    parses sommar.sol, deletes it.
  - `saveCmd` @0x00406410 [renamed]: serializes tree, then sommar.sol ->
    config.mm XOR-copy, deletes sommar.sol.
- Console/developer command system [VERIFIED, table FULLY decoded]:
  - Table @0x44b308: 26 entries x 12B {handler, pName, pDesc}; commands sorted
    alphabetically. Dispatchers (duplicate copies): `commandDispatch`
    @0x00408b60 (passes caller arg) + `commandDispatchForPlayer` @0x004089e0
    (passes `&DAT_00456210 + DAT_00458104*0x374` = local player). Both parse
    "cmd rest" (fmtSscanf "%s %s" @0x44e70c), match name (prefix match allowed,
    via `cmdNameStartsWith` @0x00408690), call `(*handler)(player, args)`.
  - Full command list (all handlers renamed <cmd>Cmd):
    action 0x4067c0, ai 0x406ab0, collision 0x406c00, eload 0x406cb0,
    eheight 0x407660, ename 0x4071a0, esave 0x4072a0, evalue 0x407520,
    get 0x4066d0, help 0x407820, kill 0x407870, load 0x406270, log 0x4078b0,
    mfind 0x407970, movie 0x407a60, nclean 0x407db0, nload 0x407e10,
    nsave 0x408120, pos 0x406b60, quit 0x408350, reset 0x408370,
    request 0x41a730, run 0x4084c0, save 0x406410, set 0x406500,
    status 0x408540.
  - FIXED mis-names: configCmd_get@0x4078b0->logCmd, configCmd_saveEventObjects
    @0x407970->mfindCmd, stopGame@0x407870->killCmd, startGame@0x4084c0->runCmd.
    (getCmd @0x4066d0 was accidentally correct semantically.) NOTE: 0x408380 was
    a stale/misread address INSIDE resetCmd's body — quit is 0x408350 (correct).
  - Help strings + effects (26 entries, null-name terminator at 0x44b43c):
    action 0x4067c0 "action <action>" (grab/release cart, drop item, run/%d);
    ai 0x406ab0 toggles g_bAiEnabled @0x458358; collision 0x406c00 toggles
    g_bCollisionEnabled @0x458354; eload 0x406cb0 "Load event objects";
    eheight 0x407660 "Set height, 0=min 1=max 2=max rel. min";
    ename 0x4071a0 "Name an event object"; esave 0x4072a0 "Save event objects";
    evalue 0x407520 "evalue <show>|<index> <value>"; get 0x4066d0 "Get
    environment variable"; help 0x407820 "Display this help page"; kill 0x407870
    "Kill current game"; load 0x406270 "Load environment variables";
    log 0x4078b0 "Modify log parameters"; mfind 0x407970 "Mesh find (Syntax:
    mfind <keyword>)"; movie 0x407a60 "Movie control" (demo recorder);
    nclean 0x407db0 "Cleanup unlinked navpoints"; nload 0x407e10 "Load
    navigation buoys"; nsave 0x408120 "Save navigation buoys"; pos 0x406b60 (no
    help, reposition mesh by name); quit 0x408350 "Shutdown the game" -> sets
    g_bQuitRequested @0x4580f4; reset 0x408370 "Reset Master, Player and/or
    Object data"; request 0x41a730 "Request menu at exit (SYSTEM CALL ONLY!)" ->
    deferred state change DAT_0045a710 (fshiscore/vahiscore/play_level/endscene);
    run 0x4084c0 "New game (Syntax: run <level>)"; save 0x406410 "Save
    environment variables"; set 0x406500 "Set environment variable"; status
    0x408540 "Show game status" (stub).
  - `configGetValue` @0x00408c60 (renamed): fmtSprintf "get %s" + dispatch.
    gameInit calls configGetValue("driver") then gxLoadDriver(result) @0x409f9c.
    `configSetValueDispatch` @0x408c90 ("set %s %s" -> commandDispatch; called
    from stateOptionsExit).
- Config data model (renamed, struct created):
  - `g_pConfigEnv` @0x00455d38 (ConfigEnv*, 20 bytes: +0 pszName, +4 nNameLen,
    +8 pTokenHead, +0xc pTokenTail, +0x10 pRoot).
  - configEnvCtor @0x004357c0 (name="default.dff"), configEnvSetName
    @0x004354a0, configParseFile @0x00435890 (tokenizer: quotes/{}[]=,,
    "//" comments; parse -> token list via configParseAddToken @0x00435820 +
    configTokenNodeCtor @0x00435650; configFreeTokenList @0x004368b0).
  - Token->tree build (VERIFIED):
    - Token nodes 0x14B: +0 mString, +8 next, +0xc parent, +0x10 children.
      configBuildTokenTree @0x00435b80 = 1st pass: walks token list, nests
      `{`/`}` blocks (sets +0xc parent, +0x10 children on `{`, recurses until
      `}`), returns head.
    - configParseTokensToTree @0x00435c30 = 2nd pass (recursive): converts
      token tree into ConfigNode objects, creating per token type:
      * `"str"` (leading quote) -> STRING node 0x24B (vtable 0x44b7c4), key =
        "$DFF_STRING" @0x451178 sentinel, value = unquoted substring.
      * `name = "str"` -> STRING node with key=name.
      * `name = num` -> VALUE node 0x28B (vtable 0x44b7c0), value double @+0x20.
      * `name [` then `]`/`{` -> BLOCK node 0x2cB (vtable 0x44b7bc): array
        count from the `[N]` token, children from inner tokens.
      Sibling chaining via prev/next (+0x14/+0x18); parent at +0x10; children
      head at +0xc.
    - Node ctors: configNodeCtorBase @0x004356d0 (vtable 0x44b7b8),
      configBlockNodeCtor @0x00435780, configValueNodeCtor @0x004357a0.
    configNodeDtor @0x00435700 [renamed] = recursive node teardown (same vtable
    0x44b7b8): frees children, sibling chain, +0x18 next, then the node's
    mString key; callers include FUN_00407140, FUN_0040aec0, FUN_00436a60
    (config env teardown). configNodeGetValue @0x00436850 [renamed, 1-arg] =
    node value double getter (distinct from the 2-arg configNodeGetValue
    @0x00436940 below).
    - Anonymous-node semantics (CLOSED): "$DFF_VALUE"/"$DFF_STRING" are
      sentinel keys marking anonymous (un-named) nodes. In configSerializeTree
      @0x00436320 the serializers checks key against "$DFF_VALUE" via
      mStringNotEquals @0x004355c0 and OMITS the `key` part for anonymous
      nodes (plain number / bare string in arrays). In contrast "$DFF_BLOCK"
      names a root block. Type values are virtual via vtable[0]:
      configNodeTypeBlock(0)/configNodeTypeValue(1)/configNodeTypeString(2) —
      thunks @0x00436590/@0x004365b0/@0x004367d0 return 0/1/2.
  - configFindNode @0x004367e0 (linked list key search), configNextNode
    @0x00436870, configGetHead @0x00436890, configNodeGetType @0x00436830,
    configNodeGetKey @0x00436900, configNodeGetValue @0x00436940,
    configSetValue @0x00436b50, configSaveToFile @0x00436230,
    configSerializeTree @0x00436320 (recursive writer).
  - Save serialization goes through the CRT iostream layer [renamed]:
    configSaveToFile @0x00436230 -> streamOpenOutputFile @0x43d4f3 (ofstream
    open; sole caller), configSerializeTree @0x00436320 writes via
    streamWriteInt @0x43d5f0 / streamWriteFloat @0x43d6a1 /
    streamWritePadded @0x43cac4, and builds keys through mStringAssignFromCStr
    @0x43c974 (called from configSerializeTree + mStringAssignFromStr
    @0x00435610). The stream/filebuf class family behind it is the stream*
    cluster @0x43c850-0x43dfbb ([11-issues.md](11-issues.md) §16).
  - Node types (virtual type-fn): 0=BLOCK/section (emits `name[count] {`),
    1=VALUE (key="$DFF_VALUE"), 2=STRING (emits `key = "value"`). Format tags
    at 0x451160: "$DFF_VALUE" "$DFF_STRING" "$DFF_BLOCK", "[\0" "]\0" "{\n"
    "}\n" "\t= " "\"". Node layout: [0]=typefn, [1]=key, [3]=children,
    [6]=next(+0x18), [7]=value-string/count.
  - Core string class (mString: +0 ptr, +4 len): mStringCStr @0x004351d0,
    mStringAssign @0x004353e0, mStringFree @0x00435430, mStringCtorFromCStr
    @0x00435100 (from-cstr), mStringAssignCopy @0x00435440 (copy-in), mStringToInt
    @0x004351e0, mStringToFloat @0x004351f0, mStringCharAt @0x004351a0,
    mStringSubstr @0x00435200, mStringLength @0x00435630, mStringEquals
    @0x00435570, mStringNotEquals @0x004355c0. mStringCtorEmpty @0x00435150
    [renamed], mStringAppendChar @0x004352b0 [renamed],
    mStringCtorWithCapacity @0x00435170 (alloc param+1, null-terminated; used
    by mStringSubstr/configNodeGetKey), mStringTrim @0x00435310 (strip leading/
    trailing ws/tab in place; aiCmd/collisionCmd), mStringEqualsMString
    @0x00435510 (mString-vs-mString equal; configEnvFind/node-tree key match),
    mStringAssignFromStr @0x00435610 (copy other mString's cstr via
    mStringAssignFromCStr @0x43c974; configSerializeTree). Config file-stream
    class dtor configStreamObjDtor @0x00435b60 (EH unwind: releases the
    buffered stream object at this+0xc).
  - Config-environment query/scan cluster [renamed]: configEnvFind @0x004365c0
    (walk g_pConfigEnv chain by name), configEnvGetValue @0x00436560 (find env +
    configGetValue), configEnvGetString @0x00436990 (find env + string getter),
    configEnvGetDouble @0x004369f0 / configEnvGetDouble2 @0x00436a20 (find +
    value-as-double), configEnvAddValue @0x00436eb0 (append/update a key=value
    pair), configEnvGetValueByIndex @0x00436550 (configEnvGetValue with null
    key). Tree-build cluster: configNodeAppendBlock @0x00436fa0 (append a BLOCK
    child under a node), configBlockNodeNew @0x00436d50 (alloc + link a block
    node into the children/sibling list), configNodeFree @0x00436a60 (unlink +
    dtor + free subtree), configEnvFreeChildren @0x004368a0 (free env's
    children; used by configParseFile teardown). Used by configGetValue callers
    (driver pick @0x409f9c etc.).
- Config-env master init chain [VERIFIED, renamed]: configMasterEnvInit/Ctor/
  Atexit/Dtor @0x409b80/0x409b90/0x409ba0/0x409bb0 (g_configEnvMaster ConfigEnv
  @0x455e48, g_pConfigEnv @0x455d38 -> it at runtime; ConfigEnv struct created
  {pszName,nNameLen,pTokenHead,pTokenTail,pRoot}).
- scenePathString init chain @0x409bc0 (+dtor @0x409c00; g_scenePathString
  @0x457db0 = levelSetup scene-path buffer, ctor scenePathStringCtor @0x409c10
  inits mString at +0x32c).
- movieDb (ConfigEnv subclass backing the demo recorder): movieDbCtor @0x40af30
  (configEnvCtor + mString +0x14), movieDbDtor @0x40aec0, global init chain
  movieDbGlobalCtor/Atexit/Dtor @0x40ae90/0x40aea0/0x40aeb0 (g_pMovieDb
  @0x455e68). Used by the "movie" console command — see [09-sound.md](09-sound.md)
  demo-recorder section.
