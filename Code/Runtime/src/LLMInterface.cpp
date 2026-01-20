#include "LlmInterface.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include "Logger.h"

namespace fs = std::filesystem;

bool LlmInterface::generateDebugPlan(
    const std::string& artifactsRoot,
    const std::string& sampleName,
    const std::string& targetPath,
    std::string& errorOut)
{
    errorOut.clear();

    Logger::info("[LlmInterface] Generating debug plan for sample: " + sampleName);

    // 1) Collect static artifacts
    StaticArtifacts artifacts;
    if (!collectStaticArtifacts(artifactsRoot, sampleName, artifacts, errorOut)) {
        return false;
    }

    // 2) Build LLM directory structure
    const fs::path llmDir = fs::path(artifactsRoot) / "LLM";
    const fs::path debugDir = fs::path(artifactsRoot) / "debug";
    fs::create_directories(llmDir);
    fs::create_directories(debugDir);

    // 3) Build prompt for GDB script generation (NEW APPROACH)
    const std::string prompt = buildGdbScriptPrompt(artifacts, targetPath);
    const std::string promptPath = (llmDir / "prompt.txt").string();
    if (!writePromptToFile(prompt, promptPath, errorOut)) {
        return false;
    }

    Logger::info("[LlmInterface] Prompt written to: " + promptPath);

    // 4) Invoke LLM to generate plan.gdb directly (NEW APPROACH)
    const std::string gdbScriptPath = (debugDir / "plan.gdb").string();
    if (!invokeLlmForGdbScript(promptPath, gdbScriptPath, errorOut)) {
        return false;
    }

    Logger::info("[LlmInterface] GDB script generated: " + gdbScriptPath);
    return true;
    
    /* OLD APPROACH (JSON-based): commented out for potential reversion
    // 3) Build prompt
    const std::string prompt = buildPrompt(artifacts, targetPath);
    const std::string promptPath = (llmDir / "prompt.txt").string();
    if (!writePromptToFile(prompt, promptPath, errorOut)) {
        return false;
    }

    Logger::info("[LlmInterface] Prompt written to: " + promptPath);

    // 4) Invoke LLM to generate plan.json
    const std::string planJsonPath = (llmDir / "plan.json").string();
    if (!invokeLlmForPlan(promptPath, planJsonPath, errorOut)) {
        return false;
    }

    Logger::info("[LlmInterface] Debug plan generated: " + planJsonPath);
    return true;
    */
}

bool LlmInterface::collectStaticArtifacts(
    const std::string& artifactsRoot,
    const std::string& sampleName,
    StaticArtifacts& outArtifacts,
    std::string& errorOut)
{
    errorOut.clear();
    outArtifacts = StaticArtifacts{};
    outArtifacts.sampleName = sampleName;

    fs::path base(artifactsRoot);
    fs::path staticDir = base / "static";
    fs::path baselineDir = base / "baseline";

    // Read SHA256
    const fs::path sha256Path = staticDir / (sampleName + ".sha256");
    if (fs::exists(sha256Path)) {
        std::string sha256Content = readFileContent(sha256Path.string());
        // Extract just the hash (before space)
        size_t spacePos = sha256Content.find(' ');
        if (spacePos != std::string::npos) {
            outArtifacts.sha256 = sha256Content.substr(0, spacePos);
        } else {
            outArtifacts.sha256 = sha256Content;
        }
    }

    // Read objdump (truncated to 5000 lines for LLM)
    const fs::path objdumpPath = staticDir / (sampleName + ".objdump");
    if (fs::exists(objdumpPath)) {
        outArtifacts.objdumpContent = truncateContent(readFileContent(objdumpPath.string()), 5000);
        Logger::info("[LlmInterface] Loaded objdump from: " + objdumpPath.string());
    } else {
        Logger::warn("[LlmInterface] objdump file not found: " + objdumpPath.string());
    }

    // Read strings (already limited to 2000 lines when generated)
    const fs::path stringsPath = staticDir / (sampleName + ".strings");
    if (fs::exists(stringsPath)) {
        outArtifacts.stringsContent = readFileContent(stringsPath.string());
    } else {
        Logger::warn("[LlmInterface] strings file not found: " + stringsPath.string());
    }

    // Read readelf
    const fs::path readelfPath = staticDir / (sampleName + ".readelf");
    if (fs::exists(readelfPath)) {
        outArtifacts.readelfContent = truncateContent(readFileContent(readelfPath.string()), 3000);
    } else {
        Logger::warn("[LlmInterface] readelf file not found: " + readelfPath.string());
    }

    // Read fileinfo
    const fs::path fileinfoPath = staticDir / (sampleName + ".fileinfo");
    if (fs::exists(fileinfoPath)) {
        outArtifacts.fileinfoContent = readFileContent(fileinfoPath.string());
    }

    // Read manifest diff (optional)
    const fs::path manifestDiffPath = baselineDir / "manifest_diff.txt";
    if (fs::exists(manifestDiffPath)) {
        outArtifacts.manifestDiffContent = truncateContent(readFileContent(manifestDiffPath.string()), 500);
    }

    return true;
}

