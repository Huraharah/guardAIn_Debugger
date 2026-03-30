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
    std::string prompt;
    if (!buildGdbScriptPrompt(artifacts, targetPath, artifactsRoot, prompt, errorOut)) {
        return false;
    }
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

    const fs::path mcpDir = base / "mcp";
    const fs::path mcpContextPath = mcpDir / "llm_context.txt";
    const fs::path mcpEnrichmentPath = mcpDir / "cursor_enrichment.txt";
    if (fs::exists(mcpContextPath)) {
        std::string mcpBlock = readFileContent(mcpContextPath.string());
        if (fs::exists(mcpEnrichmentPath)) {
            mcpBlock += "\n## MCP / Cursor manual enrichment (cursor_enrichment.txt)\n\n";
            mcpBlock += readFileContent(mcpEnrichmentPath.string());
        }
        outArtifacts.mcpGhidraContent = truncateContent(mcpBlock, 800);
        if (!outArtifacts.mcpGhidraContent.empty()) {
            Logger::info("[LlmInterface] Loaded MCP/Ghidra context from: " + mcpContextPath.string());
        }
    } else if (fs::exists(mcpEnrichmentPath)) {
        outArtifacts.mcpGhidraContent =
            truncateContent(readFileContent(mcpEnrichmentPath.string()), 800);
        if (!outArtifacts.mcpGhidraContent.empty()) {
            Logger::info("[LlmInterface] Loaded MCP enrichment only from: " + mcpEnrichmentPath.string());
        }
    }

    return true;
}

bool LlmInterface::loadGdbPromptInstructions(
    const std::string& artifactsRoot,
    const std::string& sampleName,
    std::string& contentOut,
    std::string& errorOut)
{
    errorOut.clear();
    contentOut.clear();

    const fs::path llmDir = fs::path(artifactsRoot) / "LLM";
    const fs::path fromSourceTree =
        fs::path(__FILE__).parent_path().parent_path() / "prompts" / "instructions.txt";

    const fs::path candidates[] = {
        llmDir / ("instructions_" + sampleName + ".txt"),
        llmDir / "instructions.txt",
        fromSourceTree
    };

    std::string tried;
    for (const fs::path& p : candidates) {
        tried += "  - " + p.string() + "\n";
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            continue;
        }
        contentOut = readFileContent(fs::absolute(p).string());
        if (contentOut.empty()) {
            errorOut = "GDB prompt instructions file is empty: " + fs::absolute(p).string();
            return false;
        }
        Logger::info("[LlmInterface] GDB prompt instructions loaded from: " + fs::absolute(p).string());
        return true;
    }

    errorOut = "Could not find GDB prompt instructions file. Tried:\n" + tried;
    return false;
}

