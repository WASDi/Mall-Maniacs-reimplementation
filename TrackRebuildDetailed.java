// TrackRebuildDetailed — merge of TrackRebuildProgress (tracked-function scope
// + rebuild status from @0x annotations), FindUndefinedTypes (undefined
// return/param types) and CompareCallGraph (source call graph) into one
// per-function report.
//
// The "tracked functions" are exactly the set TrackRebuildProgress keeps in
// scope: every non-external, non-thunk function outside the statically-linked
// CRT range 0x0043c850-0x0044966c, whose name does not begin with crt/unwind,
// does not contain cmd, and does not begin with one of the excluded prefixes
// (net, mnet, stateNet, stateNetwork, console, command, startServer, stateHost).
//
// For each tracked function (sorted alphabetically) the report gives:
//   - function name
//   - original address
//   - undefined = yes iff the Ghidra return type or any parameter type is
//     undefined (FindUndefinedTypes.isUndefinedType); this column is only
//     emitted when INCLUDE_UNDEF_COLUMN is true (default false)
//   - reimpl    = yes iff a @0x<addr> annotation for the function address
//     exists in src/*.c excluding stubs.c (same rule as TrackRebuildProgress);
//     "stub" when the function's body is a TODO stub defined in stubs.c
//     (stubs take precedence over the annotation rule); "no" otherwise
    //   - tracked-calls = X/Y for reimplemented (yes or stub) functions, where Y
    //     is the number of other tracked functions the function calls directly in
    //     Ghidra and X is how many of those the reimplementation actually calls
    //     (from the scanned call graph of src/*.c, excluding stubs.c and
    //     custom_helpers.c). X<Y means the reimplementation is missing calls the
    //     original makes. X is '?' when no scannable definition exists for the
    //     function (TODO stub bodies in stubs.c, or implementations living in
    //     custom_helpers.c). "-" for functions that are not reimplemented.
    //     When reimpl=yes and X<Y, the line ends with a "  missing:" list of the
    //     tracked-callee names that the reimplementation does not call.
    //     When reimpl=yes and the reimplementation calls other tracked functions
    //     the original does not, the line ends with a "  unexpected:" list (only
    //     calls to other tracked functions not called by the original are counted).
//
// Header summary:
//   - total tracked functions
//   - subset with undefined types (count + %)
//   - subset genuinely reimplemented (non-stub), subset stubbed, and the two
//     combined (count + % each)
//   - subset reimplemented AND calling the same set of tracked functions as in
//     Ghidra, i.e. complete coverage of the original's tracked callees
//     (count + %). Matches the "no missing calls" notion from CompareCallGraph
//     (extra source calls are not penalized except for tracked callees shown
//     as unexpected); unknown-callee functions (X='?') are not counted.
//   - subset reimplemented with unexpected calls to other tracked functions
//     (count + %).
//   - extra in rebuild: functions defined in src/*.c (excluding stubs.c) whose
//     name does not exist as a function in the original binary (any Ghidra
//     function, not just tracked). Listed in the "EXTRA IN REBUILD" section at
//     the end of the report with the source file for each.
//
// Run:  ghidra_run_ghidra_script(script_name="TrackRebuildDetailed.java",
//                                 program="maniac.exe")
// Results are written to /tmp/opencode/tracked-rebuild-detailed.txt.
//@author auto-ghidra
//@category Analysis
//@runtime Java

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Parameter;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Undefined;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;

public class TrackRebuildDetailed extends GhidraScript {
    private static final long CRT_START = 0x43c850L;
    private static final long CRT_END = 0x44966cL;
    private static final String DEFAULT_SOURCE_DIR = "/home/wasd/auto-ghidra/src";
    private static final String DEFAULT_OUTPUT = "/tmp/opencode/tracked-rebuild-detailed.txt";
    private static final String PROJECT_DIR = "/home/wasd/auto-ghidra";
    private static final Pattern ADDRESS = Pattern.compile("@0x([0-9a-fA-F]{1,8})\\b");