/* OLD APPROACH: Build a prompt string from collected artifacts through JSON plan generation
std::string LlmInterface::buildPrompt(const LlmInterface::StaticArtifacts& artifacts, const std::string& targetPath)
{
    std::ostringstream ss;

    ss << "You are an AI malware analysis assistant. Analyze the following static and dynamic analysis artifacts to create a GDB debug plan.\n\n";

    ss << "## Sample Information\n";
    ss << "- Sample Name: " << artifacts.sampleName << "\n";
    if (!artifacts.sha256.empty()) {
        ss << "- SHA256: " << artifacts.sha256 << "\n";
    }
    ss << "\n";

    if (!artifacts.fileinfoContent.empty()) {
        ss << "## File Information\n";
        ss << "```\n";
        ss << artifacts.fileinfoContent;
        ss << "```\n\n";
    }

    if (!artifacts.readelfContent.empty()) {
        ss << "## ReadELF Output\n";
        ss << "```\n";
        ss << artifacts.readelfContent;
        ss << "```\n\n";
    }

    if (!artifacts.stringsContent.empty()) {
        ss << "## Strings (first 2000 lines)\n";
        ss << "```\n";
        ss << artifacts.stringsContent;
        ss << "```\n\n";
    }

    if (!artifacts.objdumpContent.empty()) {
        ss << "## Objdump (first 5000 lines)\n";
        ss << "```\n";
        ss << artifacts.objdumpContent;
        ss << "```\n\n";
    }

    if (!artifacts.manifestDiffContent.empty()) {
        ss << "## Baseline Diff (file system changes)\n";
        ss << "```\n";
        ss << artifacts.manifestDiffContent;
        ss << "```\n\n";
    }

    ss << "## Output Format\n";
    ss << "Generate a JSON debug plan with the following structure:\n\n";
    ss << "```json\n";
    ss << "{\n";
    ss << "  \"targetPath\": \"" << targetPath << "\",\n";
    ss << "  \"useStarti\": true,\n";
    ss << "  \"breakpoints\": [\n";
    ss << "    {\n";
    ss << "      \"offset\": \"<hex_offset>\",\n";
    ss << "      \"note\": \"<description>\",\n";
    ss << "      \"stopAfter\": false,\n";
    ss << "      \"actions\": [\n";
    ss << "        {\n";
    ss << "          \"type\": \"set_reg\",\n";
    ss << "          \"reg\": \"<register_name>\",\n";
    ss << "          \"value\": \"<hex_or_decimal_value>\"\n";
    ss << "        },\n";
    ss << "        {\n";
    ss << "          \"type\": \"snapshot\",\n";
    ss << "          \"label\": \"<label>\"\n";
    ss << "        },\n";
    ss << "                {\n";
    ss << "          \"type\": \"gdb_cmd\",\n";
    ss << "          \"command\": \"<gdb_command>\"\n";
    ss << "        },\n";
    ss << "        {\n";
    ss << "          \"type\": \"shell\",\n";
    ss << "          \"command\": \"<bash_command>\"\n";
    ss << "        },\n";
    ss << "        {\n";
    ss << "          \"type\": \"temp_breakpoint\",\n";
    ss << "          \"offset\": \"<hex_offset>\",\n";
    ss << "          \"note\": \"<description>\",\n";
    ss << "          \"deleteAfterHit\": true,\n";
    ss << "          \"actions\": [\n";
    ss << "            {\n";
    ss << "              \"type\": \"snapshot\",\n";
    ss << "              \"label\": \"<label>\"\n";
    ss << "            },\n";
    ss << "            {\n";
    ss << "              \"type\": \"gdb_cmd\",\n";
    ss << "              \"command\": \"<gdb_command>\"\n";
    ss << "            }\n";
    ss << "          ]\n";
    ss << "        }\n";
    ss << "      ]\n";
    ss << "    },\n";
    ss << "    {\n";
    ss << "      \"offset\": \"<hex_offset>\",\n";
    ss << "      \"note\": \"<description>\",\n";
    ss << "      \"stopAfter\": true,\n";
    ss << "      \"actions\": [\n";
    ss << "        {\n";
    ss << "          \"type\": \"shell\",\n";
    ss << "          \"command\": \"<bash_command>\"\n";
    ss << "        },\n";
    ss << "        {\n";
    ss << "          \"type\": \"snapshot\",\n";
    ss << "          \"label\": \"<label>\"\n";
    ss << "        }\n";
    ss << "      ]\n";
    ss << "    }\n";
    ss << "  ]\n";
    ss << "}\n";
    ss << "```\n\n";

    ss << "## Requirements\n";
    ss << "1. Identify the entry point (usually _start) and main execution flow\n";
    ss << "2. Set breakpoints at critical system calls (execve, ptrace, memfd_create, mmap, mprotect, dlopen, dlsym)\n";
    ss << "3. Add anti-debugging bypasses (set registers to bypass ptrace checks, timing checks, etc.)\n";
    ss << "4. CRITICAL: Look for setjmp/longjmp anti-debugging patterns:\n";
    ss << "   - Calls to _setjmp() or setjmp() followed by test eax, eax\n";
    ss << "   - If eax == 0, the code may write to NULL (0x0) causing SIGSEGV\n";
    ss << "   - Set breakpoint BEFORE the test instruction and set eax to non-zero (e.g., 1) to bypass\n";
    ss << "   - Look for: call _setjmp -> test eax,eax -> je/jne -> mov [0x0], ...\n";
    ss << "   - AFTER bypassing setjmp, look for subsequent checks that read memory values set before setjmp\n";
    ss << "   - If a memory location is set to 0 before setjmp and then tested after, set breakpoint at the test\n";
    ss << "   - Modify the memory value or register to bypass the check (e.g., set memory to 1 or set eax to 0)\n";
    ss << "   - CRITICAL: After setjmp bypass, look for instructions that READ from memory locations that were set to 0 before setjmp (e.g., \"mov eax, [rip+offset]\" followed by \"test eax,eax\" and \"sete al\"). If the memory is still 0, the program will exit with code 1. Set a breakpoint at the MOV instruction that reads the memory, and either:\n";
    ss << "     a) Set the memory location to a non-zero value (e.g., \"set {int}($base + 0x230e8) = 1\")\n";
    ss << "     b) OR set eax to a non-zero value after the MOV but before the TEST (e.g., \"set $eax = 1\")\n";
    ss << "   - The memory check typically appears shortly after the setjmp test bypass, often within the same function\n";
	ss << "   - Ensure to capture breakpoints for unusual anti-debugging flows, including tracing attempts, time checks, use of syscalls (such as `/proc/self/status` or `ps -aux`-style commands)\n";
    ss << "   - CRITICAL: Ensure for ALL of these patterns, registers and flags are set correctly after the method returns to ensure proper jumps are taken to avoid stack cannaries, bad instructions, and other exit lines\n\n";
    ss << "5. Capture file operations (especially memfd files - use shell commands to copy /proc/$pid/fd/3 to /tmp/memfd_dump.bin)\n";
    ss << "6. Add snapshots at key decision points\n";
    ss << "7. Use stopAfter: true for the final breakpoint (usually execve) to prevent execution\n";
    ss << "8. **CRITICAL - Address/Offset Handling:**\n";
    ss << "   - For **PIE (Position Independent Executable) binaries** (file info shows \"dynamically linked\" or readelf shows \"Type: DYN\"): Use RELATIVE OFFSETS from objdump. For example, if objdump shows \"0x12b0 <main>\", use offset \"0x12b0\". The GDB script will add these to the runtime base address.\n";
    ss << "   - For **non-PIE/statically-linked binaries** (file info shows \"statically linked\" or readelf shows \"Type: EXEC\"): Use ABSOLUTE ADDRESSES from objdump AS-IS. For example, if objdump shows \"0x401600 <_start>\", use offset \"0x401600\". The GDB script will detect this is a non-PIE binary and set base=0, so these addresses will be used directly.\n";
    ss << "   - **Summary**: Always copy the hex address from objdump into the \"offset\" field, regardless of binary type. The GDB script handles PIE vs non-PIE automatically.\n";
    ss << "   - All offsets/addresses should be in hex format (e.g., \"0x32b0\" or \"0x401600\")\n";
    ss << "   - All register names should be full 64-bit registers in LOWERCASE (e.g. rax, not RAX, EAX, or AX) - GDB requires lowercase register names\n";
	ss << "   - Important: If the binary is non-PIE/statically-linked, do NOT include any breakpoints in the libray code (e.g., libc functions like malloc, free, etc), only in the main binary code. We know what the libraries do, we are debugging the binary itself.\n";
    ss << "9. Replace targetPath with the actual target path: " << targetPath << "\n";
    ss << "10. For GDB commands: To get PID, use the $pid variable (already set): printf \"PID=%d\\\\n\", $pid\n";
    ss << "11. Do NOT use getpid() function in GDB commands - use the $pid variable instead\n\n";
    ss << "12. For any bypass that modifies a register to affect a nearby conditional branch (e.g., test reg,reg followed by jz/jnz/je/jne), include at least one confirmation action: temporary breakpoint on the next instruction (provided as \"$base + <offset value>\", such as \"$base + 0x1234\"), and add separate gdb_cmd actions for \"x/i $pc\" and \"info registers eflags\" (DO NOT use semicolons to chain commands - use separate gdb_cmd actions). If the branch target address is known, also add a second breakpoint at the branch target to confirm the taken path. Afterwards, ensure that the script continues the program.\n";
    ss << "13. IMPORTANT: GDB command blocks do NOT support semicolons to chain commands. Each command must be a separate gdb_cmd action. For example, instead of \"x/i $pc; info registers eflags\", use two separate gdb_cmd actions: one with \"x/i $pc\" and another with \"info registers eflags\".\n";
    ss << "14. IMPORTANT: Also watch for jumps into stack fails, ie \"jne 0x1234\" then at 0x1234 flows into a call to \"__stack_chk_fail@plt\", stack cannaries, signal actions (such as SIGTRAP, SIGKILL, SIGSEGV, etc), and other code that can cause premature termination of the binary. These must also be avoided with breakpoints and register/flag changes\n";
	ss << "15. IMPORTANT: Any portion of code that performsencryption and/or decyption of data or code sections must be captured with breakpoints and snapshots to allow for memory dumps of decrypted content. If possible, also identify the encryption method used if standard, and add it as a note to the action for that location.\n";
	ss << "16. CRITICAL: If the binary expects additional arguments, environment variables, or specific file system state (files present, specific contents, etc), these must be identified and included in the plan as notes or shell commands to set up the environment prior to execution. Failure to do so may result in improper execution flow and failure to reach key code paths. If the exact information is not readily available, use dummy values and attempt to manipulate the run of the program as needed (such as register value and/or flag manipulation, directly calling functions with different values, etc.).\n";
    ss << "17. CRITICAL: ALL breakpoint addresses in gdb_cmd actions MUST use the format \"*($base + <offset>)\" where offset is a hex value like 0x3670. NEVER use absolute addresses like \"*0x3670\" in breakpoint commands. The base address is loaded dynamically and stored in $base. Example: use \"break *($base + 0x3670)\" NOT \"break *0x3670\". This applies to break, tbreak, and hbreak commands.\n\n";
    ss << "18. CRITICAL: Watch for anti-disassembly. If there are errant or wayward bytes that make instructions not make sense, bypass them and/or remove them, and see if the resultant instruction makes sense and flows properly.";
    ss << "## VITAL ##\n";
    ss << "Generate ONLY the JSON plan, no markdown, no explanations, just valid JSON.";

    return ss.str();
}*/