bool LlmInterface::buildGdbScriptPrompt(
    const StaticArtifacts& artifacts,
    const std::string& targetPath,
    const std::string& artifactsRoot,
    std::string& promptOut,
    std::string& errorOut)
{
    errorOut.clear();
    promptOut.clear();

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

    if (!artifacts.mcpGhidraContent.empty()) {
        ss << artifacts.mcpGhidraContent;
        if (artifacts.mcpGhidraContent.back() != '\n') {
            ss << "\n";
        }
        ss << "\n";
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
    ss << "```\n\n";
    
    ss << "### 2. Base Address Initialization (Python Block)\n";
    ss << "Include this EXACT Python block to calculate the base address (only required for the first iteration - after that, use the now calculated base address):\n\n";
    ss << "```gdb\n";
    ss << "starti\n\n"; 
    ss << "python\n";
    ss << "import gdb, re\n";
    ss << "def _guardain_init(target_basename):\n";
    ss << "    base = None\n";
    ss << "    load_addr = None\n";
    ss << "    \n";
    ss << "    # Method 1: Try info proc mappings (most reliable for running processes)\n";
    ss << "    try:\n";
    ss << "        out = gdb.execute('info proc mappings', to_string=True)\n";
    ss << "        for line in out.splitlines():\n";
    ss << "            parts = line.split()\n";
    ss << "            if len(parts) < 6:\n";
    ss << "                continue\n";
    ss << "            perms = parts[4]\n";
    ss << "            path = parts[-1] if len(parts) > 5 else \"\"\n";
    ss << "            # Match if path contains target basename (flexible matching)\n";
    ss << "            if 'r-x' in perms or 'r--' in perms:\n";
    ss << "                if target_basename in path or ('/' + target_basename) in path:\n";
    ss << "                    try:\n";
    ss << "                        load_addr = int(parts[0], 16)\n";
    ss << "                        break\n";
    ss << "                    except Exception:\n";
    ss << "                        continue\n";
    ss << "    except Exception:\n";
    ss << "        pass\n";
    ss << "    \n";
    ss << "    # Method 2: Fallback to info files (works even if process not fully started)\n";
    ss << "    if load_addr is None:\n";
    ss << "        try:\n";
    ss << "            out = gdb.execute('info files', to_string=True)\n";
    ss << "            # Look for entry point or .text section address\n";
    ss << "            for line in out.splitlines():\n";
    ss << "                # Match lines like: \"0x400000 - 0x400814 is .text\"\n";
    ss << "                match = re.search(r'0x([0-9a-fA-F]+)', line)\n";
    ss << "                if match and '.text' in line:\n";
    ss << "                    try:\n";
    ss << "                        load_addr = int(match.group(1), 16)\n";
    ss << "                        break\n";
    ss << "                    except Exception:\n";
    ss << "                        continue\n";
    ss << "        except Exception:\n";
    ss << "            pass\n";
    ss << "    \n";
    ss << "    # Method 3: Use entry point from info proc exe\n";
    ss << "    if load_addr is None:\n";
    ss << "        try:\n";
    ss << "            # Get the entry point\n";
    ss << "            entry_str = gdb.execute('info proc exe', to_string=True)\n";
    ss << "            # Try to get entry point from $pc if available\n";
    ss << "            try:\n";
    ss << "                pc_val = gdb.parse_and_eval('$pc')\n";
    ss << "                if pc_val:\n";
    ss << "                    load_addr = int(pc_val) & 0xfffffffffffff000  # Round down to page boundary\n";
    ss << "            except Exception:\n";
    ss << "                pass\n";
    ss << "        except Exception:\n";
    ss << "            pass\n";
    ss << "    \n";
    ss << "    # If still no address found, use default for non-PIE x86-64 binaries\n";
    ss << "    if load_addr is None:\n";
    ss << "        # Default base for non-PIE x86-64 ELF binaries\n";
    ss << "        load_addr = 0x400000\n";
    ss << "        gdb.write('guardAIn: Using default base address 0x400000 (non-PIE binary)\\n')\n";
    ss << "    \n";
    ss << "    # Determine if PIE or non-PIE based on load address\n";
    ss << "    # Non-PIE binaries typically load at 0x400000 (x86-64) or 0x8048000 (x86)\n";
    ss << "    # PIE binaries load at randomized addresses (0x55* or 0x7f*)\n";
    ss << "    if load_addr < 0x800000:\n";
    ss << "        # Non-PIE binary: addresses are absolute, set base=0\n";
    ss << "        base = 0\n";
    ss << "    else:\n";
    ss << "        # PIE binary: addresses are offsets from load address\n";
    ss << "        base = load_addr\n";
    ss << "    \n";
    ss << "    gdb.execute('set $base = 0x%x' % base)\n";
    ss << "    try:\n";
    ss << "        pid = gdb.selected_inferior().pid\n";
    ss << "        gdb.execute('set $pid = %d' % pid)\n";
    ss << "    except Exception:\n";
    ss << "        gdb.execute('set $pid = 0')\n";
    ss << "_guardain_init('" << artifacts.sampleName << "')\n";
    ss << "end\n\n";
    ss << "```\n\n";

    std::string breakpointInstructions;
    if (!loadGdbPromptInstructions(artifactsRoot, artifacts.sampleName, breakpointInstructions, errorOut)) {
        return false;
    }
    ss << breakpointInstructions;

    ss << "### 4. Script Footer\n";
    ss << "```gdb\n";
    ss << "# Let the program finish execution to see success/failure messages\n";
    ss << "continue\n";
    ss << "# Exit GDB after program completes\n";
    ss << "quit\n";
    ss << "```\n\n";

    ss << "## OUTPUT FORMAT\n";
    ss << "**CRITICAL**: Generate ONLY the complete GDB script. NO markdown code fences, NO explanations, NO JSON.\n";
    ss << "Start directly with the GDB commands (e.g., `# Auto-generated GDB script...`).\n";
    ss << "The output will be saved directly as a .gdb file and executed.\n\n";
    
    ss << "**BEGIN YOUR GDB SCRIPT NOW:**\n";

    promptOut = ss.str();
    return true;
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

// Helper to determine provider from model name
std::string LlmInterface::determineProviderFromModel(const std::string& model)
{
    // Claude models start with "claude-"
    if (model.find("claude-") == 0) {
        return "anthropic";
    }
    // OpenAI models (gpt-4, gpt-5, etc.)
    if (model.find("gpt-") == 0) {
        return "openai";
    }
    // Default to OpenAI for unknown models
    return "openai";
}

// NEW APPROACH: Invoke LLM to generate GDB script directly
bool LlmInterface::invokeLlmForGdbScript(
    const std::string& promptFilePath,
    const std::string& outputGdbScriptPath,
    std::string& errorOut,
    std::string* responseIdOut,
    const std::string& previousResponseId,
    const std::string& gdbLogPath,
    const std::string& model,
    const std::string& previousConversationHistoryPath,
    std::string* conversationHistoryPathOut)
{
    errorOut.clear();
    if (responseIdOut) {
        responseIdOut->clear();
    }
    if (conversationHistoryPathOut) {
        conversationHistoryPathOut->clear();
    }

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

    // Determine provider and get appropriate API key from environment
    std::string provider = determineProviderFromModel(model);
    std::string apiKey;
    std::string envVarName = (provider == "anthropic") ? "ANTHROPIC_API_KEY" : "OPENAI_API_KEY";
    
    #ifdef _WIN32
    size_t requiredSize = 0;
    getenv_s(&requiredSize, nullptr, 0, envVarName.c_str());
    if (requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        getenv_s(&requiredSize, buffer.data(), requiredSize, envVarName.c_str());
        apiKey = std::string(buffer.data());
    }
    #else
    const char* envKey = std::getenv(envVarName.c_str());
    if (envKey) {
        apiKey = envKey;
    }
    #endif

    // Determine provider and conversation history path
    std::string conversationHistoryPath;
    
    // For Anthropic, use conversation history file; for OpenAI, use response ID
    if (provider == "anthropic") {
        // Use previous conversation history if provided, otherwise will create new one
        conversationHistoryPath = previousConversationHistoryPath;
    }
    
    // Build command: python script.py prompt.txt output.gdb [api_key] [previous_response_id] [gdb_log_path] [model] [provider] [conversation_history_file]
    std::ostringstream cmdBuilder;
    cmdBuilder << "python \"" << scriptPath.string() << "\" "
               << "\"" << promptFilePath << "\" "
               << "\"" << outputGdbScriptPath << "\"";
    
    if (!apiKey.empty()) {
        cmdBuilder << " \"" << apiKey << "\"";
    } else {
        cmdBuilder << " \"\"";
    }
    
    // For OpenAI: pass previous_response_id; for Anthropic: pass empty (we use conversation history instead)
    if (provider == "openai" && !previousResponseId.empty()) {
        cmdBuilder << " \"" << previousResponseId << "\"";
    } else {
        cmdBuilder << " \"\"";
    }
    
    if (!gdbLogPath.empty()) {
        cmdBuilder << " \"" << gdbLogPath << "\"";
    } else {
        cmdBuilder << " \"\"";
    }
    
    // Add model and provider
    cmdBuilder << " \"" << model << "\"";
    cmdBuilder << " \"" << provider << "\"";
    
    // Add conversation history file path (for Anthropic)
    if (!conversationHistoryPath.empty()) {
        cmdBuilder << " \"" << conversationHistoryPath << "\"";
    } else {
        cmdBuilder << " \"\"";
    }

    const std::string command = cmdBuilder.str();
    Logger::info("[LlmInterface] Using model: " + model + " (provider: " + provider + ")");
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

    // Read response ID if available (for OpenAI)
    if (responseIdOut && provider == "openai") {
        fs::path responseIdPath = fs::path(outputGdbScriptPath).parent_path() / 
                                  (fs::path(outputGdbScriptPath).stem().string() + ".response_id");
        if (fs::exists(responseIdPath)) {
            *responseIdOut = readFileContent(responseIdPath.string());
            // Remove trailing newline if present
            if (!responseIdOut->empty() && responseIdOut->back() == '\n') {
                responseIdOut->pop_back();
            }
            Logger::info("[LlmInterface] Response ID captured: " + *responseIdOut);
        }
    }
    
    // Read conversation history path if available (for Anthropic)
    if (conversationHistoryPathOut && provider == "anthropic") {
        fs::path conversationHistoryPath = fs::path(outputGdbScriptPath).parent_path() / 
                                           (fs::path(outputGdbScriptPath).stem().string() + ".conversation.json");
        if (fs::exists(conversationHistoryPath)) {
            *conversationHistoryPathOut = conversationHistoryPath.string();
            Logger::info("[LlmInterface] Conversation history saved: " + *conversationHistoryPathOut);
        }
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