    // Whether each per-function line includes the "undef=" column.
    private static final boolean INCLUDE_UNDEF_COLUMN = false;

    // Function DEFINITION in stubs.c: `name(params)` followed by `{`.
    private static final Pattern STUB_DEF = Pattern.compile(
        "\\b([A-Za-z_][A-Za-z0-9_]*)\\s*\\([^;{}]*\\)\\s*\\{");

    // The call-graph scan does not read these sources: stubs.c is also skipped
    // for the annotation scan (matching TrackRebuildProgress); custom_helpers.c
    // holds debug helpers that have no counterpart in the original binary.
    private static final List<String> SCAN_EXCLUDE = Arrays.asList(
        "stubs.c", "custom_helpers.c"
    );

    // Keywords that may appear as `ident(` inside a function body but are not
    // calls.
    private static final Set<String> CALL_KEYWORDS = new HashSet<String>(Arrays.asList(
        "if", "for", "while", "switch", "return", "sizeof", "else", "do",
        "case", "default", "defined", "__attribute__", "__asm", "asm",
        "offsetof"
    ));

    private static final Pattern CALL_PATTERN =
        Pattern.compile("\\b([A-Za-z_]\\w*)\\s*\\(");

    private static final List<String> DEFAULT_EXCLUDED_PREFIXES = Arrays.asList(
        "net", "mnet", "stateNet", "stateNetwork", "console", "command",
        "startServer", "stateHost"
    );

    @Override
    public void run() throws Exception {
        List<String> sources = discoverSources();
        Set<Long> annotated = findAnnotatedAddresses(sources);

        Map<Long, Function> tracked = classifyFunctions();
        Set<Long> reimplemented = new TreeSet<Long>();
        for (Long address : annotated) {
            if (tracked.containsKey(address)) {
                reimplemented.add(address);
            }
        }

        Set<String> stubNames = findStubNames();

        Map<String, Set<String>> srcCalls = scanSourceCallGraph(sources);

        Map<String, String> srcDefinitions = findSourceDefinitions(sources);
        Set<String> ghidraNames = collectGhidraFunctionNames();

        List<String> report = buildReport(
            sources, tracked, reimplemented, stubNames, srcCalls,
            srcDefinitions, ghidraNames
        );
        writeReport(DEFAULT_OUTPUT, report);
        printSummary(tracked, reimplemented, report);
    }

    private void printSummary(Map<Long, Function> tracked, Set<Long> reimplemented,
                              List<String> report) {
        for (String line : report) {
            if (line.startsWith("=== ")) {
                println(line);
            }
        }
        println("WROTE " + DEFAULT_OUTPUT + "  (" + tracked.size() + " tracked functions)");
        println("SCRIPT COMPLETED SUCCESSFULLY");
    }

    /** Same in-scope classification as TrackRebuildProgress.classifyFunctions:
     *  address -> function, sorted by address. */
    private Map<Long, Function> classifyFunctions() {
        Map<Long, Function> result = new TreeMap<Long, Function>();
        for (Function function : currentProgram.getFunctionManager().getFunctions(true)) {
            if (exclusionReason(function) == null) {
                result.put(function.getEntryPoint().getOffset(), function);
            }
        }
        return result;
    }

    /** Returns the exclusion reason, or null when the function is in scope. */
    private String exclusionReason(Function function) {
        if (function.isExternal()) {
            return "external/imported";
        }
        if (function.isThunk()) {
            return "thunk/import stub";
        }

        long address = function.getEntryPoint().getOffset();
        if (address >= CRT_START && address <= CRT_END) {
            return "statically linked CRT range";
        }

        String name = function.getName();
        String lowerName = name.toLowerCase(Locale.ROOT);
        if (lowerName.startsWith("crt") || lowerName.startsWith("unwind") || lowerName.contains("cmd")) {
            return "system/console support by name";
        }
        for (String prefix : DEFAULT_EXCLUDED_PREFIXES) {
            if (!prefix.isEmpty() && lowerName.startsWith(prefix.toLowerCase(Locale.ROOT))) {
                return "out of scope prefix " + prefix;
            }
        }
        return null;
    }

