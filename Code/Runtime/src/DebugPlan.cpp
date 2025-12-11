#include "DebugPlan.h"

#include <stdexcept>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "nlohmann/json.hpp"

using nlohmann::json;

namespace {

    // Helper: parse "0xdeadbeef" or "12345" into uint64_t
    uint64_t parseUint64FromString(const std::string& s) {
        // base 0 lets stoull accept "0x", "0", etc.
        try {
            return std::stoull(s, nullptr, 0);
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("Failed to parse uint64 from string: '" + s +
                "': " + ex.what());
        }
    }

    DebugActionType parseActionType(const std::string& t) {
        if (t == "set_reg")          return DebugActionType::SetRegister;
        if (t == "snapshot_context") return DebugActionType::SnapshotContext;
        if (t == "shell_cmd")        return DebugActionType::ShellCommand;
        return DebugActionType::Unknown;
    }

} // anonymous namespace

DebugPlan parseDebugPlanFromJson(const std::string& jsonText) {
    json j;
    try {
        j = json::parse(jsonText);
    }
    catch (const std::exception& ex) {
        throw std::runtime_error(std::string("JSON parse error: ") + ex.what());
    }

    DebugPlan plan;

    // binary_name (required)
    if (!j.contains("binary_name") || !j["binary_name"].is_string()) {
        throw std::runtime_error("DebugPlan JSON missing 'binary_name' string");
    }
    plan.binaryName = j["binary_name"].get<std::string>();

    // ghidra_base (required, string like "0x00100000")
    if (!j.contains("ghidra_base") || !j["ghidra_base"].is_string()) {
        throw std::runtime_error("DebugPlan JSON missing 'ghidra_base' string");
    }
    plan.ghidraBase = parseUint64FromString(j["ghidra_base"].get<std::string>());

    // breakpoints (required, array)
    if (!j.contains("breakpoints") || !j["breakpoints"].is_array()) {
        throw std::runtime_error("DebugPlan JSON missing 'breakpoints' array");
    }

    for (const auto& jb : j["breakpoints"]) {
        BreakpointSpec bp;

        if (!jb.contains("id") || !jb["id"].is_string()) {
            throw std::runtime_error("Breakpoint missing 'id' string");
        }
        bp.id = jb["id"].get<std::string>();

        if (!jb.contains("ghidra_addr") || !jb["ghidra_addr"].is_string()) {
            throw std::runtime_error("Breakpoint '" + bp.id +
                "' missing 'ghidra_addr' string");
        }
        bp.ghidraAddr = parseUint64FromString(jb["ghidra_addr"].get<std::string>());

        // actions (optional; default empty)
        if (jb.contains("actions")) {
            if (!jb["actions"].is_array()) {
                throw std::runtime_error("Breakpoint '" + bp.id +
                    "' has non-array 'actions'");
            }

            for (const auto& ja : jb["actions"]) {
                if (!ja.contains("type") || !ja["type"].is_string()) {
                    throw std::runtime_error("Action in breakpoint '" + bp.id +
                        "' missing 'type' string");
                }

                DebugAction act;
                act.type = parseActionType(ja["type"].get<std::string>());

                switch (act.type) {
                case DebugActionType::SetRegister: {
                    if (!ja.contains("reg") || !ja["reg"].is_string()) {
                        throw std::runtime_error("set_reg action in breakpoint '" +
                            bp.id + "' missing 'reg'");
                    }
                    if (!ja.contains("value_hex") || !ja["value_hex"].is_string()) {
                        throw std::runtime_error("set_reg action in breakpoint '" +
                            bp.id + "' missing 'value_hex'");
                    }
                    act.reg = ja["reg"].get<std::string>();
                    act.value = parseUint64FromString(
                        ja["value_hex"].get<std::string>());
                    break;
                }

                case DebugActionType::SnapshotContext:
                    // no extra fields required
                    break;

                case DebugActionType::ShellCommand:
                    if (!ja.contains("cmd") || !ja["cmd"].is_string()) {
                        throw std::runtime_error("shell_cmd action in breakpoint '" +
                            bp.id + "' missing 'cmd'");
                    }
                    act.cmd = ja["cmd"].get<std::string>();
                    break;

                case DebugActionType::Unknown:
                default:
                    throw std::runtime_error("Unknown action type in breakpoint '" +
                        bp.id + "'");
                }

                bp.actions.push_back(std::move(act));
            }
        }

        plan.breakpoints.push_back(std::move(bp));
    }

    if (plan.breakpoints.empty()) {
        throw std::runtime_error("DebugPlan has no breakpoints");
    }

    return plan;
}

DebugPlan parseDebugPlanFromFile(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs) {
        throw std::runtime_error("Failed to open DebugPlan JSON file: " + filePath);
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return parseDebugPlanFromJson(oss.str());
}
