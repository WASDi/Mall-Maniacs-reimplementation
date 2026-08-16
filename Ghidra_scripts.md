# Running Ghidra MCP scripts in this workspace

How to write, compile, and run Java scripts against `maniac.exe` through the
Ghidra MCP bridge — including the two gotchas (stale script build cache and
manual javac compilation) that you MUST follow or every script run will fail.

## TL;DR — the working recipe

1. Write your script as a `.java` file in `/home/wasd/ghidra_scripts/` (see
   [anatomy](#anatomy-of-a-working-script)). Note: the project AGENTS.md refers
   to `ghidra_run_script_inline` / `run_script_inline` — **do not use them**,
   they are broken in this setup (see [why](#why-inline-scripts-are-broken)).
2. **Compile it manually with javac FIRST.** This is mandatory. Ghidra's script
   manager reports `class could not be found` for scripts it has never seen
   compile successfully, because its own build cache is poisoned by stale
   failed entries (see [the stale-cache problem](#the-stale-cache-problem)).
   ```bash
   GHIDRA=/home/wasd/Desktop/ghidra_12.1_PUBLIC
   CP=$(find $GHIDRA -name "*.jar" | tr '\n' ':')
   mkdir -p /tmp/opencode/classdir
   javac -proc:none -cp "$CP" -d /tmp/opencode/classdir <your_script>.java
   # compile exit: 0   <- required before you proceed
   ```
3. Run it through the MCP bridge:
   ```
   ghidra_run_ghidra_script(script_name="<your_script>.java", program="maniac.exe")
   ```
4. Expect the console output to start with a long error dump for two stale
   files (`McpInline_cdbb6984a57.java`, `McpInline_a558382203e.java`). This is
   **noise, not your failure** — your script still runs after that dump (see
   `WROTE ... rows to ...` / `SCRIPT COMPLETED SUCCESSFULLY`).

## Anatomy of a working script

```java
//@category Analysis
//@runtime Java

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import ghidra.program.model.mem.*;
import java.util.*;

public class MyScript extends GhidraScript {
    @Override
    public void run() throws Exception {
        // ... your analysis ...
        println("done");
    }
}
```

Rules that bite, learned the hard way:

- The public class name MUST match the file name (`MyScript.java` /
  `class MyScript`). This is why `run_script_inline` can never work here.
- **Never use `Instruction.getMnemonic()`** — it does not exist. Use
  `Instruction.getMnemonicString()`.
- **Never use `SymbolTable.getReferencesTo(Address)`** — it does not exist.
  Use `currentProgram.getReferenceManager().getReferencesTo(addr)` returning a
  `ReferenceIterator`.
- **`Memory.getBytes(Address, byte[])` returns `int`** (bytes read), it does
  NOT return the array. Check `== 4` etc. There is no `Memory.getInt(Address)`
  or `Memory.getFloat(Address)` — read 4 bytes and decode manually:
  ```java
  byte[] b = new byte[4];
  if (mem.getBytes(addr, b) == 4) {
      int ival = 0;
      for (int k = 0; k < 4; k++) ival |= (b[k] & 0xff) << (8 * k);
      float f = Float.intBitsToFloat(ival);
  }
  ```
- `FlatProgramAPI.getDataContaining(Listing, Address)` from the failed inline
  script is NOT a method — use `currentProgram.getListing().getDataContaining(addr)`.
- If your script is long, **write results to a file** instead of `println`.
  The MCP bridge truncates console output (768-row dumps get cut). Use:
  ```java
  java.io.File out = new java.io.File("/tmp/opencode/<name>.tsv");
  try (java.io.PrintWriter pw = new java.io.PrintWriter(out, "UTF-8")) { ... }
  println("WROTE " + out.getAbsolutePath());
  ```
  Then read /tmp/opencode/ with the Read/Grep tools.

## Why inline scripts are broken

`run_script_inline`/`run_script_inline` generates a file named
`McpInline_<hash>.java` with a body that does not match the class name Ghidra
expects, so Ghidra reports:

```
The class could not be found. It must be the public class of the .java file: McpInline_<hash>
```

Don't fight it. Use files + manual javac + `ghidra_run_ghidra_script`.

## The stale-cache problem

Ghidra's script-manager build cache lives at
`~/.config/ghidra/ghidra_12.1_PUBLIC/osgi/compiled-bundles/91bd1da7/`. Two
abandoned inline scripts are permanently recorded there as failing builds:

- `McpInline_cdbb6984a57.java` — Python-style body (`from ... import`, `for n
  in sorted(...)`) fed to the Java compiler.
- `McpInline_a558382203e.java` — uses `getDataContaining(Listing, Address)` and
  `SymbolTable.getReferencesTo(...)`.

Consequences:

- Every `ghidra_run_ghidra_script` invocation prints their full compile-error
  dump before your script's output. Ignore it — it is cosmetic.
- New scripts whose `.java` sources Ghidra has never seen compile cleanly will
  get `class could not be found`, because the poisoned cache makes the manager
  give up on the whole directory. This is why manual `javac` (step 2) is
  mandatory: once the class file exists on disk and the source compiles, the
  manager finds the compiled class and runs it.

Do NOT try to "fix" the cache by deleting files under
`~/.config/ghidra/ghidra_12.1_PUBLIC/` or editing them. It has not helped and
risks invalidating a working setup. Keep the manual-compile step instead.

## API quick reference (verified to work)

| What you want | Correct call |
|---|---|
| List of symbols | `currentProgram.getSymbolTable().getAllSymbols(true)` -> `SymbolIterator` |
| Global/primary symbol at an address | `listing.getDataAt(addr)`, symbol via `data.getPrimarySymbol()` |
| Xrefs to an address | `currentProgram.getReferenceManager().getReferencesTo(addr)` -> `ReferenceIterator` |
| Instruction at an address | `currentProgram.getListing().getInstructionAt(addr)` |
| Mnemonic of an instruction | `instruction.getMnemonicString()` |
| Function containing an address | `currentProgram.getFunctionManager().getFunctionContaining(addr)` |
| Read bytes | `currentProgram.getMemory().getBytes(addr, byte[])` -> int |
| Defined data containing an address | `currentProgram.getListing().getDataContaining(addr)` |
| Defined strings scan | `currentProgram.getListing().getDefinedData(true)` -> `DataIterator` |

## Typical workflow

1. Explore with `ghidra_*` tools (decompile, xrefs, list_data_items) to form
   a hypothesis.
2. Write a small script to enumerate/classify the targets (addresses, names,
   xref counts, byte values).
3. Manual javac compile -> run via `ghidra_run_ghidra_script` -> read the file
   it wrote under /tmp/opencode/.
4. Iterate on the script until the classification is clean.
5. Then apply changes (rename/type) — either via `ghidra_*` MCP tools (e.g.
   `rename_function_by_address`, `set_global`, `apply_data_type`) or a script
   that uses the program API.
6. `ghidra_save_program` after each chunk. Update `docs/` before moving on.