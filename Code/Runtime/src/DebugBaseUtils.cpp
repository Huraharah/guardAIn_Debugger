#include "DebugBaseUtils.h"

#include <sstream>
#include <fstream>
#include <algorithm>

static std::string trim(const std::string& s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

uint64_t computeTextBaseFromMappings(const std::string& mappingsText,
    const std::string& binaryName)
{
    std::istringstream iss(mappingsText);
    std::string line;
    uint64_t best = 0;

    // We’ll treat binaryName as a suffix to match, e.g. "suspicious"
    while (std::getline(iss, line)) {
        std::string tline = trim(line);
        if (tline.empty()) continue;

        // We expect path at the end. Quick filter: must contain the binaryName.
        if (tline.find(binaryName) == std::string::npos) {
            continue;
        }

        std::istringstream ls(tline);
        std::string startStr, endStr, perms;

        // Columns are typically:
        // Start Addr, End Addr, Size, Offset, Perms, objfile
        if (!(ls >> startStr >> endStr)) {
            continue;
        }

        // Skip "Size" and "Offset"
        std::string sizeStr, offsetStr;
        if (!(ls >> sizeStr >> offsetStr)) {
            continue;
        }

        if (!(ls >> perms)) {
            continue;
        }

        // We only care about executable mappings.
        if (perms.rfind("r-x", 0) != 0 && perms.rfind("r-xp", 0) != 0) {
            continue;
        }

        try {
            uint64_t start = std::stoull(startStr, nullptr, 16);

            // Choose the lowest executable mapping for the binary as "base".
            if (best == 0 || start < best) {
                best = start;
            }
        }
        catch (...) {
            // Ignore bad lines.
        }
    }

    return best;
}