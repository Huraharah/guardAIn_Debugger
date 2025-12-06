#include "TraceCollector.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

// ---------- Small helpers ----------

// Trim leading/trailing whitespace (for safety when parsing logs)
static std::string trim(const std::string& s)
{
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Extract syscall name from an strace line.
// Handles both "openat(...)" and "12345 openat(...)" style.
static std::string extractSyscallName(const std::string& line)
{
    std::size_t paren = line.find('(');
    if (paren == std::string::npos)
        return {};

    // Look backwards from '(' for the preceding space
    std::size_t start = line.rfind(' ', paren);
    if (start == std::string::npos)
        start = 0;
    else
        start += 1;

    if (start >= paren)
        return {};

    return line.substr(start, paren - start);
}

// Extract a function name from an ltrace line.
// e.g. "puts("foo") = 3" or "12345 puts("foo") = 3"
static std::string extractLibcallName(const std::string& line)
{
    std::size_t paren = line.find('(');
    if (paren == std::string::npos)
        return {};

    std::size_t start = line.rfind(' ', paren);
    if (start == std::string::npos)
        start = 0;
    else
        start += 1;

    if (start >= paren)
        return {};

    return line.substr(start, paren - start);
}

// ---------- Public API ----------

bool TraceCollector::buildSummary(const std::string& artifactsRoot,
    const std::string& sampleName,
    TraceSummary& outSummary)
{
    outSummary = TraceSummary{};
    outSummary.sampleName = sampleName;

    fs::path base = fs::path(artifactsRoot) / sampleName;
    fs::path baselineDir = base / "baseline";
    fs::path staticDir = base / "static";
    fs::path runDir = base / "run1";
    fs::path debugDir = base / "debug";

    loadSha256(staticDir.string(), outSummary);
    analyzeStrace(runDir.string(), outSummary);
    analyzeLtrace(runDir.string(), outSummary);
    analyzeManifestDiff(baselineDir.string(), outSummary);
    analyzeDebugLog(debugDir.string(), outSummary);

    return true;
}

bool TraceCollector::writeSummaryJson(const TraceSummary& s,
    const std::string& summaryPath)
{
    fs::path path(summaryPath);
    fs::create_directories(path.parent_path());

    std::ofstream out(path);
    if (!out)
    {
        std::cerr << "[TraceCollector] Failed to open summary for write: "
            << summaryPath << "\n";
        return false;
    }

    out << "{\n";
    out << "  \"sample\": \"" << s.sampleName << "\",\n";
    out << "  \"sha256\": \"" << s.sha256 << "\",\n";

    // Syscall counts
    out << "  \"syscall_counts\": {\n";
    bool first = true;
    for (const auto& kv : s.syscallCounts)
    {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << kv.first << "\": " << kv.second;
    }
    out << "\n  },\n";

    // Interesting syscalls
    out << "  \"interesting_syscalls\": [";
    for (std::size_t i = 0; i < s.interestingSyscalls.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << "\"" << s.interestingSyscalls[i] << "\"";
    }
    out << "],\n";

    // Libcall counts (ltrace)
    out << "  \"libcall_counts\": {\n";
    first = true;
    for (const auto& kv : s.libcallCounts)
    {
        if (!first) out << ",\n";
        first = false;
        out << "    \"" << kv.first << "\": " << kv.second;
    }
    out << "\n  },\n";

    // Files created / modified
    out << "  \"created_files\": [";
    for (std::size_t i = 0; i < s.createdFiles.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << "\"" << s.createdFiles[i] << "\"";
    }
    out << "],\n";

    out << "  \"modified_files\": [";
    for (std::size_t i = 0; i < s.modifiedFiles.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << "\"" << s.modifiedFiles[i] << "\"";
    }
    out << "],\n";

    // Crash info
    out << "  \"crash\": {\n";
    out << "    \"crashed\": " << (s.crashed ? "true" : "false") << ",\n";
    out << "    \"signal\": \"" << s.crashSignal << "\",\n";
    out << "    \"ip\": \"" << s.crashInstructionPtr << "\",\n";
    out << "    \"backtrace\": [";
    for (std::size_t i = 0; i < s.backtraceLines.size(); ++i)
    {
        if (i > 0) out << ", ";
        out << "\"" << s.backtraceLines[i] << "\"";
    }
    out << "]\n";
    out << "  }\n";

    out << "}\n";
    return true;
}

// ---------- Private helpers ----------

void TraceCollector::loadSha256(const std::string& staticDir, TraceSummary& summary)
{
    fs::path p = fs::path(staticDir) / "suspicious.sha256";
    std::ifstream in(p);
    if (!in)
        return;

    std::string hash, filename;
    in >> hash >> filename; // e.g. "<hash>  suspicious"
    summary.sha256 = hash;
}

void TraceCollector::analyzeStrace(const std::string& runDir, TraceSummary& summary)
{
    fs::path p = fs::path(runDir) / "suspicious.strace";
    std::ifstream in(p);
    if (!in)
        return;

    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty())
            continue;

        // Ignore summary lines like "+++ exited with 0 +++"
        if (line.rfind("+++ exited", 0) == 0)
            continue;

        std::string name = extractSyscallName(line);
        if (name.empty())
            continue;

        summary.syscallCounts[name]++;

        // Flag “interesting” syscalls
        if (name == "execve" || name == "ptrace" ||
            name == "clone" || name == "fork" ||
            name == "vfork" || name == "prctl" ||
            name == "kill" || name == "socket" ||
            name == "connect")
        {
            if (std::find(summary.interestingSyscalls.begin(),
                summary.interestingSyscalls.end(),
                name) == summary.interestingSyscalls.end())
            {
                summary.interestingSyscalls.push_back(name);
            }
        }
    }
}

