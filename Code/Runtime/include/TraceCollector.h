#pragma once

#include <string>
#include <map>
#include <vector>

struct TraceSummary
{
    std::string sampleName;
    std::string sha256;

    // Syscall stats from strace:
    std::map<std::string, int> syscallCounts;
    std::vector<std::string> interestingSyscalls; // e.g., "ptrace", "execve", "connect"

    // Function calls from ltrace (optional, just simple counts for now):
    std::map<std::string, int> libcallCounts;

    // Files touched (from manifest diff):
    std::vector<std::string> createdFiles;
    std::vector<std::string> modifiedFiles;

    // Crash info (from gdb log):
    bool crashed = false;
    std::string crashSignal;
    std::string crashInstructionPtr;  // e.g., "0x55555555765a"
    std::vector<std::string> backtraceLines;
};

class TraceCollector
{
public:
    // artifactsRoot: A:\artifacts
    // sampleName: e.g. "suspicious"
    bool buildSummary(const std::string& artifactsRoot,
        const std::string& sampleName,
        TraceSummary& outSummary);

    // summaryPath: e.g. A:\artifacts\suspicious\summary\summary.json
    bool writeSummaryJson(const TraceSummary& summary,
        const std::string& summaryPath);

private:
    void loadSha256(const std::string& staticDir, TraceSummary& summary);
    void analyzeStrace(const std::string& runDir, TraceSummary& summary);
    void analyzeLtrace(const std::string& runDir, TraceSummary& summary);
    void analyzeManifestDiff(const std::string& baselineDir, TraceSummary& summary);
    void analyzeDebugLog(const std::string& debugDir, TraceSummary& summary);
};