    /** True when the function's Ghidra return type or any parameter type is
     *  undefined (FindUndefinedTypes.isUndefinedType semantics). */
    private boolean hasUndefinedTypes(Function function) {
        if (isUndefinedType(function.getReturnType())) {
            return true;
        }
        for (Parameter parameter : function.getParameters()) {
            if (isUndefinedType(parameter.getDataType())) {
                return true;
            }
        }
        return false;
    }

    private boolean isUndefinedType(DataType t) {
        if (t instanceof Pointer) {
            Pointer p = (Pointer) t;
            DataType bt = p.getDataType();
            if (bt instanceof Undefined) return true;
            return bt != null && bt.getName().startsWith("undefined");
        }
        if (t instanceof Undefined) return true;
        String n = t.getName();
        if (n == null) return false;
        return n.startsWith("undefined");
    }

    /** Set of other tracked functions the given function calls directly in
     *  Ghidra, resolved through thunks, keyed by callee name. */
    private Set<String> trackedGhidraCallees(Function function, Map<Long, Function> tracked) {
        Set<String> callees = new TreeSet<String>();
        for (Function callee : function.getCalledFunctions(monitor)) {
            Function resolved = resolveThunk(callee);
            if (resolved != null && tracked.containsKey(resolved.getEntryPoint().getOffset())) {
                callees.add(resolved.getName());
            }
        }
        return callees;
    }

