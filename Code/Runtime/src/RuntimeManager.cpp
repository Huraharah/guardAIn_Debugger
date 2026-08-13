#include "RuntimeManager.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>

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
        qemu_.setDiskImagePath(outSampleDiskPath);
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
        Logger::warn("[RuntimeManager] QEMU did not exit within 15s after shutdown; trying monitor quit.");
        QemuMonitorClient mon(cfg_.qemuMonitorHost, cfg_.qemuMonitorPort);
        if (mon.connectToServer()) {
            mon.sendCommand("quit");
            mon.disconnect();
            Logger::info("[RuntimeManager] Sent quit to QEMU monitor.");
            if (WaitForSingleObject(qemu_.getProcessHandle(), 5000) != 0) {
                Logger::warn("[RuntimeManager] QEMU still running after monitor quit; forcing stop.");
                qemu_.stopVm();
            } else {
                qemu_.setIsRunning(false);
            }
        } else {
            Logger::warn("[RuntimeManager] Could not connect to QEMU monitor; forcing stop.");
            qemu_.stopVm();
        }
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

RuntimeManager::DebugResult RuntimeManager::parseGdbLogResult(const std::string& gdbLogPath)
{
    std::ifstream logFile(gdbLogPath);
    if (!logFile.is_open()) {
        Logger::warn("[RuntimeManager] Could not open GDB log: " + gdbLogPath);
        return DebugResult::Unknown;
    }

    std::string line;
    std::string logContent;
    while (std::getline(logFile, line)) {
        logContent += line + "\n";
    }

    // Check for success message (e.g., "You achieved level 1!")
    if (logContent.find("You achieved level") != std::string::npos ||
        logContent.find("achieved level") != std::string::npos ||
        logContent.find("[+]") != std::string::npos) {
        return DebugResult::Success;
    }

    // Check for failure messages
    if (logContent.find("You are not leet enough") != std::string::npos ||
        logContent.find("not leet enough") != std::string::npos ||
        logContent.find("[*]") != std::string::npos) {
        return DebugResult::Failure;
    }

    // Check for usage messages (indicates wrong arguments)
    if (logContent.find("Usage:") != std::string::npos ||
        logContent.find("usage:") != std::string::npos ||
        logContent.find("<password>") != std::string::npos) {
        return DebugResult::Failure;
    }

    // Check for errors/signals
    if (logContent.find("SIGSEGV") != std::string::npos ||
        logContent.find("SIGTRAP") != std::string::npos ||
        logContent.find("SIGKILL") != std::string::npos ||
        logContent.find("Program received signal") != std::string::npos ||
        logContent.find("inferior process exited") != std::string::npos ||
        logContent.find("Error in sourced command file") != std::string::npos) {
        return DebugResult::Error;
    }

    return DebugResult::Unknown;
}