void TraceCollector::analyzeLtrace(const std::string& runDir, TraceSummary& summary)
{
    fs::path p = fs::path(runDir) / "suspicious.ltrace";
    std::ifstream in(p);
    if (!in)
        return;

    std::string line;
    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty())
            continue;

        std::string name = extractLibcallName(line);
        if (name.empty())
            continue;

        summary.libcallCounts[name]++;
    }
}

// Parse unified diff to infer created / modified files.
void TraceCollector::analyzeManifestDiff(const std::string& baselineDir,
    TraceSummary& summary)
{
    fs::path p = fs::path(baselineDir) / "manifest_diff.txt";
    std::ifstream in(p);
    if (!in)
        return;

    // Collect + and - lines keyed by path
    std::map<std::string, std::string> plusLines;
    std::map<std::string, std::string> minusLines;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;

        if (line[0] != '+' && line[0] != '-')
            continue;

        // Skip diff headers "+++" and "---"
        if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0)
            continue;

        char sign = line[0];
        std::string payload = line.substr(1); // without leading +/- 
        payload = trim(payload);
        if (payload.empty())
            continue;

        // Manifest line: path|size|type|mtime
        std::size_t pipePos = payload.find('|');
        if (pipePos == std::string::npos)
            continue;

        std::string path = payload.substr(0, pipePos);

        if (sign == '+')
            plusLines[path] = payload;
        else if (sign == '-')
            minusLines[path] = payload;
    }

    // Determine created vs modified. Deleted files we can ignore for now.
    for (const auto& kv : plusLines)
    {
        const std::string& path = kv.first;
        bool alsoMinus = (minusLines.find(path) != minusLines.end());
        if (alsoMinus)
        {
            summary.modifiedFiles.push_back(path);
        }
        else
        {
            summary.createdFiles.push_back(path);
        }
    }
}

void TraceCollector::analyzeDebugLog(const std::string& debugDir,
    TraceSummary& summary)
{
    fs::path p = fs::path(debugDir) / "debug.log";
    std::ifstream in(p);
    if (!in)
        return;

    std::string line;
    bool inBacktrace = false;

    while (std::getline(in, line))
    {
        std::string trimmed = trim(line);

        if (trimmed.rfind("Program received signal", 0) == 0)
        {
            summary.crashed = true;

            // e.g. "Program received signal SIGSEGV, Segmentation fault."
            std::size_t pos = trimmed.find("signal");
            if (pos != std::string::npos)
            {
                std::stringstream ss(trimmed.substr(pos));
                std::string tmp, sig;
                ss >> tmp >> sig; // "signal" "SIGSEGV," 
                if (!sig.empty() && sig.back() == ',')
                    sig.pop_back();
                summary.crashSignal = sig;
            }
        }
        else if (trimmed.rfind("#0", 0) == 0)
        {
            // First backtrace frame: capture IP and line
            summary.backtraceLines.push_back(trimmed);
            std::stringstream ss(trimmed);
            std::string frame, ip;
            ss >> frame >> ip; // "#0" "0x5555..."
            summary.crashInstructionPtr = ip;
            inBacktrace = true;
        }
        else if (inBacktrace)
        {
            if (!trimmed.empty() && trimmed[0] == '#')
            {
                summary.backtraceLines.push_back(trimmed);
            }
            else
            {
                inBacktrace = false;
            }
        }
    }
}
