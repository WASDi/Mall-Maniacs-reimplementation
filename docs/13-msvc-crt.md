# Mall Maniacs (maniac.exe) — 13. Statically-linked MSVC CRT (0x43c850-0x44966c)

[Back to README](README.md)

The statically-linked MSVC6 CRT spans 0x43c850-0x44966c and is fully
mapped/renamed. This file collects the per-pass rename detail; the milestone
summary lives in [11-issues.md](11-issues.md) §16. Names use the `crt*` /
`eh*` / `stream*` prefixes. Facts marked [VERIFIED] were confirmed by reading
decompilation/xrefs; everything else is hypothesis.

## Key globals (CRT)
- Heap: `DAT_00462a10` = heap type selector (3=SBH, 2=heap2); `g_hCrtHeap`
  @0x462a0c = main heap (HeapAlloc/Free); SBH tables `g_pCrtReallocMem` @0x462a04
  / `DAT_004629f0` / `DAT_00462a00`; heap2 region heads `PTR_LOOP_00452878` /
  `PTR_LOOP_00454898`; fd table `DAT_00462a20` (stride 0x24, max
  `g_nCrtFileCount` @0x462b20 = open-FILE count).
- Locks: critical sections `g_pCrtLockCsMain/Io/Env/Misc/Time` @0x4527b0/b4/d4/e4/f4
  (roles HYPOTHESIS; all init by crtInitCriticalSections); indices 0x9=heap,
  0xb=tz, 0xc=env/atexit-alt, 0xd=atexit, 0x11=fd table, 0x13=locale,
  0x19=locale/ctype.
- FILE structs: `DAT_00452130`/`00452150` (stdout/stderr), lazy 0x1000 buffers
  `DAT_00462640`; stream buffer globals via crtGetBuf.
- Ctype: `DAT_0045257c` static ushort table (index*2, bit 4 = _DIGIT),
  `DAT_00452586` static ushort table (alt path), `DAT_004628e0` dynamic ctype
  table (0x10=upper, 0x20=lower), `DAT_004627e0` case-conversion table,
  `DAT_00462654` Unicode-support flag (1=GetStringTypeW, 2=GetStringTypeA),
  `g_nCrtWideCodePage` @0x462634 (WideCharToMultiByte/MultiByteToWideChar) +
  `g_nCrtCodePage` @0x4627bc (GetCPInfo), `g_nCrtMbBytes` @0x452788,
  `g_pCrtCtypeSrc` @0x44bdb0 (GetStringTypeA/LCMapStringA) + `g_pwCrtCtypeSrc`
  @0x44bdb4 (W variant), `DAT_00462624` LCID.
- Timezone/DST: `DAT_00454c00`=_timezone, `DAT_00454c04`=_daylight,
  `DAT_00454c08`=_dstbias, `g_szCrtTzStdName` @0x454c8c / `g_szCrtTzDstName`
  @0x454c90 = _tzname[0]/[1] (char[64]), wide sources `g_pwCrtTzStdNameW`
  @0x4626dc / `g_pwCrtTzDstNameW` @0x462730, `DAT_004626d0` tz-init flag,
  `DAT_004626d8` TIME_ZONE_INFORMATION, DST window `DAT_00454c9c`/`00454cac`,
  `DAT_00454c98`/`00454ca8`=0xffffffff.
- Env: `DAT_00462464`/`00462468` = _environ table/len, `DAT_0046246c` wide env,
  per-thread handler slots `DAT_0046278c`/`00462790`/`00462794`/`00462798`,
  per-thread table count/size `DAT_00454918`/`0045491c`, env-init flag
  `g_nCrtEnvInitDone` @0x463b48 (set by crtInitEnvironment @0x445451).
- TLS/errno: `g_dwCrtTlsIndex` @0x452400, `g_crtDosErrnoMap`
  @0x452408 (8-byte recs {uDosError, nErrno}), `g_crtRuntimeErrTable`
  @0x454928 (crtAmsgExit msgs), `g_nCrtAtexitCount` @0x454924,
  `g_nCrtDstStartMd/Ms` @0x454c9c/a0, bad-FILE template
  `g_abCrtBadFileTmpl` @0x4523b0 (crtFilbuf @0x4420ad; flags byte @+4).

## CRT naming tail — final pass (last 16 FUN_*)
- `crtXcptFilter` @0x4451d4 — SEH exception filter: maps exception code ->
  per-thread ptd[0x16] error code 0x81-0x8a, UnhandledExceptionFilter fallback,
  atexit-handler lookup via crtAtexitTableFind.
- `crtGetBuf` @0x445df2 — lazy 0x1000 stdout/stderr buffer alloc
  (DAT_00452130/00452150 + DAT_00462640); called from crtFflush.
- `crtStrcspn` @0x447a40 + `crtStrpbrk` @0x447a80 — bitmap scan helpers.
- `crtStrColl` @0x4490d7 — CompareString wrapper, Unicode detect via
  `DAT_004627b8`.
