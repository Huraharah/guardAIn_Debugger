#pragma once

#include <string>

struct RuntimeConfig {
    std::string qemuBinary;        // e.g. "A:\\QEMU\\qemu-system-x86_64.exe"
    std::string baseImagePath;     // e.g. "A:\\VMs\\linux_base.qcow2"
    std::string vmDirectory;       // e.g. "A:\\VMs"
    std::string artifactsRoot;     // e.g. "A:\\artifacts"

	std::string sampleDirectory; // e.g. "A:\\samples"
	std::string sampleName;      // e.g. "suspicious"
	
	// LLM model configuration
	bool runBothModels = true;  // If true, run both OpenAI and Anthropic models
	std::string openaiModel = "gpt-5.2";  // OpenAI model to use
	std::string claudeModel = "claude-sonnet-4-5-20250929";  // Anthropic model to use
	
	// Legacy: single model mode (if runBothModels is false)
	std::string llmModel = "gpt-5.2";  // LLM model to use (for single-model mode)
	
	// Clean run configuration
	bool cleanRun = true;  // If true, remove all artifacts and run full pipeline. If false, reuse static artifacts and only remove debug artifacts.

    // Host-side Ghidra / MCP static sidecar (runs in parallel with VM static+dynamic passes until LLM prompt build)
    bool mcpSidecarEnabled = true;
    std::string ghidraInstallDir;  // Empty: mcp_static_sidecar.py uses GHIDRA_INSTALL_DIR

    std::string sshHost = "127.0.0.1";
    int         sshPort = 10022;
    std::string sshUser = "analyst";
    std::string sshKeyPath;        // e.g. "A:\\guardain_ed25519"

    int         sshTimeoutSec = 120;

    // Watchdog: forward-progress check during debug pass (GDB in QEMU)
    int         debugStallTimeoutSec = 30;  // No progress in GDB log for this long = stall
    int         debugStallPollIntervalSec = 5;  // How often to check GDB log size
    std::string qemuMonitorHost = "127.0.0.1";
    int         qemuMonitorPort = 10023;   // Must match -monitor tcp:127.0.0.1:10023 in QEMU args
};
