#pragma once

#include "McpSidecarConfig.h"
#include <string>

struct McpSidecarRunResult
{
    bool ok = false;
    std::string detail;
};

class McpSidecar
{
public:
    // Runs the Python driver (Ghidra headless + artifact synthesis). Blocks until complete.
    static McpSidecarRunResult runBlocking(const McpSidecarConfig& cfg);
};
