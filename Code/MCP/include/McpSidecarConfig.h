#pragma once

#include <string>

// Parameters for the static MCP/Ghidra sidecar (host-side analysis, parallel to VM passes).
struct McpSidecarConfig
{
    bool enabled = true;
    // Absolute path to the sample binary on the analyst machine (same file VM runs).
    std::string sampleBinaryHostPath;
    std::string artifactsRoot;
    // Ghidra install root (contains support/analyzeHeadless*). Empty = sidecar tries GHIDRA_INSTALL_DIR env.
    std::string ghidraInstallDir;
};
