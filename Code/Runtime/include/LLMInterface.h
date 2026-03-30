#pragma once

#include <string>
#include <vector>

class LlmInterface
{
public:
    struct StaticArtifacts
    {
        std::string sampleName;
        std::string sha256;
        std::string objdumpContent;      // objdump -d -M intel output
        std::string stringsContent;      // strings -a -t x output (first 2000 lines)
        std::string readelfContent;      // readelf -a output
        std::string fileinfoContent;     // file command output
        std::string manifestDiffContent; // baseline diff (optional)
        std::string mcpGhidraContent;    // MCP/Ghidra sidecar llm_context.txt (optional)
    };

    // Generate a debug plan by collecting artifacts and calling LLM
    // Returns true on success, false on error (error details in errorOut)
    static bool generateDebugPlan(
        const std::string& artifactsRoot,
        const std::string& sampleName,
        const std::string& targetPath,  // e.g., "/home/analyst/suspicious"
        std::string& errorOut);

    // Collect static analysis artifacts from the artifacts directory
    static bool collectStaticArtifacts(
        const std::string& artifactsRoot,
        const std::string& sampleName,
        StaticArtifacts& outArtifacts,
        std::string& errorOut);

    // OLD APPROACH (JSON-based): Build a prompt string from collected artifacts
    // static std::string buildPrompt(const StaticArtifacts& artifacts, const std::string& targetPath);
    
    // NEW APPROACH (Direct GDB script): Build a prompt for LLM to generate GDB script directly.
    // Loads the "Breakpoints and Logic" section from file (see loadGdbPromptInstructions).
    static bool buildGdbScriptPrompt(
        const StaticArtifacts& artifacts,
        const std::string& targetPath,
        const std::string& artifactsRoot,
        std::string& promptOut,
        std::string& errorOut);

    // Write prompt to file
    static bool writePromptToFile(const std::string& prompt, const std::string& filePath, std::string& errorOut);

	/* OLD APPROACH (JSON-based): Invoke LLM to generate plan.json from prompt
    static bool invokeLlmForPlan(
         const std::string& promptFilePath,
         const std::string& outputJsonPath,
         std::string& errorOut);
    */

    // NEW APPROACH: Invoke LLM to generate plan.gdb (GDB script) directly from prompt
    // Returns true on success, false on error. responseIdOut will contain the response ID if successful.
    // conversationHistoryPathOut will contain the conversation history file path for Anthropic models.
    static bool invokeLlmForGdbScript(
        const std::string& promptFilePath,
        const std::string& outputGdbScriptPath,
        std::string& errorOut,
        std::string* responseIdOut = nullptr,
        const std::string& previousResponseId = "",
        const std::string& gdbLogPath = "",
        const std::string& model = "gpt-5-mini",
        const std::string& previousConversationHistoryPath = "",
        std::string* conversationHistoryPathOut = nullptr);

    // Helper to determine provider (openai/anthropic) from model name
    static std::string determineProviderFromModel(const std::string& model);

    private:
    // Middle GDB prompt section: tries LLM/instructions_<sampleName>.txt, then LLM/instructions.txt,
    // then <Runtime>/prompts/instructions.txt next to this translation unit (dev / in-tree default).
    static bool loadGdbPromptInstructions(
        const std::string& artifactsRoot,
        const std::string& sampleName,
        std::string& contentOut,
        std::string& errorOut);

    // Helper to limit content size for LLM (e.g., first 5000 lines of objdump)
    static std::string truncateContent(const std::string& content, size_t maxLines);
    static std::string readFileContent(const std::string& filePath);
};
