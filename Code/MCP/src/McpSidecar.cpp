#include "McpSidecar.h"
#include "Logger.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

void appendScriptSearchPaths(std::vector<fs::path>& out, std::vector<std::string>& tried)
{
    out.push_back(fs::path("A:\\guardAInDBG\\Code\\MCP\\scripts\\mcp_static_sidecar.py"));
    tried.push_back(out.back().string());
    out.push_back(fs::path("A:/guardAInDBG/Code/MCP/scripts/mcp_static_sidecar.py"));
    tried.push_back(out.back().string());
    out.push_back(fs::path("guardAInDBG/Code/MCP/scripts/mcp_static_sidecar.py"));
    tried.push_back(out.back().string());
    out.push_back(fs::path("Code/MCP/scripts/mcp_static_sidecar.py"));
    tried.push_back(out.back().string());

#ifdef _WIN32
    char exePath[MAX_PATH]{};
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        fs::path exeDir = fs::absolute(fs::path(exePath).parent_path());
        out.push_back(exeDir / "Code" / "MCP" / "scripts" / "mcp_static_sidecar.py");
        tried.push_back(out.back().string());
        out.push_back(exeDir.parent_path() / "Code" / "MCP" / "scripts" / "mcp_static_sidecar.py");
        tried.push_back(out.back().string());
        fs::path root = exeDir;
        for (int i = 0; i < 6; ++i) {
            root = root.parent_path();
            fs::path p = root / "Code" / "MCP" / "scripts" / "mcp_static_sidecar.py";
            out.push_back(p);
            tried.push_back(p.string());
        }
    }
#endif
}

fs::path findSidecarScript(std::string& errorOut)
{
    std::vector<fs::path> candidates;
    std::vector<std::string> tried;
    appendScriptSearchPaths(candidates, tried);

    const fs::path fromTu =
        fs::path(__FILE__).parent_path().parent_path() / "scripts" / "mcp_static_sidecar.py";
    candidates.insert(candidates.begin(), fs::absolute(fromTu));
    tried.insert(tried.begin(), fs::absolute(fromTu).string());

    for (const auto& p : candidates) {
        std::error_code ec;
        if (fs::exists(p, ec)) {
            return fs::absolute(p);
        }
    }
    std::ostringstream oss;
    oss << "mcp_static_sidecar.py not found. Tried:\n";
    for (const auto& t : tried) {
        oss << "  - " << t << "\n";
    }
    errorOut = oss.str();
    return {};
}

} // namespace

McpSidecarRunResult McpSidecar::runBlocking(const McpSidecarConfig& cfg)
{
    McpSidecarRunResult result;
    if (!cfg.enabled) {
        result.ok = true;
        result.detail = "MCP sidecar disabled in config.";
        Logger::info("[McpSidecar] " + result.detail);
        return result;
    }

    std::string findErr;
    const fs::path script = findSidecarScript(findErr);
    if (script.empty()) {
        result.detail = findErr;
        Logger::error("[McpSidecar] " + findErr);
        return result;
    }

    std::error_code ec;
    fs::create_directories(fs::path(cfg.artifactsRoot) / "mcp", ec);

    std::ostringstream cmd;
    cmd << "python \"" << script.string() << "\""
        << " --binary \"" << cfg.sampleBinaryHostPath << "\""
        << " --artifacts \"" << cfg.artifactsRoot << "\"";

    if (!cfg.ghidraInstallDir.empty()) {
        cmd << " --ghidra \"" << cfg.ghidraInstallDir << "\"";
    }

    const std::string command = cmd.str();
    Logger::info("[McpSidecar] " + command);

    const int code = std::system(command.c_str());
    if (code != 0) {
        result.detail = "mcp_static_sidecar.py exited with code " + std::to_string(code);
        Logger::warn("[McpSidecar] " + result.detail);
        // Non-zero can mean Ghidra missing; llm_context.txt may still explain — treat as soft failure
        result.ok = fs::exists(fs::path(cfg.artifactsRoot) / "mcp" / "llm_context.txt");
        if (result.ok) {
            result.detail += " (llm_context.txt present; continuing)";
        }
        return result;
    }

    result.ok = true;
    result.detail = "MCP static sidecar finished.";
    Logger::info("[McpSidecar] " + result.detail);
    return result;
}
