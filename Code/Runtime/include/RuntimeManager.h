#pragma once

#include <string>
#include <functional>
#include <memory>
#include "RuntimeConfig.h"
#include "Logger.h"
#include "QemuController.h"
#include "SshHelper.h"
#include "DebugController.h"
#include "TraceCollector.h"
#include "GdbScriptBuilder.h"
#include "DebugPlan.h"

class RuntimeManager {
public:
    explicit RuntimeManager(RuntimeConfig& cfg);

    // High-level “do everything” call:
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

    bool runStaticToolsPass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot);

    bool runStracePass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        int runIndex = 1);

    bool runLtracePass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot,
        int runIndex = 1);

    bool runDebugPass(const std::string& sampleName,
        const std::string& sampleDiskPath,
        const std::string& artifactsRoot);

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