// NEW APPROACH: Build a prompt for LLM to generate GDB script directly
// This replaces the JSON → GDB script translation layer
std::string LlmInterface::buildGdbScriptPrompt(const StaticArtifacts& artifacts, const std::string& targetPath)
{
    std::ostringstream ss;

    ss << "You are an AI malware analysis assistant. Analyze the following static and dynamic analysis artifacts to create a GDB debugging script.\n\n";
    ss << "**IMPORTANT**: You must generate a complete, ready-to-use GDB script that can be executed directly with `gdb -x script.gdb`.\n\n";

    ss << "## Sample Information\n";
    ss << "- Sample Name: " << artifacts.sampleName << "\n";
    ss << "- Target Path: " << targetPath << "\n";
    if (!artifacts.sha256.empty()) {
        ss << "- SHA256: " << artifacts.sha256 << "\n";
    }
    ss << "\n";

    if (!artifacts.fileinfoContent.empty()) {
        ss << "## File Information\n";
        ss << "```\n";
        ss << artifacts.fileinfoContent;
        ss << "```\n\n";
    }

    if (!artifacts.readelfContent.empty()) {
        ss << "## ReadELF Output\n";
        ss << "```\n";
        ss << artifacts.readelfContent;
        ss << "```\n\n";
    }

    if (!artifacts.stringsContent.empty()) {
        ss << "## Strings (first 2000 lines)\n";
        ss << "```\n";
        ss << artifacts.stringsContent;
        ss << "```\n\n";
    }

    if (!artifacts.objdumpContent.empty()) {
        ss << "## Objdump (first 5000 lines)\n";
        ss << "```\n";
        ss << artifacts.objdumpContent;
        ss << "```\n\n";
    }

    if (!artifacts.manifestDiffContent.empty()) {
        ss << "## Baseline Diff (file system changes)\n";
        ss << "```\n";
        ss << artifacts.manifestDiffContent;
        ss << "```\n\n";
    }

    ss << "## GDB Script Requirements\n\n";
    ss << "Generate a complete GDB script that includes:\n\n";
    
    ss << "### 1. Script Header\n";
    ss << "```gdb\n";
    ss << "# Auto-generated GDB script for malware analysis\n";
    ss << "# Target: " << targetPath << "\n";
    ss << "# Sample: " << artifacts.sampleName << "\n\n";
    ss << "set pagination off\n";
    ss << "set confirm off\n";
    ss << "set breakpoint pending on\n";
    ss << "set print pretty on\n\n";
    ss << "starti\n\n";
    ss << "```\n\n";
    
    ss << "### 2. Base Address Initialization (Python Block)\n";
    ss << "Include this EXACT Python block to calculate the base address:\n\n";
    ss << "```gdb\n";
    ss << "python\n";
    ss << "import gdb, re\n";
    ss << "def _guardain_init(target_basename):\n";
    ss << "    out = gdb.execute('info proc mappings', to_string=True)\n";
    ss << "    base = None\n";
    ss << "    load_addr = None\n";
    ss << "    for line in out.splitlines():\n";
    ss << "        parts = line.split()\n";
    ss << "        if len(parts) < 6:\n";
    ss << "            continue\n";
    ss << "        perms = parts[4]\n";
    ss << "        path  = parts[-1]\n";
    ss << "        if 'r--' in perms and ('/' + target_basename) in path:\n";
    ss << "            try:\n";
    ss << "                load_addr = int(parts[0], 16)\n";
    ss << "            except Exception:\n";
    ss << "                continue\n";
    ss << "            break\n";
    ss << "    if load_addr is None:\n";
    ss << "        raise gdb.GdbError('guardAIn: failed to locate base mapping for ' + target_basename)\n";
    ss << "    # For PIE binaries, load_addr will be randomized (e.g., 0x555555554000)\n";
    ss << "    # For non-PIE EXEC binaries, load_addr will be fixed (e.g., 0x400000)\n";
    ss << "    # If load_addr is in the typical PIE range (0x55* or 0x7f*), it's PIE\n";
    ss << "    # If load_addr is low (< 0x800000), it's likely non-PIE, set base=0\n";
    ss << "    if load_addr < 0x800000:\n";
    ss << "        # Non-PIE binary: addresses are absolute, set base=0\n";
    ss << "        base = 0\n";
    ss << "    else:\n";
    ss << "        # PIE binary: addresses are offsets from load address\n";
    ss << "        base = load_addr\n";
    ss << "    gdb.execute('set $base = 0x%x' % base)\n";
    ss << "    pid = gdb.selected_inferior().pid\n";
    ss << "    gdb.execute('set $pid = %d' % pid)\n";
    ss << "_guardain_init('" << artifacts.sampleName << "')\n";
    ss << "end\n\n";
    ss << "```\n\n";

    ss << "### 3. Breakpoints and Logic\n\n";
    ss << "**CRITICAL ANALYSIS REQUIREMENTS:**\n";
    ss << "1. **Entry Point & Main Flow**: Set breakpoints at _start, main, and key functions\n";
    ss << "2. **System Calls**: Break at critical syscalls (execve, ptrace, memfd_create, mmap, mprotect, dlopen, dlsym, open, socket, etc.)\n";
    ss << "3. **Anti-Debugging Bypasses**:\n";
    ss << "   - **CRITICAL - setjmp/longjmp anti-debugging patterns**:\n";
    ss << "     * Calls to _setjmp() or setjmp() followed by `test eax,eax`\n";
    ss << "     * If eax == 0, the code may write to NULL (0x0) causing SIGSEGV\n";
    ss << "     * Set breakpoint BEFORE the test instruction and set eax to non-zero (e.g., 1) to bypass\n";
    ss << "     * Look for pattern: call _setjmp -> test eax,eax -> je/jne -> mov [0x0], ...\n";
    ss << "     * AFTER bypassing setjmp, look for subsequent checks that read memory values set before setjmp\n";
    ss << "     * If a memory location is set to 0 before setjmp and then tested after, set breakpoint at the test\n";
    ss << "     * Modify the memory value or register to bypass the check (e.g., set memory to 1 or set eax to 0)\n";
    ss << "     * CRITICAL: After setjmp bypass, look for instructions that READ from memory locations that were set to 0 before setjmp\n";
    ss << "       (e.g., `mov eax, [rip+offset]` followed by `test eax,eax` and `sete al`)\n";
    ss << "       If the memory is still 0, the program will exit with code 1. Set a breakpoint at the MOV instruction, and either:\n";
    ss << "       a) Set the memory location to a non-zero value (e.g., `set {int}($base + 0x230e8) = 1`)\n";
    ss << "       b) OR set eax to a non-zero value after the MOV but before the TEST (e.g., `set $eax = 1`)\n";
    ss << "     * The memory check typically appears shortly after the setjmp test bypass, often within the same function\n";
    ss << "     * Ensure to capture breakpoints for unusual anti-debugging flows, including tracing attempts, time checks\n";
    ss << "     * Use of syscalls like `/proc/self/status` or `ps -aux`-style commands to detect debuggers\n";
    ss << "     * CRITICAL: Ensure for ALL of these patterns, registers and flags are set correctly after the method returns\n";
    ss << "       to ensure proper jumps are taken to avoid stack canaries, bad instructions, and other exit lines\n";
    ss << "   - **ptrace checks**: Bypass by modifying return values (set $rax to 0 or error value)\n";
    ss << "   - **Timing checks**: Bypass by modifying comparison results or time values\n";
    ss << "4. **Stack Canaries & Signal Handlers**: \n";
    ss << "   - Watch for jumps into stack fails (e.g., `jne 0x1234` then at 0x1234 flows into `__stack_chk_fail@plt`)\n";
    ss << "   - Watch for stack canaries, signal actions (SIGTRAP, SIGKILL, SIGSEGV, etc.)\n";
    ss << "   - Identify code that can cause premature termination and avoid it with breakpoints and register/flag changes\n";
    ss << "5. **File Operations**: \n";
    ss << "   - Capture memfd files using shell commands: `shell cp /proc/$pid/fd/3 /tmp/memfd_dump.bin 2>/dev/null || true`\n";
    ss << "   - List file descriptors: `shell ls -l /proc/$pid/fd > /tmp/memfd_fds.txt 2>/dev/null || true`\n";
    ss << "6. **Encryption/Decryption**: \n";
    ss << "   - IMPORTANT: Any portion of code that performs encryption and/or decryption of data or code sections\n";
    ss << "     must be captured with breakpoints and snapshots to allow for memory dumps of decrypted content\n";
    ss << "   - If possible, identify the encryption method used if standard, and add it as a comment\n";
    ss << "7. **Command-Line Arguments & Environment**: \n";
    ss << "   - CRITICAL: If the binary expects additional arguments, environment variables, or specific file system state\n";
    ss << "     (files present, specific contents, etc.), these must be identified and included in script comments\n";
    ss << "     and set using shell commands and/or GDB set arg commands prior to execution\n";
    ss << "   - If exact information is not readily available, set using dummy values (such as \"dummy\") for the program to execute minimally.\n";
    ss << "     Then, as the neccessary values are uncovered in the program, modify the set values and run the program again.\n";
    ss << "8. **Anti-Disassembly**: \n";
    ss << "   - CRITICAL: Watch for anti-disassembly techniques. If there are errant or wayward bytes that make instructions\n";
    ss << "     not make sense, bypass them and/or skip over them, and see if the resultant instruction makes sense and flows properly\n";
    ss << "9. **Stack String Extraction (CRITICAL for CTF/RE challenges)**:\n";
    ss << "   - IMPORTANT: Look for patterns where strings are constructed on the stack character-by-character\n";
    ss << "   - Pattern: Multiple consecutive `mov BYTE PTR [rbp-0xNN], 0xHH` instructions where 0xHH are ASCII values\n";
    ss << "   - Example: `mov BYTE PTR [rbp-0x30],0x66` (0x66='f'), `mov BYTE PTR [rbp-0x2f],0x6c` (0x6c='l'), etc.\n";
    ss << "   - These often spell out flags, passwords, or important strings that aren't visible in static string analysis\n";
    ss << "   - ACTION: When you see this pattern in a function (especially functions named like `check_password`, `verify`, `validate`):\n";
    ss << "     1. Count how many consecutive stack writes there are and note the range (e.g., [rbp-0x30] to [rbp-0x3])\n";
    ss << "     2. Set a breakpoint AFTER the LAST stack write instruction (not at function entry!)\n";
    ss << "     3. In the breakpoint commands, dump the memory region: `x/NNc $rbp-0xSTART` where NN is the string length\n";
    ss << "     4. Also dump as hex for verification: `x/NNbx $rbp-0xSTART`\n";
    ss << "     5. Print the constructed string: `printf \\\"Stack string: %s\\\\n\\\", (char*)($rbp-0xSTART)`\n";
    ss << "   - This is how many CTF challenges hide flags - by building them at runtime rather than storing as string literals\n";
    ss << "   - EXAMPLE for check_password function with stack string from [rbp-0x30] to [rbp-0x3] (46 bytes):\n";
    ss << "     ```\n";
    ss << "     # Break AFTER last stack write (e.g., at 0x4017e9 if last write is at 0x4017e5)\n";
    ss << "     break *($base + 0x4017e9)\n";
    ss << "     commands\n";
    ss << "       silent\n";
    ss << "       echo [guardAIn] Stack string constructed in check_password\\\\n\n";
    ss << "       x/46c $rbp-0x30\n";
    ss << "       x/46bx $rbp-0x30\n";
    ss << "       printf \\\"FLAG/Password: %s\\\\n\\\", (char*)($rbp-0x30)\n";
    ss << "       continue\n";
    ss << "     end\n";
    ss << "     ```\n";
    ss << "10. **Bypass Confirmation**: \n";
    ss << "   - For any bypass that modifies a register to affect a nearby conditional branch (e.g., test reg,reg followed by jz/jnz/je/jne),\n";
    ss << "     include at least one confirmation breakpoint using `tbreak` on the next instruction\n";
    ss << "   - Add commands to show current instruction (`x/i $pc`) and flags (`info registers eflags`) to verify bypass worked\n";
    ss << "   - If the branch target address is known, also add a second tbreak at the branch target to confirm the taken path\n";
    ss << "   - Ensure the script continues execution after confirmation\n\n";
    
    ss << "**ADDRESS HANDLING:**\n";
    ss << "- For **PIE binaries** (file info: \"dynamically linked\", readelf: \"Type: DYN\"): Use `*($base + 0xOFFSET)` for all breakpoints\n";
    ss << "- For **non-PIE/static binaries** (file info: \"statically linked\", readelf: \"Type: EXEC\"): Use `*($base + 0xADDRESS)` where ADDRESS is the objdump address. Base will be 0, so it becomes absolute.\n";
    ss << "- **Summary**: ALWAYS use `*($base + <hex_from_objdump>)` format. The initialization script handles PIE vs non-PIE automatically.\n";
    ss << "- **IMPORTANT**: For non-PIE/statically-linked binaries, do NOT set breakpoints in library code (libc functions like malloc, free, etc.). Only break in the main binary code.\n\n";

    ss << "**BREAKPOINT FORMAT:**\n";
    ss << "```gdb\n";
    ss << "# Example: Breakpoint at entry point with snapshot and continuation\n";
    ss << "break *($base + 0x1600)\n";
    ss << "commands\n";
    ss << "  silent\n";
    ss << "  echo [guardAIn] Hit entry point\\n\n";
    ss << "  info registers\n";
    ss << "  x/16i $pc\n";
    ss << "  x/16gx $rsp\n";
    ss << "  continue\n";
    ss << "end\n\n";
    ss << "# Example: Anti-debug bypass (setjmp)\n";
    ss << "break *($base + 0x3652)\n";
    ss << "commands\n";
    ss << "  silent\n";
    ss << "  echo [guardAIn] Bypassing setjmp anti-debug\\n\n";
    ss << "  set $rax = 1\n";
    ss << "  info registers\n";
    ss << "  continue\n";
    ss << "end\n\n";
    ss << "# Example: Capture memfd file\n";
    ss << "break *($base + 0x23a0)\n";
    ss << "commands\n";
    ss << "  silent\n";
    ss << "  echo [guardAIn] memfd_create called\\n\n";
    ss << "  shell cp /proc/$pid/fd/3 /tmp/memfd_dump.bin 2>/dev/null || true\n";
    ss << "  shell ls -l /proc/$pid/fd > /tmp/memfd_fds.txt 2>/dev/null || true\n";
    ss << "  continue\n";
    ss << "end\n\n";
    ss << "# Example: Final breakpoint (stop execution)\n";
    ss << "break *($base + 0x5000)\n";
    ss << "commands\n";
    ss << "  silent\n";
    ss << "  echo [guardAIn] Final breakpoint - stopping\\n\n";
    ss << "  info registers\n";
    ss << "  quit\n";
    ss << "end\n";
    ss << "```\n\n";

    ss << "**IMPORTANT RULES:**\n";
    ss << "- All register names must be LOWERCASE (e.g., `$rax`, not `$RAX` or `$EAX`) - GDB requires lowercase\n";
    ss << "- Use `$pid` variable for process ID (already set by initialization Python block)\n";
    ss << "- Do NOT use getpid() function in GDB commands - use the `$pid` variable instead\n";
    ss << "- Use `silent` in commands blocks to reduce output noise\n";
    ss << "- Always `continue` at the end of a command block unless it's the final breakpoint (which should use `quit`)\n";
    ss << "- For temporary breakpoints (hit once then deleted), use `tbreak` instead of `break`\n";
    ss << "- End the script with `continue` followed by `quit` to ensure clean exit\n";
    ss << "- Each GDB command should be on its own line - do NOT chain commands with semicolons\n";
    ss << "- For final breakpoints (usually at execve or program exit), use `quit` instead of `continue` to stop execution\n\n";

    ss << "### 4. Script Footer\n";
    ss << "```gdb\n";
    ss << "# Start execution\n";
    ss << "continue\n";
    ss << "quit\n";
    ss << "```\n\n";

    ss << "## OUTPUT FORMAT\n";
    ss << "**CRITICAL**: Generate ONLY the complete GDB script. NO markdown code fences, NO explanations, NO JSON.\n";
    ss << "Start directly with the GDB commands (e.g., `# Auto-generated GDB script...`).\n";
    ss << "The output will be saved directly as a .gdb file and executed.\n\n";
    
    ss << "**BEGIN YOUR GDB SCRIPT NOW:**\n";

    return ss.str();
}