    private List<String> buildReport(
        List<String> sourcePaths,
        Map<Long, Function> tracked,
        Set<Long> reimplemented,
        Set<String> stubNames,
        Map<String, Set<String>> srcCalls,
        Map<String, String> srcDefinitions,
        Set<String> ghidraNames
    ) {
        int total = tracked.size();
        int undefinedCount = 0;
        int reimplCount = 0;
        int stubCount = 0;
        int sameCallsCount = 0;
        int unknownCallCount = 0;
        int unexpectedCount = 0;

        Set<String> trackedNames = new TreeSet<String>();
        for (Function trackedFunction : tracked.values()) {
            trackedNames.add(trackedFunction.getName());
        }

        List<String> lines = new ArrayList<String>();
        lines.add("Mall Maniacs rebuild progress — detailed tracked functions");
        lines.add("Program: " + currentProgram.getName());
        lines.add("Sources:");
        for (String sourcePath : sourcePaths) {
            lines.add("  " + sourcePath);
        }
        lines.add("Scope exclusions: external/imported, thunks/import stubs, CRT range "
            + hex(CRT_START) + "-" + hex(CRT_END)
            + ", names beginning crt/unwind, names containing cmd, prefixes "
            + join(DEFAULT_EXCLUDED_PREFIXES));
        lines.add("");

        List<Function> funcs = new ArrayList<Function>(tracked.values());
        Collections.sort(funcs, Comparator.comparing(Function::getName, String.CASE_INSENSITIVE_ORDER));

        for (Function function : funcs) {
            long address = function.getEntryPoint().getOffset();
            String name = function.getName();

            boolean undefined = hasUndefinedTypes(function);
            boolean stub = stubNames.contains(name);
            boolean reimpl = reimplemented.contains(address);
            String reimplStatus = stub ? "stub" : (reimpl ? "yes" : "no");
            if (undefined) undefinedCount++;
            if (stub) stubCount++;
            else if (reimpl) reimplCount++;

            String callInfo;
            List<String> missing = null;
            List<String> unexpected = null;
            if (!reimplStatus.equals("no")) {
                Set<String> ghidraCallees = trackedGhidraCallees(function, tracked);
                int y = ghidraCallees.size();
                Set<String> srcCallees = srcCalls.get(name);
                if (srcCallees == null) {
                    unknownCallCount++;
                    callInfo = "?/" + y;
                } else {
                    int x = 0;
                    List<String> missingCallees = new ArrayList<String>();
                    for (String calleeName : ghidraCallees) {
                        if (srcCallees.contains(calleeName)) x++;
                        else missingCallees.add(calleeName);
                    }
                    if (x == y) sameCallsCount++;
                    callInfo = x + "/" + y;
                    if (reimplStatus.equals("yes") && x != y) {
                        missing = missingCallees;
                    }
                    if (reimplStatus.equals("yes")) {
                        List<String> unexpectedCallees = new ArrayList<String>();
                        for (String calleeName : srcCallees) {
                            if (!calleeName.equals(name)
                                    && trackedNames.contains(calleeName)
                                    && !ghidraCallees.contains(calleeName)) {
                                unexpectedCallees.add(calleeName);
                            }
                        }
                        if (!unexpectedCallees.isEmpty()) {
                            Collections.sort(unexpectedCallees,
                                    String.CASE_INSENSITIVE_ORDER);
                            unexpected = unexpectedCallees;
                            unexpectedCount++;
                        }
                    }
                }
            } else {
                callInfo = "-";
            }

            String line;
            if (INCLUDE_UNDEF_COLUMN) {
                line = String.format(Locale.ROOT, "%-40s %s  undef=%-3s  reimpl=%-4s  tracked-calls=%s",
                    name, hex(address), yesno(undefined), reimplStatus, callInfo);
            } else {
                line = String.format(Locale.ROOT, "%-40s %s  reimpl=%-4s  tracked-calls=%s",
                    name, hex(address), reimplStatus, callInfo);
            }
            if (missing != null && !missing.isEmpty()) {
                line += "  missing: " + String.join(",", missing);
            }
            if (unexpected != null && !unexpected.isEmpty()) {
                line += "  unexpected: " + String.join(",", unexpected);
            }
            lines.add(line);
        }

        lines.add("");
        lines.add("=== SUMMARY ===");
        lines.add("Total tracked functions: " + total);
        lines.add("With undefined return/param types: " + undefinedCount
            + " (" + pct(undefinedCount, total) + ")");
        lines.add("Reimplemented (non-stub): " + reimplCount + " (" + pct(reimplCount, total) + ")");
        lines.add("Stubbed: " + stubCount + " (" + pct(stubCount, total) + ")");
        lines.add("Reimplemented or stubbed: " + (reimplCount + stubCount)
            + " (" + pct(reimplCount + stubCount, total) + ")");
        lines.add("Reimplemented and calls the same tracked functions as ghidra: "
            + sameCallsCount + " (" + pct(sameCallsCount, total) + ")");
        lines.add("Reimplemented with unexpected calls to other tracked functions: "
            + unexpectedCount + " (" + pct(unexpectedCount, total) + ")");
        if (unknownCallCount > 0) {
            lines.add("(reimplemented/stub functions without a scannable source definition "
                + "(stub bodies in stubs.c, or implementations in custom_helpers.c) excluded "
                + "from the same-calls/unexpected counts: " + unknownCallCount + ")");
        }

        // --- Extra in rebuild: defined in src but not in Ghidra ---
        // srcDefinitions is every function definition found in src/*.c (excluding
        // stubs.c, including custom_helpers.c); ghidraNames is every function
        // present in the original binary. An entry here means the rebuild
        // introduces a new function with no counterpart in the original.
        List<String> extra = new ArrayList<String>();
        for (String srcName : srcDefinitions.keySet()) {
            if (!ghidraNames.contains(srcName)) {
                extra.add(srcName);
            }
        }
        Collections.sort(extra, String.CASE_INSENSITIVE_ORDER);
        int extraCount = extra.size();
        int srcDefCount = srcDefinitions.size();
        lines.add("Source definitions in src/*.c (excluding stubs.c): " + srcDefCount);
        lines.add("Extra in rebuild (defined in src but not in original binary): "
            + extraCount + " (" + pct(extraCount, srcDefCount) + " of src defs)");
        lines.add("");
        lines.add("=== EXTRA IN REBUILD — defined in src but not in Ghidra (" + extraCount + ") ===");
        if (extraCount == 0) {
            lines.add("(none)");
        } else {
            for (String name : extra) {
                String srcFile = srcDefinitions.get(name);
                // Show relative path when possible for readability
                String rel = srcFile;
                if (srcFile.startsWith(DEFAULT_SOURCE_DIR)) {
                    rel = "src/" + srcFile.substring(DEFAULT_SOURCE_DIR.length() + 1);
                } else if (srcFile.contains("/src/")) {
                    rel = "src/" + srcFile.substring(srcFile.lastIndexOf("/src/") + 5);
                }
                lines.add(String.format(Locale.ROOT, "%-40s [%s]", name, rel));
            }
        }
        return lines;
    }

