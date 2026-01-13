#include "GdbScriptBuilder.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    std::string basenameFromPath(const std::string& path)
    {
        auto pos = path.find_last_of('/');
        if (pos == std::string::npos)
            return path;
        return path.substr(pos + 1);
    }

    bool parseHexOrDecUint64(const std::string& text, std::uint64_t& out)
    {
        try
        {
            // base 0 lets stoull handle "0x..." (hex) or plain decimal.
            out = std::stoull(text, nullptr, 0);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

bool GdbScriptBuilder::loadPlanFromJsonFile(const std::string& jsonPath,
    Plan& outPlan,
    std::string& errorOut)
{
    errorOut.clear();

    std::ifstream in(jsonPath);
    if (!in)
    {
        errorOut = "Failed to open JSON plan file: " + jsonPath;
        return false;
    }

    json root;
    try
    {
        in >> root;
    }
    catch (const std::exception& ex)
    {
        errorOut = std::string("Failed to parse JSON: ") + ex.what();
        return false;
    }

    if (!root.contains("targetPath") || !root["targetPath"].is_string())
    {
        errorOut = "JSON plan missing required string field 'targetPath'.";
        return false;
    }

    Plan plan;
    plan.targetPath = root["targetPath"].get<std::string>();
    plan.targetBasename = basenameFromPath(plan.targetPath);
    plan.useStarti = root.value("useStarti", true);

    if (!root.contains("breakpoints") || !root["breakpoints"].is_array())
    {
        errorOut = "JSON plan missing required array field 'breakpoints'.";
        return false;
    }

    for (const auto& bpJson : root["breakpoints"])
    {
        if (!bpJson.contains("offset"))
        {
            errorOut = "Breakpoint entry is missing required field 'offset'.";
            return false;
        }

        std::uint64_t offset = 0;
        if (bpJson["offset"].is_string())
        {
            const std::string offStr = bpJson["offset"].get<std::string>();
            if (!parseHexOrDecUint64(offStr, offset))
            {
                errorOut = "Failed to parse breakpoint offset: '" + offStr + "'";
                return false;
            }
        }
        else if (bpJson["offset"].is_number_unsigned())
        {
            offset = bpJson["offset"].get<std::uint64_t>();
        }
        else
        {
            errorOut = "Breakpoint 'offset' must be a string or unsigned integer.";
            return false;
        }

        BreakpointSpec bp;
        bp.offset = offset;
        bp.note = bpJson.value("note", std::string());
        bp.stopAfter = bpJson.value("stopAfter", false);

        if (bpJson.contains("actions"))
        {
            if (!bpJson["actions"].is_array())
            {
                errorOut = "Breakpoint 'actions' must be an array.";
                return false;
            }

            for (const auto& actJson : bpJson["actions"])
            {
                if (!actJson.contains("type") || !actJson["type"].is_string())
                {
                    errorOut = "Action missing required string field 'type'.";
                    return false;
                }

                const std::string typeStr = actJson["type"].get<std::string>();
                BreakpointAction action;

                if (typeStr == "snapshot")
                {
                    action.type = BreakpointAction::Type::Snapshot;
                    action.label = actJson.value("label", std::string());
                }
                else if (typeStr == "set_reg")
                {
                    action.type = BreakpointAction::Type::SetRegister;
                    if (!actJson.contains("reg") || !actJson["reg"].is_string())
                    {
                        errorOut = "set_reg action requires string field 'reg'.";
                        return false;
                    }
                    if (!actJson.contains("value") || !actJson["value"].is_string())
                    {
                        errorOut = "set_reg action requires string field 'value'.";
                        return false;
                    }
                    action.reg = actJson["reg"].get<std::string>();
                    action.valueExpr = actJson["value"].get<std::string>();
                }
                else if (typeStr == "gdb_cmd")
                {
                    action.type = BreakpointAction::Type::GdbCommand;
                    if (!actJson.contains("command") || !actJson["command"].is_string())
                    {
                        errorOut = "gdb_cmd action requires string field 'command'.";
                        return false;
                    }
                    action.command = actJson["command"].get<std::string>();
                }
                else if (typeStr == "shell")
                {
                    action.type = BreakpointAction::Type::ShellCommand;
                    if (!actJson.contains("command") || !actJson["command"].is_string())
                    {
                        errorOut = "shell action requires string field 'command'.";
                        return false;
                    }
                    action.command = actJson["command"].get<std::string>();
                }
                else if (typeStr == "temp_breakpoint")
                {
                    action.type = BreakpointAction::Type::TempBreakpoint;
                    
                    // Required: offset for the temporary breakpoint
                    if (!actJson.contains("offset"))
                    {
                        errorOut = "temp_breakpoint action requires 'offset' field.";
                        return false;
                    }
                    
                    std::uint64_t tempOffset = 0;
                    if (actJson["offset"].is_string())
                    {
                        const std::string offStr = actJson["offset"].get<std::string>();
                        if (!parseHexOrDecUint64(offStr, tempOffset))
                        {
                            errorOut = "Failed to parse temp_breakpoint offset: '" + offStr + "'";
                            return false;
                        }
                    }
                    else if (actJson["offset"].is_number_unsigned())
                    {
                        tempOffset = actJson["offset"].get<std::uint64_t>();
                    }
                    else
                    {
                        errorOut = "temp_breakpoint 'offset' must be a string or unsigned integer.";
                        return false;
                    }
                    
                    action.tempBreakpointOffset = tempOffset;
                    action.tempBreakpointNote = actJson.value("note", std::string());
                    action.tempBreakpointDeleteAfterHit = actJson.value("deleteAfterHit", true);
                    
                    // Optional: actions for the temp breakpoint
                    if (actJson.contains("actions") && actJson["actions"].is_array())
                    {
                        for (const auto& tempActJson : actJson["actions"])
                        {
                            if (!tempActJson.contains("type") || !tempActJson["type"].is_string())
                            {
                                errorOut = "Temp breakpoint action missing required string field 'type'.";
                                return false;
                            }
                            
                            const std::string tempTypeStr = tempActJson["type"].get<std::string>();
                            BreakpointAction tempAction;
                            
                            if (tempTypeStr == "snapshot")
                            {
                                tempAction.type = BreakpointAction::Type::Snapshot;
                                tempAction.label = tempActJson.value("label", std::string());
                            }
                            else if (tempTypeStr == "set_reg")
                            {
                                tempAction.type = BreakpointAction::Type::SetRegister;
                                if (!tempActJson.contains("reg") || !tempActJson["reg"].is_string())
                                {
                                    errorOut = "Temp breakpoint set_reg action requires string field 'reg'.";
                                    return false;
                                }
                                if (!tempActJson.contains("value") || !tempActJson["value"].is_string())
                                {
                                    errorOut = "Temp breakpoint set_reg action requires string field 'value'.";
                                    return false;
                                }
                                tempAction.reg = tempActJson["reg"].get<std::string>();
                                tempAction.valueExpr = tempActJson["value"].get<std::string>();
                            }
                            else if (tempTypeStr == "gdb_cmd")
                            {
                                tempAction.type = BreakpointAction::Type::GdbCommand;
                                if (!tempActJson.contains("command") || !tempActJson["command"].is_string())
                                {
                                    errorOut = "Temp breakpoint gdb_cmd action requires string field 'command'.";
                                    return false;
                                }
                                tempAction.command = tempActJson["command"].get<std::string>();
                            }
                            else if (tempTypeStr == "shell")
                            {
                                tempAction.type = BreakpointAction::Type::ShellCommand;
                                if (!tempActJson.contains("command") || !tempActJson["command"].is_string())
                                {
                                    errorOut = "Temp breakpoint shell action requires string field 'command'.";
                                    return false;
                                }
                                tempAction.command = tempActJson["command"].get<std::string>();
                            }
                            else
                            {
                                errorOut = "Unknown temp breakpoint action type: '" + tempTypeStr + "'";
                                return false;
                            }
                            
                            action.tempBreakpointActions.push_back(std::move(tempAction));
                        }
                    }
                }
                else
                {
                    errorOut = "Unknown action type: '" + typeStr + "'";
                    return false;
                }

                bp.actions.push_back(std::move(action));
            }
        }

        plan.breakpoints.push_back(std::move(bp));
    }

    outPlan = std::move(plan);
    return true;
}

std::string GdbScriptBuilder::buildScript(const Plan& plan)
{
    std::ostringstream ss;

    ss << "# Auto-generated GDB script by guardAIn / GdbScriptBuilder\n";
    ss << "# Target: " << plan.targetPath << "\n";
    ss << "# NOTE: Assumes you start gdb as: gdb --args " << plan.targetPath << " <sample-args-if-any>\n\n";

    // Global GDB settings
    ss << "set pagination off\n";
    ss << "set confirm off\n";
    ss << "set breakpoint pending on\n";
    ss << "set print pretty on\n\n";

    // Start the inferior so that /proc/<pid>/maps exists
    if (plan.useStarti)
        ss << "starti\n\n";
    else
        ss << "run\n\n";

    // Python prelude: compute $base and $pid
    ss << "python\n";
    ss << "import gdb, re\n";
    ss << "def _guardain_init(target_basename):\n";
    ss << "    out = gdb.execute('info proc mappings', to_string=True)\n";
    ss << "    base = None\n";
    ss << "    for line in out.splitlines():\n";
    ss << "        # Example line:\n";
    ss << "        # 0x0000555555554000 0x0000555555556000 ... r--p  /home/analyst/suspicious\n";
    ss << "        parts = line.split()\n";
    ss << "        if len(parts) < 6:\n";
    ss << "            continue\n";
    ss << "        perms = parts[4]\n";
    ss << "        path  = parts[-1]\n";
    ss << "        if 'r--' in perms and ('/' + target_basename) in path:\n";
    ss << "            try:\n";
    ss << "                base = int(parts[0], 16)\n";
    ss << "            except Exception:\n";
    ss << "                continue\n";
    ss << "            break\n";
    ss << "    if base is None:\n";
    ss << "        raise gdb.GdbError('guardAIn: failed to locate base mapping for ' + target_basename)\n";
    ss << "    gdb.execute('set $base = 0x%x' % base)\n";
    ss << "    pid = gdb.selected_inferior().pid\n";
    ss << "    gdb.execute('set $pid = %d' % pid)\n";
    ss << "_guardain_init('" << escapeForSingleQuotedPython(plan.targetBasename) << "')\n";
    ss << "end\n\n";

    // Install breakpoints
    for (const auto& bp : plan.breakpoints)
    {
        ss << "# Breakpoint at offset " << toHexOffset(bp.offset);
        if (!bp.note.empty())
            ss << "  ;  " << bp.note;
        ss << "\n";

        ss << "break *($base + " << toHexOffset(bp.offset) << ")\n";
        ss << "commands\n";
        ss << "  silent\n";
        ss << "  echo [guardAIn] breakpoint at $pc (offset " << toHexOffset(bp.offset) << ")";
        if (!bp.note.empty())
            ss << " - " << bp.note;
        ss << "\\n\n";

        // Actions for this breakpoint
        for (const auto& act : bp.actions)
        {
            switch (act.type)
            {
            case BreakpointAction::Type::Snapshot:
            {
                std::string label = act.label;
                if (label.empty())
                    label = "bp_" + toHexOffset(bp.offset);

                ss << "  echo [guardAIn] snapshot '" << label << "'\\n\n";
                ss << "  info registers\n";
                ss << "  x/16i $pc-16\n";
                ss << "  x/16gx $rsp\n";
                break;
            }

            case BreakpointAction::Type::SetRegister:
            {
                // Normalize register name to lowercase (GDB requires lowercase register names)
                std::string regLower = act.reg;
                std::transform(regLower.begin(), regLower.end(), regLower.begin(), ::tolower);
                ss << "  echo [guardAIn] set $" << regLower
                    << " = " << act.valueExpr << "\\n\n";
                ss << "  set $" << regLower << " = " << act.valueExpr << "\n";
                break;
            }

            case BreakpointAction::Type::GdbCommand:
            {
                ss << "  echo [guardAIn] gdb_cmd: " << act.command << "\\n\n";
                // Split commands on semicolons and execute each separately
                // GDB doesn't support semicolons in command blocks
                std::istringstream cmdStream(act.command);
                std::string singleCmd;
                while (std::getline(cmdStream, singleCmd, ';'))
                {
                    // Trim whitespace
                    singleCmd.erase(0, singleCmd.find_first_not_of(" \t"));
                    singleCmd.erase(singleCmd.find_last_not_of(" \t") + 1);
                    if (!singleCmd.empty())
                    {
                        ss << "  " << singleCmd << "\n";
                    }
                }
                break;
            }

            case BreakpointAction::Type::ShellCommand:
            {
                ss << "  echo [guardAIn] shell: " << act.command << "\\n\n";

                const std::string escapedCmd = escapeForSingleQuotedPython(act.command);

                ss << "  python\n";
                ss << "import gdb, os\n";
                ss << "pid = gdb.selected_inferior().pid\n";
                ss << "try:\n";
                ss << "    base = int(gdb.parse_and_eval('$base'))\n";
                ss << "except Exception:\n";
                ss << "    base = None\n";
                ss << "cmd = '" << escapedCmd << "'\n";
                ss << "cmd = cmd.replace('$pid', str(pid))\n";
                ss << "if base is not None:\n";
                ss << "    cmd = cmd.replace('$base', hex(base))\n";
                ss << "os.system(cmd)\n";
                ss << "end\n";
                break;
            }

            case BreakpointAction::Type::TempBreakpoint:
            {
                // Create a temporary breakpoint at the specified offset
                ss << "  echo [guardAIn] creating temp breakpoint at offset " 
                   << toHexOffset(act.tempBreakpointOffset);
                if (!act.tempBreakpointNote.empty())
                    ss << " - " << act.tempBreakpointNote;
                ss << "\\n\n";
                
                // Use tbreak (temporary breakpoint) - auto-deletes after first hit
                // We'll create it with commands inline
                ss << "  tbreak *($base + " << toHexOffset(act.tempBreakpointOffset) << ")\n";
                ss << "  commands\n";
                ss << "    silent\n";
                ss << "    echo [guardAIn] temp breakpoint hit at offset " 
                   << toHexOffset(act.tempBreakpointOffset);
                if (!act.tempBreakpointNote.empty())
                    ss << " - " << act.tempBreakpointNote;
                ss << "\\n\n";
                
                // Execute actions for the temp breakpoint
                for (const auto& tempAct : act.tempBreakpointActions)
                {
                    switch (tempAct.type)
                    {
                    case BreakpointAction::Type::Snapshot:
                    {
                        std::string label = tempAct.label;
                        if (label.empty())
                            label = "temp_bp_" + toHexOffset(act.tempBreakpointOffset);
                        
                        ss << "    echo [guardAIn] snapshot '" << label << "'\\n\n";
                        ss << "    info registers\n";
                        ss << "    x/16i $pc-16\n";
                        ss << "    x/16gx $rsp\n";
                        break;
                    }
                    
                    case BreakpointAction::Type::SetRegister:
                    {
                        // Normalize register name to lowercase (GDB requires lowercase register names)
                        std::string regLower = tempAct.reg;
                        std::transform(regLower.begin(), regLower.end(), regLower.begin(), ::tolower);
                        ss << "    echo [guardAIn] set $" << regLower
                           << " = " << tempAct.valueExpr << "\\n\n";
                        ss << "    set $" << regLower << " = " << tempAct.valueExpr << "\n";
                        break;
                    }
                    
                    case BreakpointAction::Type::GdbCommand:
                    {
                        ss << "    echo [guardAIn] gdb_cmd: " << tempAct.command << "\\n\n";
                        // Split commands on semicolons and execute each separately
                        // GDB doesn't support semicolons in command blocks
                        std::istringstream cmdStream(tempAct.command);
                        std::string singleCmd;
                        while (std::getline(cmdStream, singleCmd, ';'))
                        {
                            // Trim whitespace
                            singleCmd.erase(0, singleCmd.find_first_not_of(" \t"));
                            singleCmd.erase(singleCmd.find_last_not_of(" \t") + 1);
                            if (!singleCmd.empty())
                            {
                                ss << "    " << singleCmd << "\n";
                            }
                        }
                        break;
                    }
                    
                    case BreakpointAction::Type::ShellCommand:
                    {
                        ss << "    echo [guardAIn] shell: " << tempAct.command << "\\n\n";
                        const std::string escapedCmd = escapeForSingleQuotedPython(tempAct.command);
                        ss << "    python\n";
                        ss << "import gdb, os\n";
                        ss << "pid = gdb.selected_inferior().pid\n";
                        ss << "try:\n";
                        ss << "    base = int(gdb.parse_and_eval('$base'))\n";
                        ss << "except Exception:\n";
                        ss << "    base = None\n";
                        ss << "cmd = '" << escapedCmd << "'\n";
                        ss << "cmd = cmd.replace('$pid', str(pid))\n";
                        ss << "if base is not None:\n";
                        ss << "    cmd = cmd.replace('$base', hex(base))\n";
                        ss << "os.system(cmd)\n";
                        ss << "end\n";
                        break;
                    }
                    
                    case BreakpointAction::Type::TempBreakpoint:
                        // Nested temp breakpoints not supported
                        ss << "    echo [guardAIn] WARNING: Nested temp breakpoints not supported\\n\n";
                        break;
                    }
                }
                
                // After temp breakpoint actions, continue execution
                // Note: tbreak automatically deletes itself after first hit, so we don't need to delete it
                ss << "    continue\n";
                ss << "  end\n";
                break;
            }
            } // end switch
        }     // end actions loop

       // Now decide how to *leave* this breakpoint:
        if (bp.stopAfter) {
            ss << "  echo [guardAIn] terminating at breakpoint offset 0x"
                << std::hex << bp.offset << std::dec << "\\n\n";
            ss << "  quit\n";
        }
        else {
            ss << "  continue\n";
        }

        ss << "end\n\n";
    }

    // Finally, resume execution under breakpoint control.
    ss << "# Start execution under breakpoint control\n";
    ss << "continue\n";
    ss << "quit\n";
	ss << "\n";
	ss << "# End of guardAIn generated GDB script\n";

    return ss.str();
}

bool GdbScriptBuilder::writeScriptToFile(const std::string& jsonPath,
    const std::string& scriptPath,
    std::string& errorOut)
{
    Plan plan;
    if (!loadPlanFromJsonFile(jsonPath, plan, errorOut))
        return false;

    const std::string script = buildScript(plan);

    std::ofstream out(scriptPath, std::ios::binary);
    if (!out)
    {
        errorOut = "Failed to open output script file: " + scriptPath;
        return false;
    }

    out << script;
    if (!out.good())
    {
        errorOut = "Failed while writing script to: " + scriptPath;
        return false;
    }

    return true;
}

std::string GdbScriptBuilder::escapeForSingleQuotedPython(const std::string& s)
{
    // For now, we just escape backslashes and single quotes.
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\\'"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                out += c;
                break;
            }
        }
        return out;
    }
}

std::string GdbScriptBuilder::toHexOffset(std::uint64_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::nouppercase << value;
    return oss.str();
}