bool LlmInterface::writePromptToFile(const std::string& prompt, const std::string& filePath, std::string& errorOut)
{
    errorOut.clear();

    std::ofstream out(filePath, std::ios::binary);
    if (!out) {
        errorOut = "Failed to open prompt file for writing: " + filePath;
        return false;
    }

    out << prompt;
    if (!out.good()) {
        errorOut = "Failed to write prompt to file: " + filePath;
        return false;
    }

    return true;
}

/* OLD APPROACH: Invoke LLM to generate JSON plan
bool LlmInterface::invokeLlmForPlan(
    const std::string& promptFilePath,
    const std::string& outputJsonPath,
    std::string& errorOut)
{
    errorOut.clear();

    std::vector<fs::path> possiblePaths;
    std::vector<std::string> triedPaths;

    // Build list of possible paths
    // First, try absolute path from A:\ (user's workspace root)
    fs::path absoluteScriptPath = fs::path("A:\\guardAInDBG\\Code\\Runtime\\scripts\\generate_plan.py");
    possiblePaths.push_back(absoluteScriptPath);
    triedPaths.push_back(absoluteScriptPath.string());
    
    // Also try with forward slashes
    absoluteScriptPath = fs::path("A:/guardAInDBG/Code/Runtime/scripts/generate_plan.py");
    possiblePaths.push_back(absoluteScriptPath);
    triedPaths.push_back(absoluteScriptPath.string());
    
    // Try relative to current working directory
    possiblePaths.push_back(fs::path("guardAInDBG/Code/Runtime/scripts/generate_plan.py"));
    triedPaths.push_back("guardAInDBG/Code/Runtime/scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("scripts/generate_plan.py"));
    triedPaths.push_back("scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("../scripts/generate_plan.py"));
    triedPaths.push_back("../scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("../../scripts/generate_plan.py"));
    triedPaths.push_back("../../scripts/generate_plan.py");

    // Try relative to executable location (most reliable on Windows)
    #ifdef _WIN32
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path exeDirAbs = fs::absolute(exeDir);
        
        // Try various levels up from executable
        possiblePaths.push_back(exeDirAbs / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs / "scripts" / "generate_plan.py").string());
        
        possiblePaths.push_back(exeDirAbs.parent_path() / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs.parent_path() / "scripts" / "generate_plan.py").string());
        
        // Go up to project root (x64/Debug/ -> guardAInDBG/)
        fs::path projectRoot = exeDirAbs;
        for (int i = 0; i < 5; ++i) {
            projectRoot = projectRoot.parent_path();
            fs::path scriptPath = projectRoot / "Code" / "Runtime" / "scripts" / "generate_plan.py";
            possiblePaths.push_back(scriptPath);
            triedPaths.push_back(scriptPath.string());
        }
    }
    #else
    char exePath[1024];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
        exePath[len] = '\0';
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path exeDirAbs = fs::absolute(exeDir);
        
        possiblePaths.push_back(exeDirAbs / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs / "scripts" / "generate_plan.py").string());
        
        possiblePaths.push_back(exeDirAbs.parent_path() / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs.parent_path() / "scripts" / "generate_plan.py").string());
        
        // Go up to project root
        fs::path projectRoot = exeDirAbs;
        for (int i = 0; i < 5; ++i) {
            projectRoot = projectRoot.parent_path();
            fs::path scriptPath = projectRoot / "Code" / "Runtime" / "scripts" / "generate_plan.py";
            possiblePaths.push_back(scriptPath);
            triedPaths.push_back(scriptPath.string());
        }
    }
    #endif

    fs::path scriptPath;
    bool found = false;
    
    Logger::debug("[LlmInterface] Searching for generate_plan.py script...");
    for (size_t i = 0; i < possiblePaths.size(); ++i) {
        fs::path testPath = possiblePaths[i];
        // Try both the original path and the absolute version
        if (fs::exists(testPath)) {
            scriptPath = fs::absolute(testPath);
            found = true;
            Logger::info("[LlmInterface] Found script at: " + scriptPath.string());
            break;
        } else {
            // Try absolute version
            fs::path absPath = fs::absolute(testPath);
            if (fs::exists(absPath)) {
                scriptPath = absPath;
                found = true;
                Logger::info("[LlmInterface] Found script at: " + scriptPath.string());
                break;
            } else {
                Logger::debug("[LlmInterface] Tried: " + testPath.string() + " (not found)");
            }
        }
    }

    if (!found) {
        std::ostringstream errorMsg;
        errorMsg << "Python script not found: generate_plan.py\n";
        errorMsg << "Searched " << triedPaths.size() << " locations:\n";
        size_t maxShow = (triedPaths.size() < 10) ? triedPaths.size() : 10;
        for (size_t i = 0; i < maxShow; ++i) {
            errorMsg << "  - " << triedPaths[i] << "\n";
        }
        if (triedPaths.size() > 10) {
            errorMsg << "  ... and " << (triedPaths.size() - 10) << " more locations\n";
        }
        errorOut = errorMsg.str();
        Logger::error("[LlmInterface] " + errorOut);
        return false;
    }

    Logger::info("[LlmInterface] Invoking LLM script: " + scriptPath.string());

    // Build command: python generate_plan.py <prompt_file> <output_json>
    // Use python3 on Unix, python on Windows
    #ifdef _WIN32
    const char* pythonCmd = "python";
    #else
    const char* pythonCmd = "python3";
    #endif

    // Try to get API key from environment and pass it to Python script
    std::string apiKey;
    #ifdef _WIN32
    char* apiKeyEnv = nullptr;
    size_t apiKeyLen = 0;
    if (_dupenv_s(&apiKeyEnv, &apiKeyLen, "OPENAI_API_KEY") == 0 && apiKeyEnv != nullptr) {
        apiKey = std::string(apiKeyEnv);
        free(apiKeyEnv);
    }
    #else
    const char* apiKeyEnv = std::getenv("OPENAI_API_KEY");
    if (apiKeyEnv != nullptr) {
        apiKey = std::string(apiKeyEnv);
    }
    #endif

    std::ostringstream cmd;
    cmd << pythonCmd << " \"" << scriptPath.string() << "\" \"" << promptFilePath << "\" \"" << outputJsonPath << "\"";
    if (!apiKey.empty()) {
        cmd << " \"" << apiKey << "\"";
        Logger::debug("[LlmInterface] Passing API key to Python script");
    } else {
        Logger::debug("[LlmInterface] No API key found in environment, Python script will check environment");
    }

    Logger::debug("[LlmInterface] Executing: " + cmd.str());

    int exitCode = std::system(cmd.str().c_str());

    if (exitCode != 0) {
        errorOut = "LLM script failed with exit code: " + std::to_string(exitCode);
        Logger::error("[LlmInterface] " + errorOut);
        Logger::error("[LlmInterface] Make sure Python is installed and openai library is available: pip install openai");
        Logger::error("[LlmInterface] Also ensure OPENAI_API_KEY environment variable is set");
        return false;
    }

    // Verify output file exists
    if (!fs::exists(outputJsonPath)) {
        errorOut = "LLM script did not generate output file: " + outputJsonPath;
        Logger::error("[LlmInterface] " + errorOut);
        return false;
    }

    return true;
}*/