    private String pct(int part, int whole) {
        return String.format(Locale.ROOT, "%.1f%%", percent(part, whole));
    }

    private double percent(int part, int whole) {
        return whole == 0 ? 0.0 : (part * 100.0) / whole;
    }

    private String yesno(boolean value) {
        return value ? "yes" : "no";
    }

    /** Names of functions whose body is a TODO stub in src/stubs.c. */
    private Set<String> findStubNames() throws Exception {
        Set<String> names = new HashSet<String>();
        File stubs = new File(DEFAULT_SOURCE_DIR, "stubs.c");
        if (!stubs.isFile()) {
            return names;
        }
        String text = new String(Files.readAllBytes(stubs.toPath()), StandardCharsets.UTF_8);
        Matcher matcher = STUB_DEF.matcher(text);
        while (matcher.find()) {
            names.add(matcher.group(1));
        }
        return names;
    }

    private Set<Long> findAnnotatedAddresses(List<String> sourcePaths) throws Exception {
        Set<Long> addresses = new HashSet<Long>();
        for (String sourcePath : sourcePaths) {
            File source = new File(sourcePath);
            if (!source.isFile()) {
                throw new IllegalArgumentException("Source file does not exist: "
                    + source.getAbsolutePath());
            }
            String sourceText = new String(
                Files.readAllBytes(source.toPath()), StandardCharsets.UTF_8
            );
            Matcher matcher = ADDRESS.matcher(sourceText);
            while (matcher.find()) {
                addresses.add(Long.parseLong(matcher.group(1), 16));
            }
        }
        return addresses;
    }

