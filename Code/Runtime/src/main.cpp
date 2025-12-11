#include <iostream>
#include "RuntimeManager.h"

int main()
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
    cfg_.sampleDirectory = "A:\\Samples\\"; // placeholder for now
	cfg_.sampleName = "suspicious"; // placeholder for now
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
