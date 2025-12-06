#pragma once

#include <string>

class RuntimeManager
{
public:
    RuntimeManager(const std::string& qemuExecutablePath,
        const std::string& baseDiskImagePath,
        const std::string& privateKeyPath,
        const std::string& artifactRoot);

    // One-time prep per sample: copy base image and bake sample into it.
    // Returns full path to per-sample disk image, or empty string on error.
    std::string prepareSampleImage(const std::string& samplePath);

    // Baseline diff pass: no strace, just pre/post manifests + diff.
    void runBaselineDiffPass(const std::string& samplePath);

    // Your existing first-pass trace (we’ll tweak to use per-sample image):
    void runFirstPass(const std::string& samplePath);

	// Debug pass: run under gdbserver and collect debug artifacts.
	void runDebugPass(const std::string& samplePath);

    bool runTraceCollectorForSample(const std::string& samplePath);

private:
    std::string m_qemuExecutablePath;
    std::string m_baseDiskImagePath;   // linux_base.qcow2
    std::string m_privateKeyPath;      // e.g. A:\guardain_ed25519
	std::string m_artifactsRoot;      // e.g. A:\Artifacts

    // Helper: extract "suspicious" from "A:\Samples\suspicious"
    std::string extractSampleName(const std::string& samplePath) const;

    // Helper: compute per-sample disk image path
    std::string getSampleDiskImagePath(const std::string& sampleName) const;
};
