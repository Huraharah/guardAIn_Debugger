#include "AnalysisCoordinator.h"
#include "Logger.h"

void AnalysisCoordinator::startStaticMcpParallel(const McpSidecarConfig& cfg)
{
    mcpFuture_.reset();
    mcpScheduled_ = false;

    if (!cfg.enabled) {
        Logger::info("[AnalysisCoordinator] MCP static sidecar disabled; skipping parallel phase.");
        return;
    }

    mcpScheduled_ = true;
    mcpFuture_ = std::async(std::launch::async, [cfg]() { return McpSidecar::runBlocking(cfg); });
    Logger::info("[AnalysisCoordinator] MCP static sidecar started in background.");
}

void AnalysisCoordinator::joinStaticMcpBeforeLlm()
{
    if (!mcpFuture_.has_value()) {
        return;
    }

    try {
        McpSidecarRunResult r = mcpFuture_->get();
        mcpFuture_.reset();
        if (mcpScheduled_) {
            if (r.ok) {
                Logger::info("[AnalysisCoordinator] MCP static sidecar joined: " + r.detail);
            } else {
                Logger::warn("[AnalysisCoordinator] MCP static sidecar joined with issues: " + r.detail);
            }
        }
    } catch (const std::exception& ex) {
        mcpFuture_.reset();
        Logger::error(std::string("[AnalysisCoordinator] MCP static sidecar exception: ") + ex.what());
    } catch (...) {
        mcpFuture_.reset();
        Logger::error("[AnalysisCoordinator] MCP static sidecar unknown exception.");
    }
    mcpScheduled_ = false;
}