RuntimeManager::ModelRunResult RuntimeManager::runModelIterations(
    const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot,
    const std::string& modelName,
    const std::string& modelDebugDir,
    const std::string& promptPath)
{
    ModelRunResult result;
    result.success = false;
    result.iterations = 0;
    
    std::string previousResponseId;
    std::string previousConversationHistoryPath;
    std::string previousGdbLogPath;
    std::string provider = LlmInterface::determineProviderFromModel(modelName);
    const int maxIterations = 10;  // Prevent infinite loops
    std::string llmError;
    
    Logger::info("[RuntimeManager] Running model: " + modelName + " (provider: " + provider + ")");
    Logger::info("[RuntimeManager] Artifacts directory: " + modelDebugDir);
    
    while (result.iterations < maxIterations && !result.success) {
        result.iterations++;
        Logger::info("[RuntimeManager] [" + modelName + "] Iteration " + std::to_string(result.iterations));

        // Per-iteration paths (plan_iterN.gdb, sample_iterN.gdb.log)
        const std::string gdbScriptPath = (fs::path(modelDebugDir) / ("plan_iter" + std::to_string(result.iterations) + ".gdb")).string();
        const std::string gdbLogPath = (fs::path(modelDebugDir) / (sampleName + "_iter" + std::to_string(result.iterations) + ".gdb.log")).string();
        std::string responseId;
        std::string conversationHistoryPath;

        if (!LlmInterface::invokeLlmForGdbScript(
            promptPath,
            gdbScriptPath,
            llmError,
            &responseId,
            previousResponseId,
            previousGdbLogPath,
            modelName,
            previousConversationHistoryPath,
            &conversationHistoryPath)) {
            Logger::error("[RuntimeManager] [" + modelName + "] Failed to generate GDB script: " + llmError);
            break;
        }

        Logger::info("[RuntimeManager] [" + modelName + "] Generated GDB script: " + gdbScriptPath);
        if (!responseId.empty()) {
            Logger::info("[RuntimeManager] [" + modelName + "] Response ID: " + responseId);
        }

        // Copy the generated script to plan.gdb for execution
        fs::copy_file(gdbScriptPath, (fs::path(modelDebugDir) / "plan.gdb"), fs::copy_options::overwrite_existing);

        // Run debug pass, writing log to iteration-specific path and capturing artifacts
        // Pass modelDebugDir instead of artifactsRoot so artifacts go to the right place
        if (!runDebugPassWithDir(sampleName, sampleDiskPath, modelDebugDir, gdbLogPath, result.iterations)) {
            Logger::warn("[RuntimeManager] [" + modelName + "] Debug pass failed in iteration " + std::to_string(result.iterations));
        }

        // Check result from this iteration's log
        DebugResult debugResult = parseGdbLogResult(gdbLogPath);
        if (debugResult == DebugResult::Success) {
            Logger::info("[RuntimeManager] [" + modelName + "] SUCCESS! Flag extracted in iteration " + std::to_string(result.iterations));
            result.success = true;
            break;
        } else if (debugResult == DebugResult::Failure) {
            Logger::info("[RuntimeManager] [" + modelName + "] Iteration " + std::to_string(result.iterations) + " failed - refining...");
            if (provider == "openai") {
                previousResponseId = responseId;
            } else {
                previousConversationHistoryPath = conversationHistoryPath;
            }
            previousGdbLogPath = gdbLogPath;
            // Continue to next iteration
        } else if (debugResult == DebugResult::Error) {
            Logger::warn("[RuntimeManager] [" + modelName + "] Iteration " + std::to_string(result.iterations) + " encountered an error - refining...");
            if (provider == "openai") {
                previousResponseId = responseId;
            } else {
                previousConversationHistoryPath = conversationHistoryPath;
            }
            previousGdbLogPath = gdbLogPath;
            // Continue to next iteration
        } else {
            Logger::warn("[RuntimeManager] [" + modelName + "] Could not determine result from GDB log - stopping");
            break;
        }
    }

    if (!result.success) {
        Logger::warn("[RuntimeManager] [" + modelName + "] Iterative refinement did not achieve success after " + 
            std::to_string(result.iterations) + " iterations");
    }
    
    return result;
}

bool RuntimeManager::runDebugPass(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& artifactsRoot,
    const std::string& localGdbLogPath,
    int iteration)
{
    const auto debugDir = buildDebugDir(artifactsRoot);
    return runDebugPassWithDir(sampleName, sampleDiskPath, debugDir, localGdbLogPath, iteration);
}

