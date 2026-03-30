#pragma once

#include "McpSidecarConfig.h"
#include "McpSidecar.h"

#include <future>
#include <optional>
#include <string>

// Owns parallel host-side work that must complete before the LLM prompt is built.
class AnalysisCoordinator
{
public:
    // Starts Ghidra/MCP static sidecar on a worker thread (no-op if cfg.enabled is false).
    void startStaticMcpParallel(const McpSidecarConfig& cfg);

    // Blocks until the sidecar finishes; logs outcome. Safe to call if sidecar was disabled or never started.
    void joinStaticMcpBeforeLlm();

private:
    std::optional<std::future<McpSidecarRunResult>> mcpFuture_;
    bool mcpScheduled_ = false;
};
