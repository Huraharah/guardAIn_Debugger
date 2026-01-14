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
    fs::create_directories(llmDir);

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

std::string LlmInterface::buildPrompt(const StaticArtifacts& artifacts, const std::string& targetPath)
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
    ss << "8. All offsets should be in hex format (e.g., \"0x32b0\"), and all register names should be full 64-bit registers in LOWERCASE (e.g. rax, not RAX, EAX, or AX) - GDB requires lowercase register names\n";
    ss << "9. Replace targetPath with the actual target path: " << targetPath << "\n";
    ss << "10. For GDB commands: To get PID, use the $pid variable (already set): printf \"PID=%d\\\\n\", $pid\n";
    ss << "11. Do NOT use getpid() function in GDB commands - use the $pid variable instead\n\n";
    ss << "12. For any bypass that modifies a register to affect a nearby conditional branch (e.g., test reg,reg followed by jz/jnz/je/jne), include at least one confirmation action: temporary breakpoint on the next instruction (provided as \"$base + <offset value>\", such as \"$base + 0x1234\"), and add separate gdb_cmd actions for \"x/i $pc\" and \"info registers eflags\" (DO NOT use semicolons to chain commands - use separate gdb_cmd actions). If the branch target address is known, also add a second breakpoint at the branch target to confirm the taken path. Afterwards, ensure that the script continues the program.\n";
    ss << "13. IMPORTANT: GDB command blocks do NOT support semicolons to chain commands. Each command must be a separate gdb_cmd action. For example, instead of \"x/i $pc; info registers eflags\", use two separate gdb_cmd actions: one with \"x/i $pc\" and another with \"info registers eflags\".\n";
    ss << "14. IMPORTANT: Also watch for jumps into stack fails, ie \"jne 0x1234\" then at 0x1234 flows into a call to \"__stack_chk_fail\". These must also be avoided with breakpoints and register/flag changes\n";
    ss << "15. CRITICAL: ALL breakpoint addresses in gdb_cmd actions MUST use the format \"*($base + <offset>)\" where offset is a hex value like 0x3670. NEVER use absolute addresses like \"*0x3670\" in breakpoint commands. The base address is loaded dynamically and stored in $base. Example: use \"break *($base + 0x3670)\" NOT \"break *0x3670\". This applies to break, tbreak, and hbreak commands.\n\n";
	ss << "## IMPORTANT ##\n";
    ss << "Generate ONLY the JSON plan, no markdown, no explanations, just valid JSON.";

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
