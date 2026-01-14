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

    // Build a prompt string from collected artifacts
    static std::string buildPrompt(const StaticArtifacts& artifacts, const std::string& targetPath);

    // Write prompt to file
    static bool writePromptToFile(const std::string& prompt, const std::string& filePath, std::string& errorOut);

    // Invoke LLM to generate plan.json from prompt
    // This calls a Python script that interfaces with the LLM
    static bool invokeLlmForPlan(
        const std::string& promptFilePath,
        const std::string& outputJsonPath,
        std::string& errorOut);

private:
    // Helper to limit content size for LLM (e.g., first 5000 lines of objdump)
    static std::string truncateContent(const std::string& content, size_t maxLines);
    static std::string readFileContent(const std::string& filePath);
};
