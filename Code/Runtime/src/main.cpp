#include <iostream>
#include "RuntimeManager.h"

int main()
{
    const std::string qemuPath = "A:\\QEMU\\qemu-system-x86_64.exe";
    const std::string diskImage = "A:\\VMs\\linux_base.qcow2";   // your Fedora Server qcow2
    const std::string sample = "A:\\Samples\\suspicious"; // placeholder for now

    RuntimeManager rm(qemuPath, diskImage);
    rm.runFirstPass(sample);

    std::cout << "[main] Done.\n";
    return 0;
}