// NEW APPROACH: Invoke LLM to generate GDB script directly
bool LlmInterface::invokeLlmForGdbScript(
    const std::string& promptFilePath,
    const std::string& outputGdbScriptPath,
    std::string& errorOut)
{
    errorOut.clear();

    std::vector<fs::path> possiblePaths;
    std::vector<std::string> triedPaths;

    // Build list of possible paths (same as old approach)
    fs::path absoluteScriptPath = fs::path("A:\\guardAInDBG\\Code\\Runtime\\scripts\\generate_plan.py");
    possiblePaths.push_back(absoluteScriptPath);
    triedPaths.push_back(absoluteScriptPath.string());
    
    absoluteScriptPath = fs::path("A:/guardAInDBG/Code/Runtime/scripts/generate_plan.py");
    possiblePaths.push_back(absoluteScriptPath);
    triedPaths.push_back(absoluteScriptPath.string());
    
    possiblePaths.push_back(fs::path("guardAInDBG/Code/Runtime/scripts/generate_plan.py"));
    triedPaths.push_back("guardAInDBG/Code/Runtime/scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("scripts/generate_plan.py"));
    triedPaths.push_back("scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("../scripts/generate_plan.py"));
    triedPaths.push_back("../scripts/generate_plan.py");
    
    possiblePaths.push_back(fs::path("../../scripts/generate_plan.py"));
    triedPaths.push_back("../../scripts/generate_plan.py");

    // Try relative to executable location
    #ifdef _WIN32
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path exeDirAbs = fs::absolute(exeDir);
        
        possiblePaths.push_back(exeDirAbs / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs / "scripts" / "generate_plan.py").string());
        
        possiblePaths.push_back(exeDirAbs.parent_path() / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs.parent_path() / "scripts" / "generate_plan.py").string());
        
        fs::path projectRoot = exeDirAbs;
        for (int i = 0; i < 5; ++i) {
            projectRoot = projectRoot.parent_path();
            fs::path scriptPath = projectRoot / "Code" / "Runtime" / "scripts" / "generate_plan.py";
            possiblePaths.push_back(scriptPath);
            triedPaths.push_back(scriptPath.string());
        }
    }
    #else
    char exePath[1024];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
        exePath[len] = '\0';
        fs::path exeDir = fs::path(exePath).parent_path();
        fs::path exeDirAbs = fs::absolute(exeDir);
        
        possiblePaths.push_back(exeDirAbs / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs / "scripts" / "generate_plan.py").string());
        
        possiblePaths.push_back(exeDirAbs.parent_path() / "scripts" / "generate_plan.py");
        triedPaths.push_back((exeDirAbs.parent_path() / "scripts" / "generate_plan.py").string());
        
        fs::path projectRoot = exeDirAbs;
        for (int i = 0; i < 5; ++i) {
            projectRoot = projectRoot.parent_path();
            fs::path scriptPath = projectRoot / "Code" / "Runtime" / "scripts" / "generate_plan.py";
            possiblePaths.push_back(scriptPath);
            triedPaths.push_back(scriptPath.string());
        }
    }
    #endif

    fs::path scriptPath;
    bool found = false;
    
    Logger::debug("[LlmInterface] Searching for generate_plan.py script...");
    for (size_t i = 0; i < possiblePaths.size(); ++i) {
        fs::path testPath = possiblePaths[i];
        if (fs::exists(testPath)) {
            scriptPath = fs::absolute(testPath);
            found = true;
            Logger::info("[LlmInterface] Found script at: " + scriptPath.string());
            break;
        } else {
            fs::path absPath = fs::absolute(testPath);
            if (fs::exists(absPath)) {
                scriptPath = absPath;
                found = true;
                Logger::info("[LlmInterface] Found script at: " + scriptPath.string());
                break;
            }
        }
    }

    if (!found) {
        errorOut = "Could not find generate_plan.py. Tried paths:\n";
        for (const auto& p : triedPaths) {
            errorOut += "  - " + p + "\n";
        }
        Logger::error("[LlmInterface] " + errorOut);
        return false;
    }

    // Get API key from environment
    std::string apiKey;
    #ifdef _WIN32
    size_t requiredSize = 0;
    getenv_s(&requiredSize, nullptr, 0, "OPENAI_API_KEY");
    if (requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        getenv_s(&requiredSize, buffer.data(), requiredSize, "OPENAI_API_KEY");
        apiKey = std::string(buffer.data());
    }
    #else
    const char* envKey = std::getenv("OPENAI_API_KEY");
    if (envKey) {
        apiKey = envKey;
    }
    #endif

    // Build command: python script.py prompt.txt output.gdb [api_key]
    std::ostringstream cmdBuilder;
    cmdBuilder << "python \"" << scriptPath.string() << "\" "
               << "\"" << promptFilePath << "\" "
               << "\"" << outputGdbScriptPath << "\"";
    
    if (!apiKey.empty()) {
        cmdBuilder << " \"" << apiKey << "\"";
    }

    const std::string command = cmdBuilder.str();
    Logger::info("[LlmInterface] Executing: " + command);

    int exitCode = std::system(command.c_str());
    if (exitCode != 0) {
        errorOut = "LLM script exited with code: " + std::to_string(exitCode);
        Logger::error("[LlmInterface] " + errorOut);
        return false;
    }

    // Verify output file was created
    if (!fs::exists(outputGdbScriptPath)) {
        errorOut = "LLM script completed but output file was not created: " + outputGdbScriptPath;
        Logger::error("[LlmInterface] " + errorOut);
        return false;
    }

    Logger::info("[LlmInterface] GDB script successfully generated: " + outputGdbScriptPath);
    return true;
}

std::string LlmInterface::truncateContent(const std::string& content, size_t maxLines)
{
    if (content.empty()) {
        return content;
    }

    std::istringstream iss(content);
    std::ostringstream oss;
    size_t lineCount = 0;

    std::string line;
    while (std::getline(iss, line) && lineCount < maxLines) {
        oss << line << "\n";
        lineCount++;
    }

    if (lineCount >= maxLines) {
        oss << "\n... (" << maxLines << " lines shown, truncated for LLM context)\n";
    }

    return oss.str();
}

std::string LlmInterface::readFileContent(const std::string& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        return "";
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