    private void writeReport(String outputPath, List<String> lines) throws Exception {
        File output = new File(outputPath);
        File parent = output.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalArgumentException("Could not create report directory: " + parent);
        }
        PrintWriter writer = new PrintWriter(output, StandardCharsets.UTF_8.name());
        try {
            for (String line : lines) {
                writer.println(line);
            }
        } finally {
            writer.close();
        }
    }

    private List<String> discoverSources() throws Exception {
        File sourceDirectory = new File(DEFAULT_SOURCE_DIR);
        if (!sourceDirectory.isDirectory()) {
            throw new IllegalArgumentException("Source directory does not exist: "
                + sourceDirectory.getAbsolutePath());
        }
        List<String> sources = new ArrayList<String>();
        try (Stream<Path> paths = Files.walk(Paths.get(DEFAULT_SOURCE_DIR))) {
            java.util.Iterator<Path> iterator = paths
                .filter(Files::isRegularFile)
                .filter(path -> path.toString().toLowerCase(Locale.ROOT).endsWith(".c"))
                .filter(path -> !new File(path.toString()).getName().equalsIgnoreCase("stubs.c"))
                .sorted()
                .iterator();
            while (iterator.hasNext()) {
                sources.add(iterator.next().toFile().getAbsolutePath());
            }
        }
        if (sources.isEmpty()) {
            throw new IllegalArgumentException("No .c source files found under "
                + sourceDirectory.getAbsolutePath());
        }
        return sources;
    }

    /** Blank out comments and string/char literals (replacing their contents
     *  with spaces, newlines preserved) so that brace/paren matching and call
     *  extraction never see them. */
    private String stripCommentsAndLiterals(String text) {
        StringBuilder out = new StringBuilder(text.length());
        int i = 0;
        int n = text.length();
        while (i < n) {
            char c = text.charAt(i);
            if (c == '/' && i + 1 < n && text.charAt(i + 1) == '/') {
                while (i < n && text.charAt(i) != '\n') {
                    out.append(' ');
                    i++;
                }
            } else if (c == '/' && i + 1 < n && text.charAt(i + 1) == '*') {
                out.append("  ");
                i += 2;
                while (i < n) {
                    if (text.charAt(i) == '*' && i + 1 < n && text.charAt(i + 1) == '/') {
                        out.append("  ");
                        i += 2;
                        break;
                    }
                    out.append(text.charAt(i) == '\n' ? '\n' : ' ');
                    i++;
                }
            } else if (c == '"' || c == '\'') {
                char quote = c;
                out.append(' ');
                i++;
                while (i < n && text.charAt(i) != quote) {
                    if (text.charAt(i) == '\\' && i + 1 < n) {
                        out.append(' ');
                        i++;
                    }
                    out.append(text.charAt(i) == '\n' ? '\n' : ' ');
                    i++;
                }
                if (i < n) {
                    out.append(' ');
                    i++;
                }
            } else {
                out.append(c);
                i++;
            }
        }
        return out.toString();
    }

    /** Returns the index just past the bracket matching the one at {@code open}
     *  ('(' or '{'), or -1 when unbalanced within the text. */
    private int matchBracket(String text, int open) {
        char openCh = text.charAt(open);
        char closeCh = openCh == '(' ? ')' : '}';
        int depth = 0;
        for (int i = open; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == openCh) {
                depth++;
            } else if (c == closeCh) {
                depth--;
                if (depth == 0) {
                    return i + 1;
                }
            }
        }
        return -1;
    }

    /** Scan src/*.c (excluding SCAN_EXCLUDE files) and build a map of function
     *  name -> set of directly called function names. Definitions are found by
     *  walking top-level `ident(...)` groups followed by `{`; calls are
     *  identifiers followed by `(` inside the body. This replaces the previous
     *  cflow-based graph: GNU cflow 1.7 silently drops or truncates mutually
     *  recursive functions (e.g. stateCharacterSelect/stateCharSelectOk), which
     *   produced false "missing:" callees in the report. */
    private Map<String, Set<String>> scanSourceCallGraph(List<String> sourcePaths)
            throws Exception {
        Map<String, Set<String>> calls = new LinkedHashMap<String, Set<String>>();
        for (String sourcePath : sourcePaths) {
            File source = new File(sourcePath);
            if (SCAN_EXCLUDE.contains(source.getName())) {
                continue;
            }
            String text = stripCommentsAndLiterals(
                new String(Files.readAllBytes(source.toPath()), StandardCharsets.UTF_8)
            );
            int length = text.length();
            int i = 0;
            while (i < length) {
                char c = text.charAt(i);
                if (!Character.isJavaIdentifierStart(c)) {
                    i++;
                    continue;
                }
                int nameEnd = i;
                while (nameEnd < length && Character.isJavaIdentifierPart(text.charAt(nameEnd))) {
                    nameEnd++;
                }
                String ident = text.substring(i, nameEnd);
                int paren = nameEnd;
                while (paren < length && Character.isWhitespace(text.charAt(paren))) {
                    paren++;
                }
                if (paren < length && text.charAt(paren) == '(') {
                    int close = matchBracket(text, paren);
                    if (close > 0) {
                        int brace = close;
                        while (brace < length && Character.isWhitespace(text.charAt(brace))) {
                            brace++;
                        }
                        if (brace < length && text.charAt(brace) == '{') {
                            int bodyEnd = matchBracket(text, brace);
                            Set<String> calleeSet =
                                calls.computeIfAbsent(ident, k -> new LinkedHashSet<String>());
                            Matcher matcher = CALL_PATTERN.matcher(
                                text.substring(brace, bodyEnd > 0 ? bodyEnd : length));
                            while (matcher.find()) {
                                String callee = matcher.group(1);
                                if (!CALL_KEYWORDS.contains(callee)) {
                                    calleeSet.add(callee);
                                }
                            }
                            i = bodyEnd > 0 ? bodyEnd : length;
                            continue;
                        }
                    }
                }
                i = nameEnd;
            }
        }
        return calls;
    }

    /** Collect every function name present in the original binary (Ghidra). */
    private Set<String> collectGhidraFunctionNames() {
        Set<String> names = new HashSet<String>();
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            names.add(fn.getName());
        }
        return names;
    }

    /** Scan src/*.c (via sourcePaths, which already excludes stubs.c) and
     *  return a map of function definition name -> absolute source file.
     *  Unlike scanSourceCallGraph this includes custom_helpers.c so that
     *  helpers intentionally absent from the original are visible as "extra".
     *  Parsing is the same ident '(' ... '{' walk after stripping comments
     *  and literals, so the definition set is consistent with the call graph. */
    private Map<String, String> findSourceDefinitions(List<String> sourcePaths)
            throws Exception {
        Map<String, String> defs = new TreeMap<String, String>(String.CASE_INSENSITIVE_ORDER);
        for (String sourcePath : sourcePaths) {
            File source = new File(sourcePath);
            // sourcePaths already excludes stubs.c. custom_helpers.c is the
            // documented home for rebuild-only helpers (appLog, setSignVerts,
            // etc.), so its definitions are intentionally NOT flagged as
            // "extra in rebuild".
            if (SCAN_EXCLUDE.contains(source.getName())) {
                continue;
            }
            String text = stripCommentsAndLiterals(
                new String(Files.readAllBytes(source.toPath()), StandardCharsets.UTF_8)
            );
            int length = text.length();
            int i = 0;
            while (i < length) {
                char c = text.charAt(i);
                if (!Character.isJavaIdentifierStart(c)) {
                    i++;
                    continue;
                }
                int nameEnd = i;
                while (nameEnd < length && Character.isJavaIdentifierPart(text.charAt(nameEnd))) {
                    nameEnd++;
                }
                String ident = text.substring(i, nameEnd);
                int paren = nameEnd;
                while (paren < length && Character.isWhitespace(text.charAt(paren))) {
                    paren++;
                }
                if (paren < length && text.charAt(paren) == '(') {
                    int close = matchBracket(text, paren);
                    if (close > 0) {
                        int brace = close;
                        while (brace < length && Character.isWhitespace(text.charAt(brace))) {
                            brace++;
                        }
                        if (brace < length && text.charAt(brace) == '{') {
                            int bodyEnd = matchBracket(text, brace);
                            // Record definition (first file wins if duplicate)
                            if (!defs.containsKey(ident)) {
                                defs.put(ident, source.getAbsolutePath());
                            }
                            i = bodyEnd > 0 ? bodyEnd : length;
                            continue;
                        }
                    }
                }
                i = nameEnd;
            }
        }
        return defs;
    }

    /** Follow thunks to the real function (import thunk -> external function). */
    private Function resolveThunk(Function f) {
        int guard = 0;
        while (f != null && f.isThunk() && guard++ < 16) {
            Function t = f.getThunkedFunction(true);
            if (t == null || t.equals(f)) break;
            f = t;
        }
        return f;
    }

    private String join(List<String> values) {
        StringBuilder result = new StringBuilder();
        for (String value : values) {
            if (result.length() > 0) {
                result.append(",");
            }
            result.append(value);
        }
        return result.toString();
    }

    private String hex(long address) {
        return String.format(Locale.ROOT, "0x%08x", address);
    }
}
