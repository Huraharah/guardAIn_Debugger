#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Supported action types in the debug plan
enum class DebugActionType {
    SetRegister,
    SnapshotContext,
    ShellCommand,
    Unknown
};

struct DebugAction {
    DebugActionType type = DebugActionType::Unknown;

    // For SetRegister
    std::string reg;
    uint64_t    value = 0;     // numeric value parsed from value_hex

    // For ShellCommand
    std::string cmd;
};

struct BreakpointSpec {
    std::string id;
    uint64_t    ghidraAddr = 0;   // address in Ghidra space
    std::vector<DebugAction> actions;
};

struct DebugPlan {
    std::string binaryName;
    uint64_t    ghidraBase = 0;          // e.g., 0x00100000
    std::vector<BreakpointSpec> breakpoints;
};

// Parse JSON text (LLM output / manual file) into a DebugPlan.
// Throws std::runtime_error on parse/validation errors.
DebugPlan parseDebugPlanFromJson(const std::string& jsonText);

// Optional helper if you want to load directly from a file path.
DebugPlan parseDebugPlanFromFile(const std::string& filePath);
