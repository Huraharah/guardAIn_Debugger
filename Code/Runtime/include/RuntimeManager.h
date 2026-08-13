#pragma once

#include <string>
#include <functional>
#include <memory>
#include "RuntimeConfig.h"
#include "Logger.h"
#include "QemuController.h"
#include "SshHelper.h"
#include "TraceCollector.h"
#include "LlmInterface.h"
#include "QemuMonitorClient.h"

class RuntimeManager {
public:
    explicit RuntimeManager(RuntimeConfig& cfg);

    // High-level �do everything� call:
    bool analyzeSample();

private:
    RuntimeConfig cfg_;
	QemuController qemu_;
	SshHelper ssh_;
	TraceCollector trace_;

    // Core pipeline helpers
    bool prepareSampleImage(const std::string& sampleName,
        std::string& outSampleDiskPath);

    bool runBaselineDiffPass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot);

    /*
    bool runStaticToolsPass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot);
        */

    bool runStracePass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        int runIndex = 1);

    /*
    bool runLtracePass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        int runIndex = 1);
        */

    bool runDebugPass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        const std::string& localGdbLogPath = "",
        int iteration = 0);
    
    // Internal helper that accepts debug directory directly (for model-specific paths)
    bool runDebugPassWithDir(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& debugDir,
        const std::string& localGdbLogPath,
        int iteration);

    // Helper to check GDB log for success/failure
    enum class DebugResult {
        Success,      // Found success message (e.g., "You achieved level 1!")
        Failure,      // Found failure message (e.g., "You are not leet enough" or usage message)
        Error,        // Program crashed or hit signal
        Unknown       // Could not determine result
    };
    static DebugResult parseGdbLogResult(const std::string& gdbLogPath);

    // Run iterative LLM refinement for a single model
    struct ModelRunResult {
        bool success = false;
        int iterations = 0;
        std::string finalFlag;  // If flag was extracted
    };
    ModelRunResult runModelIterations(
        const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        const std::string& modelName,
        const std::string& modelDebugDir,  // e.g., "debug/openai" or "debug/claude"
        const std::string& promptPath);

    // Generic snapshot-VM runner
    bool runInSnapshotVm(const std::string& vmName,
        const std::string& sampleDiskPath,
        const std::function<bool(SshHelper&)>& work);

    // Path helpers
    std::string extractSampleName(const std::string& samplePath) const;
    std::string buildArtifactsRoot(const std::string& sampleName) const;
    std::string buildBaselineDir(const std::string& artifactsRoot) const;
    std::string buildStaticDir(const std::string& artifactsRoot) const;
    std::string buildRunDir(const std::string& artifactsRoot, int runIndex) const;
    std::string buildDebugDir(const std::string& artifactsRoot) const;
    void        ensureDirectories(const std::string& path) const;
};