bool RuntimeManager::runDebugPassWithDir(const std::string& sampleName,
    const std::string& sampleDiskPath,
    const std::string& debugDir,
    const std::string& localGdbLogPath,
    int iteration)
{
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
            // 3a) Remote paths (remote log always same name; we copy to iteration-specific local path)
            const std::string remoteHome = "/home/" + cfg_.sshUser;      // from RuntimeConfig
            const std::string remoteScript = remoteHome + "/plan.gdb";
            const std::string remoteSample = remoteHome + "/" + sampleName;
            const std::string remoteGdbLog = "/tmp/" + sampleName + ".gdb.log";
            const std::string remoteMemfd = "/tmp/memfd_dump.bin";
            const std::string localGdbLog = localGdbLogPath.empty()
                ? (debugDir + "/" + sampleName + ".gdb.log")
                : localGdbLogPath;

            // 3b) Copy the script into the guest
            if (!ssh.copyTo(gdbScriptHost, remoteScript))
            {
                Logger::error("[RuntimeManager] Failed to copy plan.gdb into guest.");
                return false;
            }

            // 3c) Build and run the GDB command in the guest, with watchdog for forward progress.
            std::string gdbCmd =
                "cd " + remoteHome + " && "
                "gdb -q -batch -x " + remoteScript +
                " -- " + remoteSample +
                " > " + remoteGdbLog + " 2>&1";

            Logger::info("[RuntimeManager] Running remote GDB: " + gdbCmd);
            Logger::debug("[Watchdog] Forward-progress check enabled (stall timeout: " + std::to_string(cfg_.debugStallTimeoutSec) + "s, poll every " + std::to_string(cfg_.debugStallPollIntervalSec) + "s).");

            std::atomic<bool> gdbFinished{false};
            std::atomic<bool> stallDetected{false};

            std::thread gdbThread([&]() {
                bool ok = ssh.runRemote(gdbCmd);
                gdbFinished = true;
                if (!ok)
                    Logger::error("[RuntimeManager] Remote GDB command reported failure (ssh runRemote). Will still attempt to collect artifacts.");
            });

            std::thread watchdogThread([&]() {
                const int stallSec = cfg_.debugStallTimeoutSec;
                const int pollSec = cfg_.debugStallPollIntervalSec;
                size_t lastSize = 0;
                bool everSawProgress = false;
                auto lastProgressTime = std::chrono::steady_clock::now();

                while (!gdbFinished.load() && !stallDetected.load()) {
                    std::this_thread::sleep_for(std::chrono::seconds(pollSec));
                    if (gdbFinished.load()) break;

                    SshHelper watchSsh(cfg_);
                    std::string out;
                    std::string statCmd = "stat -c %s " + remoteGdbLog + " 2>/dev/null || echo 0";
                    if (!watchSsh.runRemoteGetOutput(statCmd, out)) {
                        Logger::debug("[Watchdog] Could not read remote GDB log size (SSH failed).");
                        continue;
                    }
                    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
                    size_t size = 0;
                    try {
                        if (!out.empty()) size = static_cast<size_t>(std::stoull(out));
                    } catch (...) {}
                    if (size > 0) everSawProgress = true;
                    if (size != lastSize) {
                        lastSize = size;
                        lastProgressTime = std::chrono::steady_clock::now();
                        Logger::debug("[Watchdog] GDB log size: " + std::to_string(size) + " bytes.");
                    } else if (everSawProgress) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - lastProgressTime).count();
                        if (elapsed >= stallSec) {
                            stallDetected = true;
                            Logger::error("[Watchdog] No forward progress in GDB log for " + std::to_string(stallSec) + "s; assuming hang.");
                            Logger::critical("[Watchdog] Terminating GDB and collecting partial artifacts.");
                            watchSsh.runRemote("pkill -9 -f 'gdb.*plan.gdb' || true");
                            Logger::info("[Watchdog] Sent pkill to GDB on guest; main thread will collect logs and then shut down VM.");
                            break;
                        }
                    }
                }
            });

            gdbThread.join();
            watchdogThread.join();

            if (stallDetected.load()) {
                Logger::error("[RuntimeManager] Debug pass was terminated by watchdog (stall). Partial log and artifacts will be collected.");
            }

            // 3d) Pull back GDB log (to iteration-specific path when provided)
            if (!ssh.copyFrom(remoteGdbLog, localGdbLog))
            {
                Logger::warn("[RuntimeManager] Failed to copy GDB log from guest: " + remoteGdbLog);
            }

            // 3e) Pull back artifacts from /tmp/guardAIn/* (if any) to iteration-specific directory
            // Note: debugDir here is the model-specific directory (debug/openai/ or debug/claude/)
            if (iteration > 0) {
                const std::string remoteArtifactsDir = "/tmp/guardAIn";
                const std::string localArtifactsDir = (fs::path(debugDir) / std::to_string(iteration)).string();
                ensureDirectories(localArtifactsDir);

                // Create a file list on the guest, then copy it and use it to copy artifacts
                const std::string remoteFileList = "/tmp/guardAIn_filelist.txt";
                std::string listCmd = "ls -1 " + remoteArtifactsDir + " > " + remoteFileList + " 2>/dev/null || echo '' > " + remoteFileList;
                ssh.runRemote(listCmd);

                // Copy the file list to local
                const std::string localFileList = (fs::path(localArtifactsDir) / "filelist.txt").string();
                if (ssh.copyFrom(remoteFileList, localFileList)) {
                    // Read the file list
                    std::ifstream fileListStream(localFileList);
                    std::string filename;
                    bool artifactsFound = false;
                    while (std::getline(fileListStream, filename)) {
                        // Remove trailing whitespace
                        filename.erase(filename.find_last_not_of(" \t\r\n") + 1);
                        if (filename.empty()) continue;

                        const std::string remoteArtifact = remoteArtifactsDir + "/" + filename;
                        const std::string localArtifact = (fs::path(localArtifactsDir) / filename).string();

                        if (ssh.copyFrom(remoteArtifact, localArtifact)) {
                            Logger::info("[RuntimeManager] Retrieved artifact: " + localArtifact);
                            artifactsFound = true;
                        } else {
                            Logger::warn("[RuntimeManager] Failed to copy artifact: " + remoteArtifact);
                        }
                    }
                    if (!artifactsFound) {
                        Logger::debug("[RuntimeManager] No artifacts found in " + remoteArtifactsDir);
                    }
                } else {
                    Logger::debug("[RuntimeManager] Could not retrieve artifact list (directory may not exist): " + remoteArtifactsDir);
                }

                // Clean up remote file list
                ssh.runRemote("rm -f " + remoteFileList);
            }

            /* 3f) Pull back memfd dump(if any) - commented out, using artifact capture instead
            const std::string localMemfd = debugDir + "/memfd_dump.bin";
            if (!ssh.copyFrom(remoteMemfd, localMemfd))
            {
                Logger::warn("[RuntimeManager] No memfd dump copied (file may not exist): " + remoteMemfd);
            }
            else
            {
                Logger::info("[RuntimeManager] Retrieved memfd dump: " + localMemfd);
            }*/

            return true;
        });
}

