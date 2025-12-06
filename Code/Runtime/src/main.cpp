#include <iostream>
#include "RuntimeManager.h"

int main()
{
    const std::string qemuPath = "A:\\QEMU\\qemu-system-x86_64.exe";
    const std::string diskImage = "A:\\VMs\\linux_base.qcow2";   // your Fedora Server qcow2 golden baseline image
    const std::string sample = "A:\\Samples\\suspicious"; // placeholder for now
	const std::string privateKey = "A:\\guardain_ed25519"; // your SSH private key for Fedora VM
	const std::string artifactRoot = "A:\\artifacts";

    RuntimeManager rm(qemuPath, diskImage, privateKey,artifactRoot);
	rm.prepareSampleImage(sample);
	rm.runBaselineDiffPass(sample);
    rm.runFirstPass(sample);
    rm.runDebugPass(sample);
	rm.runTraceCollectorForSample(sample);

    std::cout << "[main] Done.\n";
    return 0;
}
