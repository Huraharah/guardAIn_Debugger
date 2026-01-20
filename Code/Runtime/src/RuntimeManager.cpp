#include "RuntimeManager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

RuntimeManager::RuntimeManager(RuntimeConfig& cfg)
    : cfg_(cfg),
	qemu_(cfg),
	ssh_(cfg),
	trace_(cfg)
{
}

std::string RuntimeManager::extractSampleName(const std::string& samplePath) const
{
    fs::path p(samplePath);
    auto stem = p.stem().string();
    return stem.empty() ? "sample" : stem;
}

std::string RuntimeManager::buildArtifactsRoot(const std::string& sampleName) const
{
    fs::path root(cfg_.artifactsRoot);
    root /= sampleName;
    return root.string();
}

std::string RuntimeManager::buildBaselineDir(const std::string& artifactsRoot) const
{
    fs::path p(artifactsRoot);
    p /= "baseline";
    return p.string();
}

std::string RuntimeManager::buildStaticDir(const std::string& artifactsRoot) const
{
    fs::path p(artifactsRoot);
    p /= "static";
    return p.string();
}

std::string RuntimeManager::buildRunDir(const std::string& artifactsRoot, int runIndex) const
{
    fs::path p(artifactsRoot);
    p /= ("run" + std::to_string(runIndex));
    return p.string();
}

std::string RuntimeManager::buildDebugDir(const std::string& artifactsRoot) const
{
    fs::path p(artifactsRoot);
    p /= "debug";
    return p.string();
}

void RuntimeManager::ensureDirectories(const std::string& path) const
{
    fs::create_directories(path);
}

bool RuntimeManager::prepareSampleImage(const std::string& sampleName,
    std::string& outSampleDiskPath)
{
    fs::path vmDir(cfg_.vmDirectory);
    ensureDirectories(vmDir.string());

    fs::path sampleDisk = vmDir / ("linux_" + sampleName + ".qcow2");
    outSampleDiskPath = sampleDisk.string();

    if (!fs::exists(sampleDisk)) {
        Logger::info("[RuntimeManager] Copying base image to per-sample image...");
        try {
            fs::copy_file(cfg_.baseImagePath, sampleDisk,
                fs::copy_options::overwrite_existing);
        }
        catch (const std::exception& ex) {
            Logger::error(std::string("[RuntimeManager] Failed to copy base image: ") + ex.what());
            return false;
        }

        // One-time boot WITHOUT -snapshot to scp sample into /home/analyst/
        Logger::info("[RuntimeManager] Booting per-sample image to inject sample...");

        qemu_.setDiskImagePath(outSampleDiskPath);

        if (!qemu_.startVm("prepare", false)) {
            Logger::error("[RuntimeManager] Failed to start VM for sample prep");
            return false;
        }

        if (!ssh_.waitForReady(cfg_.sshTimeoutSec)) {
            Logger::error("[RuntimeManager] SSH not ready during sample prep");
            qemu_.stopVm();
            return false;
        }

        if (!ssh_.copyTo(cfg_.sampleDirectory + cfg_.sampleName,
            "/home/" + cfg_.sshUser + "/" + sampleName)) {
            Logger::error("[RuntimeManager] Failed to copy sample into VM");
            qemu_.stopVm();
            return false;
        }

        if (!ssh_.runRemote("chmod +x /home/" + cfg_.sshUser + "/" + sampleName)) {
            Logger::error("[RuntimeManager] Failed to chmod sample in VM");
            qemu_.stopVm();
            return false;
        }

        ssh_.runRemote("sudo chmod +x /home/" + cfg_.sshUser + "/" + sampleName);

		// Graceful shutdown
        ssh_.runRemote(
            "sync; sudo sync; sudo shutdown -h now"
        );
        Logger::info("[RuntimeManager] Requested guest shutdown (prepare phase).");
        if (WaitForSingleObject(qemu_.getProcessHandle(), 15000) != 0)  // 15 seconds
        {
            Logger::warn("[RuntimeManager] QEMU did not exit within 15s after shutdown; forcing stop.");
            qemu_.stopVm(); // emergency sledgehammer
        }
        
        Logger::info("[RuntimeManager] Sample image ready: " + outSampleDiskPath);
		qemu_.setIsRunning(false);
    }
    else {
        Logger::info("[RuntimeManager] Sample disk already exists: " + outSampleDiskPath);
    }

    return true;
}