- `crtEnvWToMb` @0x449069 — wide env conversion (DAT_0046246c).
- `crtPutEnv` @0x449354 — _putenv core: SetEnvironmentVariableA + _environ
  DAT_00462464/68.
- `crtEnvFind` @0x4494db, `crtEnvCopy` @0x449533 (env table clone via
  crtStrDup).
- `crtMbsChr` @0x44959a — _mbschr (multibyte-aware strchr).
- `crtStrDup` @0x449631 — _strdup.
- `crtStrUpr` @0x44966c — _strupr via crtLMapString.

## CRT tail pass 2 (stdio/format/locale clusters, ~45 functions)
- `crtStrToFloat` @0x446aaf, `crtFtoaPadDigits` @0x446adc, `crtGcvt` @0x446b53,
  `crtDblToLd` @0x446baf, `crtPutcBuffered` @0x446f4f.
- `crtGetStringType` @0x446fbd — GetStringTypeW/A wrapper (Unicode flag
  DAT_00462654), `crtWcToMbLocked` @0x447106, `crtWcToMb` @0x44715f
  (WideCharToMultiByte single char, EILSEQ).
- `crtFdSeek` @0x4472b5 + `crtFdSeekCore` @0x447326 (locked SetFilePointer),
  `crtFtell` @0x4473ab, `crtHexDigitVal` @0x442d62 (scanf hex canonicalizer).
- ctype/locale: `crtIsCtype` @0x447589 + `crtIsDigit` @0x447578, `crtSetLocale`
  @0x4475ba + `crtResolveCodePage` @0x447767 (-2 OEM/-3 ACP/-4 current),
  `crtMbCurMax` @0x4477b1, `crtCtypeReset` @0x4477e4, `crtInitCtypeTables`
  @0x44780d (dynamic ctype DAT_004628e0 + case table DAT_004627e0),
  `crtLocaleInit` @0x447992, `crtMessageBoxA` @0x4479ae (dynamic user32 load).
- Long-double strtold cluster: `crtStrtold` @0x447aba, `crtLdFromDigits`
  @0x4487e6, `crtLdShiftLeft1` @0x44878b, `crtLdShiftRight1` @0x4487b9,
  `crtLdMul` @0x448d11, `crtLdScale10` @0x448f31, `crtAdd32Carry` @0x44870c,
  `crtLdToStr` @0x4488ad.
- Timezone/DST: `crtTzsetInit` @0x447f8b, `crtTzsetCore` @0x447fb9 (_tzset),
  `crtIsDst` @0x448240, `crtIsDstCore` @0x448261, `crtDstTransition` @0x44840d.
- Strings: `crtStricmp` @0x448b40 + `crtStrnicmp` @0x448c10, `crtGetenv`
  @0x448fad, `crtCallHandlerSlot` @0x44854d + `crtPtdFindSlot` @0x4486cf.

## EH runtime + allocator + exit/lock/startup clusters (~80 functions)
- C++-EH unwind: `crtCxxFrameHandlerEntry` @0x43dde1 (__CxxFrameHandler, 70
  xrefs from 0x449xxx scope table), `crtLocalUnwind` @0x43dd92 (_local_unwind),
  `ehFuncletJump`/`Dispatch`/`Dispatch2` @0x43dd50/84/8b, `ehCallSettingFrame`
  @0x43de17 + `ehFrameHandler` @0x43de6b, `ehStateDispatch`/`Handler`/`Lookup`
  @0x43de90/df46/dfbb.
- EH runtime internals: `ehTypeMatch` @0x4416cc, `ehWalkFuncletChain` @0x441729,
  `ehDispatchException` @0x4417dd, `ehInvokeFunclet` @0x441858,
  `ehCheckTermination` @0x441925 (0x19930520 magic), `ehDispatchCatchBlock`
  @0x44199d, `ehDispatchFuncletHandler` @0x441b61, `ehRelocHandlerAddr` @0x441bc8,
  `ehAbortHandler` @0x441daa, `ehTerminateHandler` @0x441e0b.
- C++-EH vec ctor/dtor: `crtEhvecCtor` @0x43eb5d (used by
  playerArrayCtorInit g_apPlayers), `crtEhvecDtor` @0x43ebdf, unwind funclets/
  loop @0x43ebc7/ec47/ec5f.
- FPU/FP-cvt init: `crtFpuInit` @0x43dcbf, `crtFpCvtInit` @0x43dcd7,
  `crtThreadHookStub` @0x43dcd6, `crtCinit` @0x43ecef (cinit tables
  0x44e024-0x44e038 / 0x44e000-0x44e020).
- atexit/exit: `exitRegisterAtexitCore` @0x43ea9e, `exitProcInternal` @0x43ed3e,
  `crtLockAtexit`/`UnlockAtexit` @0x43ede3/43edec, `crtAtexitTableFind` @0x445312.
- lock/unlock-by-index: `crtLockByIndex`/`UnlockByIndex` @0x443d5d/443dbe
  (critical sections @0x4527b4-0x4527f4), `crtInitCriticalSections` @0x443d34,
  `crtLocalUnwind2` @0x443d19, `crtCallNewHandler` @0x443dd3.