bool RuntimeManager::analyzeSample()
{
	const std::string samplePath = cfg_.sampleDirectory + cfg_.sampleName;
    Logger::info("[RuntimeManager] Starting full analysis for sample: " + samplePath);

    const std::string sampleName = extractSampleName(samplePath);
    const std::string artifactsRoot = buildArtifactsRoot(sampleName);
    
    // Handle clean run vs reuse artifacts
    if (cfg_.cleanRun) {
        // Clean run: remove entire artifacts folder if it exists
        if (fs::exists(artifactsRoot)) {
            Logger::info("[RuntimeManager] Clean run: removing existing artifacts folder: " + artifactsRoot);
            try {
                fs::remove_all(artifactsRoot);
                Logger::info("[RuntimeManager] Removed existing artifacts folder");
            } catch (const std::exception& ex) {
                Logger::error("[RuntimeManager] Failed to remove artifacts folder: " + std::string(ex.what()));
                return false;
            }
        } else {
            Logger::info("[RuntimeManager] Clean run: no existing artifacts folder found (first run)");
        }
        ensureDirectories(artifactsRoot);
    } else {
        // Reuse artifacts: remove only debug artifacts (debug/ and LLM/ directories)
        Logger::info("[RuntimeManager] Reuse artifacts mode: removing only debug artifacts");
        const auto debugDir = buildDebugDir(artifactsRoot);
        const auto llmDir = (fs::path(artifactsRoot) / "LLM").string();
        
        if (fs::exists(debugDir)) {
            try {
                fs::remove_all(debugDir);
                Logger::info("[RuntimeManager] Removed debug directory: " + debugDir);
            } catch (const std::exception& ex) {
                Logger::warn("[RuntimeManager] Failed to remove debug directory: " + std::string(ex.what()));
            }
        }
        
        if (fs::exists(llmDir)) {
            try {
                fs::remove_all(llmDir);
                Logger::info("[RuntimeManager] Removed LLM directory: " + llmDir);
            } catch (const std::exception& ex) {
                Logger::warn("[RuntimeManager] Failed to remove LLM directory: " + std::string(ex.what()));
            }
        }
        
        // Verify static artifacts exist
        const auto staticDir = buildStaticDir(artifactsRoot);
        if (!fs::exists(staticDir)) {
            Logger::error("[RuntimeManager] Reuse artifacts mode requires static artifacts, but static directory not found: " + staticDir);
            Logger::error("[RuntimeManager] Please run with clean run (default) first to generate static artifacts");
            return false;
        }
        Logger::info("[RuntimeManager] Reusing existing static artifacts from: " + staticDir);
    }
    
	//Logger::debug("[RuntimeManager] Key File: " + cfg_.sshKeyPath);   - [!] Bug Squashed, retained for historical reference

    std::string sampleDiskPath;
    if (!prepareSampleImage(sampleName, sampleDiskPath)) {
        Logger::error("[RuntimeManager] Failed to prepare sample image");
        return false;
    }

    // Skip static analysis and dynamic tracing if reusing artifacts
    if (cfg_.cleanRun) {
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
    } else {
        Logger::info("[RuntimeManager] Skipping static analysis and dynamic tracing (reusing artifacts)");
    }

    // 2.5) Generate LLM debug plan from static analysis and run iterative refinement
    const std::string targetPath = "/home/" + cfg_.sshUser + "/" + sampleName;
    const auto debugDir = buildDebugDir(artifactsRoot);
    const auto llmDir = (fs::path(artifactsRoot) / "LLM").string();
    ensureDirectories(debugDir);
    ensureDirectories(llmDir);

    // Collect static artifacts once
    LlmInterface::StaticArtifacts artifacts;
    std::string llmError;
    if (!LlmInterface::collectStaticArtifacts(artifactsRoot, sampleName, artifacts, llmError)) {
        Logger::error("[RuntimeManager] Failed to collect static artifacts: " + llmError);
        Logger::warn("[RuntimeManager] Continuing without LLM-generated plan (debug pass may fail)");
    } else {
        // Build initial prompt (same for both models)
        std::string prompt;
        if (!LlmInterface::buildGdbScriptPrompt(artifacts, targetPath, artifactsRoot, prompt, llmError)) {
            Logger::error("[RuntimeManager] Failed to build LLM prompt: " + llmError);
            Logger::warn("[RuntimeManager] Continuing without LLM-generated plan (debug pass may fail)");
        } else {
            const std::string promptPath = (fs::path(llmDir) / "prompt.txt").string();
            if (!LlmInterface::writePromptToFile(prompt, promptPath, llmError)) {
                Logger::error("[RuntimeManager] Failed to write prompt: " + llmError);
            } else {
                if (cfg_.runBothModels) {
                // Dual-model mode: run both OpenAI and Anthropic
                Logger::info("[RuntimeManager] ===== DUAL-MODEL COMPARISON MODE =====");
                
                // Run OpenAI model
                Logger::info("[RuntimeManager] ===== Starting OpenAI Model Run =====");
                const std::string openaiDebugDir = (fs::path(debugDir) / "openai").string();
                ensureDirectories(openaiDebugDir);
                ModelRunResult openaiResult = runModelIterations(
                    sampleName, sampleDiskPath, artifactsRoot,
                    cfg_.openaiModel, openaiDebugDir, promptPath);
                
                Logger::info("[RuntimeManager] ===== OpenAI Model Complete =====");
                if (openaiResult.success) {
                    Logger::info("[RuntimeManager] OpenAI Result: SUCCESS after " + std::to_string(openaiResult.iterations) + " iterations");
                }
                else {
                    Logger::info("[RuntimeManager] OpenAI Result: FAILED");
				}
                std::this_thread::sleep_for(2s); // Brief pause between models
                
                // Run Anthropic model
                Logger::info("[RuntimeManager] ===== Starting Anthropic Model Run =====");
                const std::string claudeDebugDir = (fs::path(debugDir) / "claude").string();
                ensureDirectories(claudeDebugDir);
                ModelRunResult claudeResult = runModelIterations(
                    sampleName, sampleDiskPath, artifactsRoot,
                    cfg_.claudeModel, claudeDebugDir, promptPath);
                
                Logger::info("[RuntimeManager] ===== Anthropic Model Complete =====");
                if (claudeResult.success) {
                    Logger::info("[RuntimeManager] Anthropic Result: SUCCESS after " + std::to_string(claudeResult.iterations) + " iterations");
				}
                else {
					Logger::info("[RuntimeManager] Anthropic Result: FAILED");
				}
                
                // Summary
                Logger::info("[RuntimeManager] ===== COMPARISON SUMMARY =====");
                if (openaiResult.success) {
                    Logger::info("[RuntimeManager] OpenAI Result: SUCCESS after " + std::to_string(openaiResult.iterations) + " iterations");
                }
                else {
                    Logger::info("[RuntimeManager] OpenAI Result: FAILED");
                }
                if (claudeResult.success) {
                    Logger::info("[RuntimeManager] Anthropic Result: SUCCESS after " + std::to_string(claudeResult.iterations) + " iterations");
                }
                else {
                    Logger::info("[RuntimeManager] Anthropic Result: FAILED");
                }
                } else {
                // Single-model mode (legacy behavior)
                Logger::info("[RuntimeManager] Starting iterative LLM refinement loop (single model)");
                const std::string modelDebugDir = debugDir;  // Use main debug dir for single model
                ModelRunResult result = runModelIterations(
                    sampleName, sampleDiskPath, artifactsRoot,
                    cfg_.llmModel, modelDebugDir, promptPath);
                
                if (!result.success) {
                    Logger::warn("[RuntimeManager] Iterative refinement did not achieve success after " + 
                        std::to_string(result.iterations) + " iterations");
                }
                }
            }
        }
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