bool RuntimeManager::runInSnapshotVm(
    const std::string& vmName,
    const std::string& sampleDiskPath,
    const std::function<bool(SshHelper&)>& work)
{
    if (!qemu_.startVm(vmName, true)) {
        Logger::error("[RuntimeManager] Failed to start VM for " + vmName);
        return false;
    }

    if (!ssh_.waitForReady(cfg_.sshTimeoutSec)) {
        Logger::error("[RuntimeManager] SSH not ready for " + vmName);
        qemu_.stopVm();
        return false;
    }

    const bool ok = work(ssh_);
    ssh_.runRemote(
        "sync; sudo sync; sudo shutdown -h now"
    );
    Logger::info("[RuntimeManager] Requested guest shutdown.");
    if (WaitForSingleObject(qemu_.getProcessHandle(), 15000) != 0)  // 15 seconds
    {
        Logger::warn("[RuntimeManager] QEMU did not exit within 15s after shutdown; forcing stop.");
        qemu_.stopVm(); // emergency sledgehammer
    }
    else
    {
        Logger::info("[RuntimeManager] Guest shutdown complete.");
        qemu_.setIsRunning(false);
    }
    return ok;
}

bool RuntimeManager::runBaselineDiffPass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot)
{
    const auto baselineDir = buildBaselineDir(artifactsRoot);
    ensureDirectories(baselineDir);
    
    const auto staticDir = buildStaticDir(artifactsRoot);
    ensureDirectories(staticDir);

    Logger::info("[RuntimeManager] Starting baseline diff & Static Tools pass for sample: " + sampleName);

    return runInSnapshotVm("static&baseline", sampleDiskPath,
        [&](SshHelper& ssh) {

            // Static Tools
            ssh.runRemote("file /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".fileinfo");
            ssh.runRemote("sha256sum /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".sha256");
            ssh.runRemote("strings -a -t x /home/" + cfg_.sshUser + "/" + sampleName + " | head -n 2000 > /tmp/" + sampleName + ".strings");
            ssh.runRemote("binwalk /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".binwalk || true");
            ssh.runRemote("readelf -a /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".readelf");
            ssh.runRemote("objdump -d -M intel /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".objdump");

            // Pre-manifest
            ssh_.runRemote(
                "find /home/" + cfg_.sshUser + " /tmp - xdev "
                "-printf '%p|%s|%Y|%T@\\n' > /tmp/pre_exec.manifest 2>/dev/null || true");

            // Execute sample (we expect non-zero exit in many cases)
            if (!ssh_.runRemote("/home/" + cfg_.sshUser + "/" + sampleName)) {
                Logger::warn("[RuntimeManager] Sample exited with non-zero status during baseline diff.");
            }

            // Post-manifest
            ssh_.runRemote(
                "find /home/" + cfg_.sshUser + " /tmp - xdev "
                "-printf '%p|%s|%Y|%T@\\n' > /tmp/post_exec.manifest 2>/dev/null || true");

            // Diff
            ssh_.runRemote(
                "diff -u /tmp/pre_exec.manifest /tmp/post_exec.manifest "
                "> /tmp/manifest_diff.txt || true");

            // Copy artifacts back
            ssh.copyFrom("/tmp/" + sampleName + ".fileinfo", staticDir + "\\" + sampleName + ".fileinfo");
            ssh.copyFrom("/tmp/" + sampleName + ".sha256", staticDir + "\\" + sampleName + ".sha256");
            ssh.copyFrom("/tmp/" + sampleName + ".strings", staticDir + "\\" + sampleName + ".strings");
            ssh.copyFrom("/tmp/" + sampleName + ".binwalk", staticDir + "\\" + sampleName + ".binwalk");
            ssh.copyFrom("/tmp/" + sampleName + ".readelf", staticDir + "\\" + sampleName + ".readelf");
            ssh.copyFrom("/tmp/" + sampleName + ".objdump", staticDir + "\\" + sampleName + ".objdump");

            ssh_.copyFrom("/tmp/pre_exec.manifest",
                baselineDir + "\\pre_exec.manifest");
            ssh_.copyFrom("/tmp/post_exec.manifest",
                baselineDir + "\\post_exec.manifest");
            ssh_.copyFrom("/tmp/manifest_diff.txt",
                baselineDir + "\\manifest_diff.txt");

            Logger::info("[RuntimeManager] Baseline diff artifacts saved under: " + baselineDir + " & Static tool artifacts under: " + staticDir);

			/* This portion moved into runInSnapshotVm() to reduce code duplication - retained for historical reference
            ssh.runRemote(
                "sync; sudo sync; sudo shutdown -h now"
            );
            Logger::info("[RuntimeManager] Requested guest shutdown (static phase).");
            if (WaitForSingleObject(qemu_.getProcessHandle(), 15000) != 0)  // 15 seconds
            {
                Logger::warn("[RuntimeManager] QEMU did not exit within 15s after shutdown; forcing stop.");
                qemu_.stopVm(); // emergency sledgehammer
            }
            else
            {
				Logger::info("[RuntimeManager] Guest shutdown complete (static phase).");
				qemu_.setIsRunning(false);
            }*/
            return true;
        });
}

/* Refactored into baseline diff path to reduce number of system boots - retained for historical reference
bool RuntimeManager::runStaticToolsPass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot)
{
    const auto staticDir = buildStaticDir(artifactsRoot);
    ensureDirectories(staticDir);

    Logger::info("[RuntimeManager] Starting static tools pass for sample: " + sampleName);

    return runInSnapshotVm("static-tools", sampleDiskPath,
        [&](SshHelper& ssh) {
            ssh.runRemote("file /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".fileinfo");
            ssh.runRemote("sha256sum /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".sha256");
            ssh.runRemote("strings -a -t x /home/" + cfg_.sshUser + "/" + sampleName + " | head -n 2000 > /tmp/" + sampleName + ".strings");
            ssh.runRemote("binwalk /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName + ".binwalk || true");
            ssh.runRemote("readelf -a /home/" + cfg_.sshUser + "/" + sampleName + " > /tmp/" + sampleName+".readelf");
            ssh.runRemote("objdump -d -M intel /home/" + cfg_.sshUser + "/" + sampleName + " | head -n 2000 > /tmp/" + sampleName + ".objdump");

            ssh.copyFrom("/tmp/" + sampleName + ".fileinfo", staticDir + "\\" + sampleName + ".fileinfo");
            ssh.copyFrom("/tmp/" + sampleName + ".sha256", staticDir + "\\" + sampleName + ".sha256");
            ssh.copyFrom("/tmp/" + sampleName + ".strings", staticDir + "\\" + sampleName + ".strings");
            ssh.copyFrom("/tmp/" + sampleName + ".binwalk", staticDir + "\\" + sampleName + ".binwalk");
            ssh.copyFrom("/tmp/" + sampleName + ".readelf", staticDir + "\\" + sampleName + ".readelf");
            ssh.copyFrom("/tmp/" + sampleName + ".objdump", staticDir + "\\" + sampleName + ".objdump");

            Logger::info("[RuntimeManager] Static tool analysis artifacts saved under: " + staticDir);
            return true;
        });
}
*/

bool RuntimeManager::runStracePass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot,
    int runIndex)
{
    const auto runDir = buildRunDir(artifactsRoot, runIndex);
    ensureDirectories(runDir);

    const auto debugDir = buildDebugDir(artifactsRoot);
    ensureDirectories(debugDir);

    Logger::info("[RuntimeManager] Starting strace, ltrace, network captures, and initial GDB pass for sample: " + sampleName);

    return runInSnapshotVm("quickdynamics", sampleDiskPath,
        [&](SshHelper& ssh) {

            // strace with tcpdump
			ssh.runRemote("sudo tcpdump -i any -w /tmp/" + sampleName + ".pcap >/dev/null 2>&1 & echo $! > /tmp/tcpdump.pid");

            if (!ssh.runRemote("strace -f -o /tmp/" + sampleName + ".strace /home/" + cfg_.sshUser + "/" + sampleName)) {
                Logger::warn("[RuntimeManager] strace completed with non-zero exit (likely from target process). Continuing to fetch trace.");
            }

            ssh.runRemote("if [ -f /tmp/tcpdump.pid ];" "then kill \"$(cat /tmp/tcpdump.pid)\" 2>/dev/null || true;" "fi");

            ssh.copyFrom("/tmp/" + sampleName + ".strace", runDir + "\\" + sampleName + ".strace");
            ssh.copyFrom("/tmp/" + sampleName+".pcap", runDir + "\\" + sampleName+".pcap");

            Logger::info("[RuntimeManager] strace pass saved to: " + runDir + "\\" + sampleName + ".strace");
            Logger::info("[RuntimeManager] tcpdump pacp saved to: " + runDir + "\\" + sampleName + ".pcap");

            // ltrace with tshark
            ssh.runRemote("sudo tshark -i any -w /tmp/" + sampleName + "_tshark.pcap >/dev/null 2>&1 & echo $! > /tmp/tshark.pid");

            if (!ssh.runRemote("ltrace -f -o /tmp/" + sampleName + ".ltrace /home/" + cfg_.sshUser + "/" + sampleName)) {
                Logger::warn("[RuntimeManager] ltrace completed with non-zero exit (likely from process). Continuing to fetch trace.");
            }

            ssh.runRemote("if [ -f /tmp/tshark.pid ];" "then kill \"$(cat /tmp/tshark.pid)\" 2>/dev/null || true;" "fi");

            ssh.runRemote(
                "if [ -f /tmp/" + sampleName + "_tshark.pcap ]; then "
                "sudo chown analyst:analyst /tmp/" + sampleName + "_tshark.pcap 2>/dev/null || true; "
                "sudo chmod 644 /tmp/" + sampleName + "_tshark.pcap 2>/dev/null || true; "
                "fi"
            );

            ssh.copyFrom("/tmp/" + sampleName + ".ltrace", runDir + "\\" + sampleName + ".ltrace");
            ssh.copyFrom("/tmp/" + sampleName + "_tshark.pcap", runDir + "\\" + sampleName + "_tshark.pcap");

            Logger::info("[RuntimeManager] ltrace log saved to: " + runDir + "\\" + sampleName + ".ltrace");
            Logger::info("[RuntimeManager] tshark pcap saved to: " + runDir + "\\" + sampleName + "_tshark.pcap");

            // initial GDB run
            const std::string gdbCmd =
                "gdb -q -batch "
                "-ex 'set pagination off' "
                "-ex 'set confirm off' "
                "-ex 'break main' "
                "-ex 'run' "
                "-ex 'info registers' "
                "-ex 'bt' "
                "-- /home/" + cfg_.sshUser + "/" + sampleName +
                " > /tmp/" + sampleName + ".gdb.txt 2>&1";

            Logger::info("[RuntimeManager] Running remote gdb:\n    " + gdbCmd);

            ssh.runRemote(gdbCmd);

            ssh.copyFrom("/tmp/" + sampleName + ".gdb.txt",
                debugDir + "\\debug.log");

            Logger::info("[RuntimeManager] Debug log saved to: " + debugDir + "\\debug.log");

			/* This portion moved into runInSnapshotVm() to reduce code duplication - retained for historical reference
            ssh.runRemote("sync; sudo sync; sudo shutdown -h now");

            Logger::info("[RuntimeManager] Requested guest shutdown (early debug phase).");
            if (WaitForSingleObject(qemu_.getProcessHandle(), 15000) != 0)  // 15 seconds
            {
                Logger::warn("[RuntimeManager] QEMU did not exit within 15s after shutdown; forcing stop.");
                qemu_.stopVm(); // emergency sledgehammer
            }
            else
            {
				Logger::info("[RuntimeManager] Guest shutdown complete (early debug phase).");
				qemu_.setIsRunning(false);
            }*/
            return true;
        });
}

/* Function refactored into same boot cycle as strace to reduce boot cycles - retained for historical reference
bool RuntimeManager::runLtracePass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot,
    int runIndex)
{
    const auto runDir = buildRunDir(artifactsRoot, runIndex);
    ensureDirectories(runDir);

    Logger::info("[RuntimeManager] Starting ltrace pass for sample: " + sampleName);

    return runInSnapshotVm("ltrace-pass", sampleDiskPath,
        [&](SshHelper& ssh) {
            ssh.runRemote("sudo tshark -i any -w /tmp/" + sampleName + "_tshark.pcap >/dev/null 2>&1 & echo $! > /tmp/tshark.pid");

            if (!ssh.runRemote("ltrace -f -o /tmp/" + sampleName + ".ltrace /home/" + cfg_.sshUser + "/" + sampleName)) {
                Logger::warn("[RuntimeManager] ltrace completed with non-zero exit (likely from process). Continuing to fetch trace.");
            }

            ssh.runRemote("if [ -f /tmp/tshark.pid ];" "then kill \"$(cat /tmp/tshark.pid)\" 2>/dev/null || true;" "fi");

            ssh.copyFrom("/tmp/" + sampleName + ".ltrace", runDir + "\\" + sampleName + ".ltrace");
            ssh.copyFrom("/tmp/" + sampleName + "_tshark.pcap", runDir + "\\" + sampleName + "_tshark.pcap");

            Logger::info("[RuntimeManager] ltrace log saved to: " + runDir + "\\" + sampleName + ".ltrace");
           
            return true;
        });
}
*/

/* Function refactored into the same strace cycle to reduce boot cycles, with a new guided debug pass under the same method name - retained for historical reference
bool RuntimeManager::runDebugPass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot)
{
    const auto debugDir = buildDebugDir(artifactsRoot);
    ensureDirectories(debugDir);

    Logger::info("[RuntimeManager] Starting debug pass for sample: " + sampleName);

    return runInSnapshotVm("debug-pass", sampleDiskPath,
        [&](SshHelper& ssh) {
 
            const std::string gdbCmd =
                "gdb -q -batch "
                "-ex 'set pagination off' "
                "-ex 'set confirm off' "
                "-ex 'break main' "
                "-ex 'run' "
                "-ex 'info registers' "
                "-ex 'bt' "
                "-- /home/" + cfg_.sshUser + "/" + sampleName +
                " > /tmp/" + sampleName + ".gdb.txt 2>&1";

            Logger::info("[RuntimeManager] Running remote gdb:\n    " + gdbCmd);

            ssh.runRemote(gdbCmd);

            ssh.copyFrom("/tmp/" + sampleName + ".gdb.txt",
                debugDir + "\\debug.log");

            Logger::info("[RuntimeManager] Debug log saved to: " + debugDir + "\\debug.log");
            return true;
        });
}
*/

static std::string readFileToString(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool RuntimeManager::runDebugPass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot)
{
    const auto debugDir = buildDebugDir(artifactsRoot);
    ensureDirectories(debugDir);

    Logger::info("[RuntimeManager] Starting debug pass for sample: " + sampleName);

    // NEW APPROACH: GDB script is generated directly by LLM (no JSON→GDB translation)
    const std::string gdbScriptHost = debugDir + "/plan.gdb";
    
    // Verify the GDB script exists (should have been created by generateDebugPlan)
    if (!fs::exists(gdbScriptHost)) {
        Logger::error("[RuntimeManager] GDB script not found: " + gdbScriptHost);
        Logger::error("[RuntimeManager] LLM should have generated this file in generateDebugPlan()");
        return false;
    }
    
    Logger::info("[RuntimeManager] Using LLM-generated GDB script: " + gdbScriptHost);

    /* OLD APPROACH (JSON-based): commented out for potential reversion
    // 1) Paths on the HOST
    const std::string planJsonHost = artifactsRoot + "/LLM/plan.json";
    const std::string gdbScriptHost = debugDir + "/plan.gdb";
    const std::string gdbErrorScript = debugDir + "/plan_error.gdb";

    // 2) Build GDB script from the LLM JSON plan
    std::string errorMsg;
    if (!GdbScriptBuilder::writeScriptToFile(planJsonHost, gdbScriptHost, errorMsg))
    {
        Logger::error("[RuntimeManager] Failed to build GDB script from plan: " + errorMsg);

        // Optionally also dump the error script path
        Logger::error("[RuntimeManager] See " + gdbErrorScript + " for partial script / diagnostics (if written).");
        return false;
    }
    */

    // 3) Run this inside a fresh snapshot VM, using the existing helper.
    return runInSnapshotVm("debug-pass", sampleDiskPath,
        [&](SshHelper& ssh) -> bool
        {
            // 3a) Remote paths
            const std::string remoteHome = "/home/" + cfg_.sshUser;      // from RuntimeConfig
            const std::string remoteScript = remoteHome + "/plan.gdb";
            const std::string remoteSample = remoteHome + "/" + sampleName;
            const std::string remoteGdbLog = "/tmp/" + sampleName + ".gdb.log";
            const std::string remoteMemfd = "/tmp/memfd_dump.bin";

            // 3b) Copy the script into the guest
            if (!ssh.copyTo(gdbScriptHost, remoteScript))
            {
                Logger::error("[RuntimeManager] Failed to copy plan.gdb into guest.");
                return false;
            }

            // 3c) Build and run the GDB command in the guest.
            //     We run in batch mode so GDB exits when the script finishes.
            std::string gdbCmd =
                "cd " + remoteHome + " && "
                "gdb -q -batch -x " + remoteScript +
                " -- " + remoteSample +
                " > " + remoteGdbLog + " 2>&1";

            Logger::info("[RuntimeManager] Running remote GDB: " + gdbCmd);

            if (!ssh.runRemote(gdbCmd))
            {
                Logger::error("[RuntimeManager] Remote GDB command reported failure (ssh runRemote). "
                    "Will still attempt to collect artifacts.");
            }

            // 3d) Pull back GDB log
            const std::string localGdbLog = debugDir + "/" + sampleName + ".gdb.log";
            if (!ssh.copyFrom(remoteGdbLog, localGdbLog))
            {
                Logger::warn("[RuntimeManager] Failed to copy GDB log from guest: " + remoteGdbLog);
            }

            // 3e) Pull back memfd dump (if any)
            const std::string localMemfd = debugDir + "/memfd_dump.bin";
            if (!ssh.copyFrom(remoteMemfd, localMemfd))
            {
                Logger::warn("[RuntimeManager] No memfd dump copied (file may not exist): " + remoteMemfd);
            }
            else
            {
                Logger::info("[RuntimeManager] Retrieved memfd dump: " + localMemfd);
            }

            // You can add more artifact pulls here later (e.g., /tmp/other_dump.bin)
            return true;
        });
}

bool RuntimeManager::analyzeSample()
{
	const std::string samplePath = cfg_.sampleDirectory + cfg_.sampleName;
    Logger::info("[RuntimeManager] Starting full analysis for sample: " + samplePath);

    const std::string sampleName = extractSampleName(samplePath);
    const std::string artifactsRoot = buildArtifactsRoot(sampleName);
    ensureDirectories(artifactsRoot);
	//Logger::debug("[RuntimeManager] Key File: " + cfg_.sshKeyPath);   - [!] Bug Squashed, retained for historical reference

    std::string sampleDiskPath;
    if (!prepareSampleImage(sampleName, sampleDiskPath)) {
        Logger::error("[RuntimeManager] Failed to prepare sample image");
        return false;
    }

	std::this_thread::sleep_for(2s); // brief pause

    // 1) Baseline diff + static tools
    if (!runBaselineDiffPass(sampleName, sampleDiskPath, artifactsRoot)) {
        Logger::warn("[RuntimeManager] Baseline diff pass failed (continuing)");
    }

    /* Function no longer exists - moved into diff function
    if (!runStaticToolsPass(sampleName, sampleDiskPath, artifactsRoot)) {
        Logger::warn("[RuntimeManager] Static tools pass failed (continuing)");
    }
    */

    std::this_thread::sleep_for(2s); // brief pause

    // 2) Dynamic tracing
    const int runIndex = 1; // later you can loop this or branch paths
    if (!runStracePass(sampleName, sampleDiskPath, artifactsRoot, runIndex)) {
        Logger::warn("[RuntimeManager] strace pass failed (continuing)");
    }

    /* Functions no longer exists - moved into strace function
    if (!runLtracePass(sampleName, sampleDiskPath, artifactsRoot, runIndex)) {
        Logger::warn("[RuntimeManager] ltrace pass failed (continuing)");
    }
    */

    std::this_thread::sleep_for(2s); // brief pause

    // 2.5) Generate LLM debug plan from static analysis
    const std::string targetPath = "/home/" + cfg_.sshUser + "/" + sampleName;
    std::string llmError;
    if (!LlmInterface::generateDebugPlan(artifactsRoot, sampleName, targetPath, llmError)) {
        Logger::error("[RuntimeManager] Failed to generate LLM debug plan: " + llmError);
        Logger::warn("[RuntimeManager] Continuing without LLM-generated plan (debug pass may fail)");
    } else {
        Logger::info("[RuntimeManager] LLM debug plan generated successfully");
    }

    // 3) Debug snapshot
    if (!runDebugPass(sampleName, sampleDiskPath, artifactsRoot)) {
        Logger::warn("[RuntimeManager] debug pass failed (continuing)");
    }
    
    std::this_thread::sleep_for(2s); // brief pause

    // 4) Collate everything into JSON
    const auto summaryDir = (fs::path(artifactsRoot) / "summary").string();
    ensureDirectories(summaryDir);
    const auto summaryPath = (fs::path(summaryDir) / "summary.json").string();

	Logger::debug("[RuntimeManager] Building TraceSummary for sample: " + sampleName);

    TraceSummary summary;
    if (!trace_.buildSummary(artifactsRoot, sampleName, summary)) {
        Logger::warn("[RuntimeManager] Failed to build TraceSummary summary");
    }
    else if (!trace_.writeSummaryJson(summary, summaryPath)) {
        Logger::warn("[RuntimeManager] Failed to write summary JSON to: " + summaryPath);
    }
    else {
        Logger::info("[RuntimeManager] Summary JSON written to: " + summaryPath);
    }

    return true;
}
