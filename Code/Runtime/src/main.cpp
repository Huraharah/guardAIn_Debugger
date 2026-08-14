#include <iostream>
#include <fstream>

#include "RuntimeManager.h"
#include "../../external/nlohmann/json.hpp"

using json = nlohmann::json;

bool loadRuntimeConfig(const std::string& path, RuntimeConfig& cfg)
{
    std::ifstream file(path);

    if (!file.is_open()) {
        Logger::error("[GuardAInDBG Runtime] Unable to open config file: " + path);
        return false;
    }

    try {
        json j;
        file >> j;

        cfg.qemuBinary    = j.at("qemuBinary").get<std::string>();
        cfg.baseImagePath = j.at("baseImagePath").get<std::string>();
        cfg.vmDirectory   = j.at("vmDirectory").get<std::string>();
        cfg.sshKeyPath    = j.at("sshKeyPath").get<std::string>();

		// Print config confirmations
		Logger::debug("[GuardAInDBG Runtime] QEMU: " + cfg_.qemuBinary);
		Logger::debug("[GuardAInDBG Runtime] VM image: " + cfg_.baseImagePath);
		Logger::debug("[GuardAInDBG Runtime] VM directory: " + cfg_.vmDirectory);
		Logger::debug("[GuardAInDBG Runtime] SSH key: " + cfg_.sshKeyPath);
    }
    catch (const json::exception& e) {
        Logger::error(
            std::string("[GuardAInDBG Runtime] Invalid config file: ") + e.what()
        );
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
#ifdef _DEBUG
	Logger::setMinLevel(LogLevel::Debug);
#else
	Logger::setMinLevel(LogLevel::Info);
#endif

	RuntimeConfig cfg_;

	// Portable defaults
	cfg_.artifactsRoot = ".\\artifacts\\";
    cfg_.sampleDirectory = ".\\Samples\\";
	cfg_.sshHost = "127.0.0.1";
	cfg_.sshPort = 10022;
	cfg_.sshUser = "analyst";

	// Machine-Specific configuration
    if (!loadRuntimeConfig("config.json", cfg_)) {
        Logger::error(
            "[GuardAInDBG Runtime] Failed to load runtime configuration."
        );
        return 1;
    }
	
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

	Logger::info("[GuardAInDBG Runtime] Starting");
	Logger::debug("[GuardAInDBG Runtime] Debug information enabled");

    RuntimeManager rm(cfg_);
	rm.analyzeSample();

    Logger::info("[GuardAInDBG Runtime] Done.");
    return 0;
}
