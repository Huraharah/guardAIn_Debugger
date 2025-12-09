#pragma once

#include <string>

struct RuntimeConfig {
    std::string qemuBinary;        // e.g. "A:\\QEMU\\qemu-system-x86_64.exe"
    std::string baseImagePath;     // e.g. "A:\\VMs\\linux_base.qcow2"
    std::string vmDirectory;       // e.g. "A:\\VMs"
    std::string artifactsRoot;     // e.g. "A:\\artifacts"

	std::string sampleDirectory; // e.g. "A:\\samples"
	std::string sampleName;      // e.g. "suspicious"

    std::string sshHost = "127.0.0.1";
    int         sshPort = 10022;
    std::string sshUser = "fedora";
    std::string sshKeyPath;        // e.g. "A:\\guardain_ed25519"

    int         sshTimeoutSec = 120;
};