- MSVC6 small-block heap SBH: `crtHeapInit` @0x443f63 (HeapCreate +
  __MSVCRT_HEAP_SELECT), `crtHeapSelect` @0x443e1b, `crtGetPeSubsystem`
  @0x443dee, `crtSbhInit` @0x443fc0, `crtSbhFindBlock` @0x444008,
  `crtSbhFreeBlock` @0x444033, `crtSbhAllocBlock` @0x44435c,
  `crtSbhAllocRegion` @0x444665 (VirtualAlloc 0x100000), `crtSbhAllocGroup`
  @0x444716 (VirtualAlloc 0x8000), `crtSbhReallocBlock` @0x444811, `crtRealloc`
  @0x44383c, `crtMsize` @0x443b64, + crtUnlockHeap9* wrappers.
- heap2 large-block allocator: `crtHeap2FindBlock` @0x444d63,
  `crtHeap2AllocBlock` @0x444dff, `crtHeap2FreeBlock` @0x444dba,
  `crtHeap2CarveBlock` @0x445007, `crtHeap2ReallocBlock` @0x44512b,
  `crtHeap2AllocRegion` @0x444b07, `crtHeap2FreePages` @0x444ca1,
  `crtHeap2DeleteRegion` @0x444c4b.
- TLS/type_info: `crtTlsInit` @0x441c3c, `crtPtdInit` @0x441c90,
  `crtTypeInfoScalarDeleteDtor` @0x43fcf9.
- startup cmdline/env: `crtFindCmdlineArgs` @0x44534c, `crtInitArgv` @0x44545d
  + `crtParseCmdlineCore` @0x4454f6, `crtInitEnvironment` @0x4453a4,
  `crtGetEnvironmentStrings` @0x4456aa.
- runtime-error reporting: `crtAmsgExit` @0x446cc8, `crtReportErrorExit`
  @0x4457dc, `crtWriteRuntimeError` @0x445815, `crtAbortError` @0x446d85,
  `crtIsBad{Read,Write,Code}Ptr` @0x446d35/51/6d, `crtMbToWc`/`Locked`
  @0x446e66/446e09, `crtStreamGetBuf` @0x446d9c, `crtStreamIsUnbuffered`
  @0x446de0, `crtSehFrameSetup` @0x43fd18, `crtStrChr` @0x443780.

## CRT region 0x43c850-0x44966c mapped + renamed (130 functions)
- config-save iostream/serialization stream layer (stream* @0x43c850-0x43dfbb:
  streamOpenOutputFile/OpenInputFile, streamWriteInt/Float, streamFilebufOpen,
  streamIosCtor/Dtor, streambuf virtuals streamXsputn/Xsgetn/Sputc/Sgetc/
  Sbumpc/Snextc/Sputbackc/Setb, streamCsEnter/Leave; mStringAssignFromCStr
  @0x43c974).
- low-level CRT I/O: crtWrite/Read/Open/Close/Lseek/Setmode/IoInit
  @0x43fec3-0x4408c5, crtFopen/FopenSlot @0x442105/0x442275, crtFscanfCore
  @0x44233d, crtVfprintfCore @0x442ea4 + crtSprintf @0x43ee0f + crtPutcCore/
  PutFill/PutChars/VaArg @0x4435e5-0x4436a0, crtFilbuf/Flsbuf @0x442029/0x441ee6,
  crtFgetc/Ungetc @0x442d99/0x442db3, crtFflush family @0x43f7cd-0x43facc,
  crtLock/Unlock @0x43fc2c/0x43fc7e.
- memory/ctype/float: crtMalloc/Free @0x43ee9d/0x43f003, crtMemcpy/Memmove
  @0x440c00/0x440590, crtFindFirst/Next/Close @0x43f5b4/0x43f681/0x43f749,
  crtTolower/Toupper+Loc, crtIsspaceTable, crtAtof, float-format
  crtEcvt/Fcvt/Gcvt + helpers @0x4410ac-0x4413af, crtExit @0x43f2bb,
  crtEndThread @0x43f918, crtRunAtExitHandlers @0x43edf5, FP init
  crtFpInit/FpStartup @0x440f35-0x440fae.
- TLS/errno: crtGetPtd/FreePtd @0x441ca3/0x441d0a, crtErrno/crtDoserrno
  @0x441ed4/0x441edd, crtDosmaperr @0x441e61 + WIN32->errno table @0x452408/
  0x45240c.
- C++ EH runtime: crtCxxFrameHandler @0x4413d4, magic 0x19930520, 'msc' code
  0xe06d7363, + wrappers @0x44146f/0x441622.

## Notes / pitfalls
- Rename calls must use the oldName/newName schema; name/new_name is silently
  ignored by the MCP bridge.
- ghidra_rename_function_by_address interprets addresses inside a function body
  as the containing function — always pass the entry address.
- Decompile/disassemble-by-name forms fail with "No function found" / "could
  not be resolved" — pass addresses instead.
- PascalCase warnings on rename are harmless.
