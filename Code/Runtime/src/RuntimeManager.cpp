#include "RuntimeManager.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>

#include "QEMUController.h"
#include "SshHelper.h"

namespace fs = std::filesystem;

RuntimeManager::RuntimeManager(const std::string& qemuExecutablePath,
    const std::string& diskImagePath)
    : m_qemuExecutablePath(qemuExecutablePath),
    m_diskImagePath(diskImagePath)
{
}

void RuntimeManager::runFirstPass(const std::string& samplePath)
{
    std::cout << "[RuntimeManager] Starting first-pass analysis for sample: "
        << samplePath << "\n";

    std::string sampleName = samplePath;
    {
        auto pos = sampleName.find_last_of("\\/");
        if (pos != std::string::npos)
            sampleName = sampleName.substr(pos + 1);
    }

    QEMUController controller(m_qemuExecutablePath, m_diskImagePath);

    if (!controller.startVm("first-pass"))
    {
        std::cerr << "[RuntimeManager] Failed to start VM for first pass.\n";
        return;
    }

    // Use your actual key path here:
    const std::string privateKey =
        "A:\\guardain_ed25519";

    SshHelper ssh("127.0.0.1", "fedora", 10022, privateKey);

    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available in time.\n";
        controller.stopVm();
        return;
    }

    const std::string remoteSamplePath = "/home/fedora/" + sampleName;
    const std::string remoteTracePath = "/tmp/" + sampleName + ".strace";

    if (!ssh.copyToGuest(samplePath, remoteSamplePath))
    {
        std::cerr << "[RuntimeManager] Failed to copy sample into guest.\n";
        controller.stopVm();
        return;
    }

    ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    std::string straceCmd =
        "strace -f -o " + remoteTracePath + " " + remoteSamplePath;

    if (!ssh.runRemoteCommand(straceCmd))
    {
        std::cerr << "[RuntimeManager] strace completed with non-zero exit (likely from target process). "
                  << "Continuing to fetch trace.\n";
        // We'll still try to fetch any trace.
    }

    // Prepare local artifacts directory
    fs::path artifactsRoot = fs::current_path() / "artifacts";
    fs::path sampleDir = artifactsRoot / sampleName / "run1";

    try
    {
        fs::create_directories(sampleDir);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[RuntimeManager] Failed to create artifact directory: "
            << ex.what() << "\n";
    }

    fs::path localTracePath = sampleDir / (sampleName + ".strace");

    // Copy trace back to host
    if (!ssh.copyFromGuest(remoteTracePath, localTracePath.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy trace from guest.\n";
    }
    else
    {
        std::cout << "[RuntimeManager] First-pass trace saved to: "
            << localTracePath.string() << "\n";
    }

    // Optional: connect QMP and query-status just as a sanity check.
    if (controller.connectQmp())
    {
        controller.queryStatus();
    }

    std::cout << "[RuntimeManager] First-pass complete. Stopping VM...\n";
    controller.stopVm();
}
