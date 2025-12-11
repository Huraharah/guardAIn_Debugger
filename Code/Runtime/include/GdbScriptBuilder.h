#pragma once

#include <string>
#include <vector>
#include <cstdint>

class GdbScriptBuilder
{
public:
    struct BreakpointAction
    {
        enum class Type
        {
            Snapshot,
            SetRegister,
            GdbCommand,
            ShellCommand
        };

        Type        type = Type::Snapshot;
        std::string label;       // for Snapshot
        std::string reg;         // for SetRegister
        std::string valueExpr;   // for SetRegister (e.g. "0xffffffff")
        std::string command;     // for GdbCommand / ShellCommand
    };

    struct BreakpointSpec
    {
        std::uint64_t                  offset = 0;      // offset from ELF base
        std::string                    note;            // human-readable description
        bool                           stopAfter = false;
        std::vector<BreakpointAction>  actions;
    };

    struct Plan
    {
        std::string              targetPath;     // /home/analyst/suspicious
        std::string              targetBasename; // suspicious
        bool                     useStarti = true;
        std::vector<BreakpointSpec> breakpoints;
    };

    // Load a Plan from a JSON file produced by the LLM.
    // Returns false on parse/validation error, with human-readable message in errorOut.
    static bool loadPlanFromJsonFile(const std::string& jsonPath,
        Plan& outPlan,
        std::string& errorOut);

    // Build the full .gdb script as a single string.
    static std::string buildScript(const Plan& plan);

    // Convenience helper: load plan, build script, and write it to disk.
    static bool writeScriptToFile(const std::string& jsonPath,
        const std::string& scriptPath,
        std::string& errorOut);

private:
    static std::string escapeForSingleQuotedPython(const std::string& s);
    static std::string toHexOffset(std::uint64_t value);
};
