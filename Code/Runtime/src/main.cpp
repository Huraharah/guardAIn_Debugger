#include <iostream>
#include "RuntimeManager.h"

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	Logger::setMinLevel(LogLevel::Debug);
#else
	Logger::setMinLevel(LogLevel::Info);
#endif

	RuntimeConfig cfg_;
    cfg_.qemuBinary = "A:\\QEMU\\qemu-system-x86_64.exe";
    cfg_.baseImagePath = "A:\\VMs\\linux_base.qcow2";   // your Fedora Server qcow2 golden baseline image
	cfg_.vmDirectory = "A:\\VMs\\";
	cfg_.artifactsRoot = "A:\\artifacts\\";
    cfg_.sampleDirectory = "A:\\Samples\\";
	
	// Parse command line arguments
	cfg_.runBothModels = true;  // Default to running both models
	cfg_.sampleName = "suspicious";  // Default sample name
	
	for (int i = 1; i < argc; i++) {
		std::string arg = argv[i];
		
		if (arg == "--openai-model" && i + 1 < argc) {
			cfg_.openaiModel = argv[++i];
			Logger::info("[GuardAInDBG Runtime] OpenAI model: " + cfg_.openaiModel);
		}
		else if (arg == "--claude-model" && i + 1 < argc) {
			cfg_.claudeModel = argv[++i];
			Logger::info("[GuardAInDBG Runtime] Claude model: " + cfg_.claudeModel);
		}
		else if (arg == "--single-model" && i + 1 < argc) {
			cfg_.runBothModels = false;
			cfg_.llmModel = argv[++i];
			Logger::info("[GuardAInDBG Runtime] Single model mode: " + cfg_.llmModel);
		}
		else if (arg == "--openai-only") {
			cfg_.runBothModels = false;
			cfg_.llmModel = cfg_.openaiModel;
			Logger::info("[GuardAInDBG Runtime] OpenAI only mode: " + cfg_.llmModel);
		}
		else if (arg == "--claude-only") {
			cfg_.runBothModels = false;
			cfg_.llmModel = cfg_.claudeModel;
			Logger::info("[GuardAInDBG Runtime] Claude only mode: " + cfg_.llmModel);
		}
		else if (arg == "--reuse-artifacts") {
			cfg_.cleanRun = false;
			Logger::info("[GuardAInDBG Runtime] Reuse artifacts mode: will skip static analysis and reuse existing artifacts");
		}
		else if (arg == "--no-mcp-sidecar") {
			cfg_.mcpSidecarEnabled = false;
			Logger::info("[GuardAInDBG Runtime] MCP/Ghidra static sidecar disabled");
		}
		else if (arg == "--ghidra" && i + 1 < argc) {
			cfg_.ghidraInstallDir = argv[++i];
			Logger::info("[GuardAInDBG Runtime] Ghidra install dir: " + cfg_.ghidraInstallDir);
		}
		else if (arg[0] != '-') {
			// Positional argument: sample name
			cfg_.sampleName = arg;
			Logger::info("[GuardAInDBG Runtime] Using sample from command line: " + cfg_.sampleName);
		}
	}
	
	if (cfg_.runBothModels) {
		Logger::info("[GuardAInDBG Runtime] Dual-model mode: OpenAI=" + cfg_.openaiModel + ", Claude=" + cfg_.claudeModel);
	} else {
		Logger::info("[GuardAInDBG Runtime] Single-model mode: " + cfg_.llmModel);
	}
	cfg_.sshHost = "127.0.0.1";
	cfg_.sshPort = 10022; // your forwarded SSH port for Fedora VM
	cfg_.sshUser = "analyst";
	cfg_.sshKeyPath = "A:\\guardain_ed25519";

	Logger::info("[GuardAInDBG Runtime] Starting");
	Logger::debug("[GuardAInDBG Runtime] Debug information enabled");

    RuntimeManager rm(cfg_);
	rm.analyzeSample();

	/*std::string errorOut;
	if (!GdbScriptBuilder::writeScriptToFile(cfg_.artifactsRoot + "/" + cfg_.sampleName + "/LLM/plan.json",
											 cfg_.artifactsRoot + "/" + cfg_.sampleName + "/debug/plan.gdb",
											 errorOut))
	{
		Logger::error("[GuardAInDBG Runtime] Failed to write GDB script from LLM plan.");
	}
	else
	{
		Logger::info("[GuardAInDBG Runtime] GDB script successfully written from LLM plan.");
	}*/

    Logger::info("[GuardAInDBG Runtime] Done.");
    return 0;
}
