#include "RuntimeManager.h"

#include <iostream>
#include <filesystem>
#include "QEMUController.h"
#include "SshHelper.h"
#include "DebugController.h"
#include "TraceCollector.h"

namespace fs = std::filesystem;

RuntimeManager::RuntimeManager(const std::string& qemuExecutablePath,
    const std::string& baseDiskImagePath,
    const std::string& privateKeyPath,
    const std::string& artifactsRoot)
    : m_qemuExecutablePath(qemuExecutablePath),
    m_baseDiskImagePath(baseDiskImagePath),
    m_privateKeyPath(privateKeyPath),
    m_artifactsRoot(artifactsRoot)
{
}


std::string RuntimeManager::extractSampleName(const std::string& samplePath) const
{
    std::string name = samplePath;
    auto pos = name.find_last_of("\\/");
    if (pos != std::string::npos)
        name = name.substr(pos + 1);
    return name;
}

std::string RuntimeManager::getSampleDiskImagePath(const std::string& sampleName) const
{
    fs::path baseDisk(m_baseDiskImagePath);
    fs::path parent = baseDisk.parent_path();
    // e.g. linux_suspicious.qcow2
    fs::path sampleDisk = parent / ("linux_" + sampleName + ".qcow2");
    return sampleDisk.string();
}

std::string RuntimeManager::prepareSampleImage(const std::string& samplePath)
{
    const std::string sampleName = extractSampleName(samplePath);
    const std::string sampleDiskPath = getSampleDiskImagePath(sampleName);

    fs::path baseDisk(m_baseDiskImagePath);
    fs::path sampleDisk(sampleDiskPath);

    std::cout << "[RuntimeManager] Preparing sample image for '"
        << sampleName << "'\n";

    // If per-sample image already exists, assume it’s already baked
    // (sample copied in a previous run) and reuse it.
    if (fs::exists(sampleDisk))
    {
        std::cout << "[RuntimeManager] Sample disk already exists: "
            << sampleDiskPath << "\n";
        return sampleDiskPath;
    }

    // Ensure base disk exists
    if (!fs::exists(baseDisk))
    {
        std::cerr << "[RuntimeManager] Base disk image not found: "
            << m_baseDiskImagePath << "\n";
        return {};
    }

    // Copy base → per-sample
    try
    {
        std::cout << "[RuntimeManager] Copying base image to per-sample image...\n";
        fs::copy_file(baseDisk, sampleDisk,
            fs::copy_options::overwrite_existing);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[RuntimeManager] Failed to copy disk image: "
            << ex.what() << "\n";
        return {};
    }

    // Boot per-sample image ONCE without snapshot to bake the sample into it
    QEMUController controller(m_qemuExecutablePath, sampleDiskPath);
    if (!controller.startVm("prep-" + sampleName, /*useSnapshot=*/false))
    {
        std::cerr << "[RuntimeManager] Failed to start VM for sample prep.\n";
        return {};
    }

    SshHelper ssh("127.0.0.1", "fedora", 10022, m_privateKeyPath);
    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available during prep.\n";
        controller.stopVm();
        return {};
    }

    const std::string remoteSamplePath = "/home/fedora/" + sampleName;

    // Copy sample into guest
    if (!ssh.copyToGuest(samplePath, remoteSamplePath))
    {
        std::cerr << "[RuntimeManager] Failed to copy sample into guest during prep.\n";
        controller.stopVm();
        return {};
    }

    // Make it executable
    ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    // For now we just terminate QEMU from the host side; since this is a prep step
    // and we’re not using -snapshot, the file is persisted into the qcow2.
    controller.stopVm();

    std::cout << "[RuntimeManager] Sample image ready: " << sampleDiskPath << "\n";
    return sampleDiskPath;
}

void RuntimeManager::runBaselineDiffPass(const std::string& samplePath)
{
    const std::string sampleName = extractSampleName(samplePath);
    std::cout << "[RuntimeManager] Starting baseline diff pass for sample: "
        << samplePath << "\n";

    // 1) Ensure per-sample disk is prepared
    const std::string sampleDiskPath = prepareSampleImage(samplePath);
    if (sampleDiskPath.empty())
    {
        std::cerr << "[RuntimeManager] Cannot run baseline diff: sample image not prepared.\n";
        return;
    }

    // 2) Start VM with snapshot
    QEMUController controller(m_qemuExecutablePath, sampleDiskPath);
    if (!controller.startVm("baseline-diff"))
    {
        std::cerr << "[RuntimeManager] Failed to start VM for baseline diff.\n";
        return;
    }

    SshHelper ssh("127.0.0.1", "fedora", 10022, m_privateKeyPath);
    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available for baseline diff.\n";
        controller.stopVm();
        return;
    }

    const std::string remoteSamplePath = "/home/fedora/" + sampleName;
    const std::string remotePreManifest = "/tmp/pre_exec.manifest";
    const std::string remotePostManifest = "/tmp/post_exec.manifest";
    const std::string remoteDiffManifest = "/tmp/manifest_diff.txt";
    const std::string remoteFileInfo = "/tmp/" + sampleName + ".fileinfo";
    const std::string remoteSha256 = "/tmp/" + sampleName + ".sha256";
    const std::string remoteStrings = "/tmp/" + sampleName + ".strings";
	const std::string remoteBinwalk = "/tmp/" + sampleName + ".binwalk";
	const std::string remoteReadElf = "/tmp/" + sampleName + ".readelf";
	const std::string remoteObjdump = "/tmp/" + sampleName + ".objdump";

	ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    // 2.5) Static metadata collection (before execution)

    // Basic type info
    ssh.runRemoteCommand("file " + remoteSamplePath + " > " + remoteFileInfo);

    // Cryptographic hash
    ssh.runRemoteCommand("sha256sum " + remoteSamplePath + " > " + remoteSha256);

    // Strings (capped so we don’t blow up disks / UI)
    // -a: scan whole file
    // -t x: show offset in hex
    // head -n 2000: keep it to first ~2000 lines for now
    ssh.runRemoteCommand(
        "strings -a -t x " + remoteSamplePath +
        " | head -n 2000 > " + remoteStrings
    );

    // Binwalk check
    ssh.runRemoteCommand(
        "binwalk " + remoteSamplePath +
        " > " + remoteBinwalk
	);

	// Readelf 
	ssh.runRemoteCommand(
		"readelf -a " + remoteSamplePath +
		" > " + remoteReadElf
	);

	// Objdump
	ssh.runRemoteCommand(
		"objdump -d -M intel " + remoteSamplePath + " | head -n 500"
		" > " + remoteObjdump
	);

    // 3) Generate pre-execution manifest (limited to /home/fedora and /tmp for now)
    std::string findCmd =
        "find /home/fedora /tmp -xdev "
        "-printf '%p|%s|%Y|%T@\\n' > /tmp/pre_exec.manifest 2>/dev/null || true";

    if (!ssh.runRemoteCommand(findCmd))
    {
        std::cerr << "[RuntimeManager] Failed to generate pre-exec manifest.\n";
        controller.stopVm();
        return;
    }

    // 4) Run the sample normally (no strace)
    std::string runCmd = remoteSamplePath;
    if (!ssh.runRemoteCommand(runCmd))
    {
        std::cerr << "[RuntimeManager] Sample exited with non-zero status during baseline diff.\n";
        // Not fatal; we still generate post manifest and diff.
    }

    // 5) Generate post-execution manifest
    std::string findCmdPost =
        "find /home/fedora /tmp -xdev "
        "-printf '%p|%s|%Y|%T@\\n' > /tmp/post_exec.manifest 2>/dev/null || true";

    if (!ssh.runRemoteCommand(findCmdPost))
    {
        std::cerr << "[RuntimeManager] Failed to generate pre-exec manifest.\n";
        controller.stopVm();
        return;
    }


    // 6) Diff them inside guest; diff returns non-zero when files differ, so we
    // add '|| true' so ssh itself returns 0.
    std::string diffCmd =
        "diff -u " + remotePreManifest + " " + remotePostManifest +
        " > " + remoteDiffManifest + " || true";

    ssh.runRemoteCommand(diffCmd); // ignore success/failure; diff file should still exist

    // 7) Copy manifests back to host
	fs::path artifactsRoot(m_artifactsRoot);
    fs::path sampleDir = artifactsRoot / sampleName / "baseline";
	fs::path staticDir = artifactsRoot / sampleName / "static";

    try
    {
        fs::create_directories(sampleDir);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[RuntimeManager] Failed to create baseline artifact directory: "
            << ex.what() << "\n";
    }

    try
    {
        fs::create_directories(staticDir);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[RuntimeManager] Failed to create static artifact directory: "
            << ex.what() << "\n";
    }

    fs::path localPre = sampleDir / "pre_exec.manifest";
    fs::path localPost = sampleDir / "post_exec.manifest";
    fs::path localDiff = sampleDir / "manifest_diff.txt";
	fs::path localFileInfo = staticDir / (sampleName + ".fileinfo");
	fs::path localSha256 = staticDir / (sampleName + ".sha256");
	fs::path localStrings = staticDir / (sampleName + ".strings");
	fs::path localBinwalk = staticDir / (sampleName + ".binwalk");
	fs::path localReadElf = staticDir / (sampleName + ".readelf");
	fs::path localObjdump = staticDir / (sampleName + ".objdump");

    if (!ssh.copyFromGuest(remotePreManifest, localPre.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy pre_exec.manifest from guest.\n";
    }

    if (!ssh.copyFromGuest(remotePostManifest, localPost.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy post_exec.manifest from guest.\n";
    }

    if (!ssh.copyFromGuest(remoteDiffManifest, localDiff.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy manifest_diff.txt from guest.\n";
    }

    std::cout << "[RuntimeManager] Baseline diff artifacts saved under: "
        << sampleDir.string() << "\n";

    if (!ssh.copyFromGuest(remoteFileInfo, localFileInfo.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy sample file info from guest.\n";
    }

    if (!ssh.copyFromGuest(remoteSha256, localSha256.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy sample SHA-256 hash from guest.\n";
    }

    if (!ssh.copyFromGuest(remoteStrings, localStrings.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy sample strings from guest.\n";
	}

	if (!ssh.copyFromGuest(remoteBinwalk, localBinwalk.string()))
	{
		std::cerr << "[RuntimeManager] Failed to copy sample binwalk output from guest.\n";
	}

	if (!ssh.copyFromGuest(remoteReadElf, localReadElf.string()))
	{
		std::cerr << "[RuntimeManager] Failed to copy sample readelf output from guest.\n";
	}

	if (!ssh.copyFromGuest(remoteObjdump, localObjdump.string()))
	{
		std::cerr << "[RuntimeManager] Failed to copy sample objdump output from guest.\n";
	}

    std::cout << "[RuntimeManager] Static tool analysis artifacts saved under: "
        << staticDir.string() << "\n";

    controller.stopVm();
}

void RuntimeManager::runFirstPass(const std::string& samplePath)
{
    const std::string sampleName = extractSampleName(samplePath);
    std::cout << "[RuntimeManager] Starting strace pass for sample: "
        << samplePath << "\n";

    // 1) Ensure per-sample disk is prepared
    const std::string sampleDiskPath = prepareSampleImage(samplePath);
    if (sampleDiskPath.empty())
    {
        std::cerr << "[RuntimeManager] Cannot run first pass: sample image not prepared.\n";
        return;
    }

	// 2) Start VM with snapshot
    QEMUController scontroller(m_qemuExecutablePath, sampleDiskPath);

    if (!scontroller.startVm("strace-pass")) // uses snapshot by default
    {
        std::cerr << "[RuntimeManager] Failed to start VM for strace pass.\n";
        return;
    }

    SshHelper ssh("127.0.0.1", "fedora", 10022, m_privateKeyPath);
    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available in time.\n";
        scontroller.stopVm();
        return;
    }

	// Paths inside guest
    const std::string remoteSamplePath = "/home/fedora/" + sampleName;
    const std::string remoteTracePath = "/tmp/" + sampleName + ".strace";

    // No need to copy the sample here; it's already baked into the image.
    // Just ensure it’s executable (harmless if already so).
    ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    // Run under strace
    std::string straceCmd =
        "strace -f -o " + remoteTracePath + " " + remoteSamplePath;

    if (!ssh.runRemoteCommand(straceCmd))
    {
        std::cerr << "[RuntimeManager] strace completed with non-zero exit "
            "(likely from target process). Continuing to fetch trace.\n";
    }

    // Prepare local artifact path: artifacts/<sample>/run1/<sample>.strace
	fs::path artifactsRoot(m_artifactsRoot);
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

    if (!ssh.copyFromGuest(remoteTracePath, localTracePath.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy trace from guest.\n";
    }
    else
    {
        std::cout << "[RuntimeManager] strace pass saved to: "
            << localTracePath.string() << "\n";
    }

    // Optional: QMP sanity check
    // if (controller.connectQmp())
    // {
    //    controller.queryStatus();
    // }

    std::cout << "[RuntimeManager] strace pass complete. Stopping VM...\n";
    scontroller.stopVm();

	// Now run ltrace in a similar fashion
    QEMUController lcontroller(m_qemuExecutablePath, sampleDiskPath);

    if (!lcontroller.startVm("ltrace-pass"))
    {
        std::cerr << "[RuntimeManager] Failed to start VM for ltrace pass.\n";
        return;
    }

    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available for ltrace pass.\n";
        lcontroller.stopVm();
        return;
    }

    const std::string remoteLtracePath = "/tmp/" + sampleName + ".ltrace";

    // Make sure sample is executable
    ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    // Clean any stale ltrace file
    ssh.runRemoteCommand("rm -f " + remoteLtracePath);

    // Run under ltrace
    std::string ltraceCmd =
        "ltrace -f -o " + remoteLtracePath + " " + remoteSamplePath;

    if (!ssh.runRemoteCommand(ltraceCmd))
    {
        std::cerr << "[RuntimeManager] ltrace completed with non-zero exit.\n";
    }

    std::filesystem::path localLtracePath = sampleDir / (sampleName + ".ltrace");

    if (!ssh.copyFromGuest(remoteLtracePath, localLtracePath.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy ltrace log from guest.\n";
    }
    else
    {
        std::cout << "[RuntimeManager] ltrace log saved to: "
            << localLtracePath.string() << "\n";
    }

    lcontroller.stopVm();
}

void RuntimeManager::runDebugPass(const std::string& samplePath)
{
    namespace fs = std::filesystem;

    const std::string sampleName = extractSampleName(samplePath);
    std::cout << "[RuntimeManager] Starting debug pass for sample: "
        << samplePath << "\n";

    // 1) Prepare per-sample image (clone of linux_base)
    const std::string sampleDiskPath = prepareSampleImage(samplePath);
    if (sampleDiskPath.empty())
    {
        std::cerr << "[RuntimeManager] Cannot run debug pass: "
            "sample image not prepared.\n";
        return;
    }

    // 2) Start VM in snapshot mode using the per-sample image
    QEMUController controller(m_qemuExecutablePath, sampleDiskPath);
    if (!controller.startVm("debug-pass"))  // your startVm already adds -snapshot
    {
        std::cerr << "[RuntimeManager] Failed to start VM for debug pass.\n";
        return;
    }

    // 3) Wait for SSH
    SshHelper ssh("127.0.0.1", "fedora", 10022, m_privateKeyPath);
    if (!ssh.waitForSsh(120))
    {
        std::cerr << "[RuntimeManager] SSH did not become available for debug pass.\n";
        controller.stopVm();
        return;
    }

    const std::string remoteSamplePath = "/home/fedora/" + sampleName;
    const std::string remoteGdbLogPath = "/tmp/" + sampleName + ".gdb.txt";

    // 4) Ensure the sample is executable
    ssh.runRemoteCommand("chmod +x " + remoteSamplePath);

    // 5) Remove any prior debug log in /tmp
    ssh.runRemoteCommand("rm -f " + remoteGdbLogPath);

    // 6) Build gdb batch command
    //
    // NOTE: Only single quotes inside the remote command string so that
    // the outer ssh command can safely wrap it in double quotes.
    std::string gdbCmd =
        "gdb -q -batch "
        "-ex 'set pagination off' "
        "-ex 'set confirm off' "
        "-ex 'break main' "
        "-ex 'run' "
        "-ex 'info registers' "
        "-ex 'bt' "
        "-- " + remoteSamplePath +
        " > " + remoteGdbLogPath + " 2>&1";

    std::cout << "[RuntimeManager] Running remote gdb:\n    " << gdbCmd << "\n";

    if (!ssh.runRemoteCommand(gdbCmd))
    {
        // gdb frequently exits non-zero if the program crashes or hits signals
        // That's actually *useful* info, so we log a warning but continue.
        std::cerr << "[RuntimeManager] gdb completed with non-zero exit "
            "(likely from the target process). Continuing to fetch log.\n";
    }

    // 7) Copy the gdb log back to artifacts
    fs::path artifactsRoot(m_artifactsRoot);
    fs::path debugDir = artifactsRoot / sampleName / "debug";
    fs::create_directories(debugDir);

    fs::path localGdbLogPath = debugDir / "debug.log";
    if (!ssh.copyFromGuest(remoteGdbLogPath, localGdbLogPath.string()))
    {
        std::cerr << "[RuntimeManager] Failed to copy gdb log from guest.\n";
    }
    else
    {
        std::cout << "[RuntimeManager] Debug log saved to: "
            << localGdbLogPath.string() << "\n";
    }

    // 8) Shut down the VM
    std::cout << "[RuntimeManager] Debug pass complete. Stopping VM...\n";
    controller.stopVm();
}

bool RuntimeManager::runTraceCollectorForSample(const std::string& samplePath)
{
    namespace fs = std::filesystem;

    std::string sampleName = extractSampleName(samplePath);
    const std::string artifactsRoot = "A:\\artifacts";

    TraceCollector collector;
    TraceSummary summary;

    if (!collector.buildSummary(artifactsRoot, sampleName, summary))
    {
        std::cerr << "[RuntimeManager] TraceCollector buildSummary failed for sample: "
            << samplePath << "\n";
        return false;
    }

    fs::path summaryPath =
        fs::path(artifactsRoot) / sampleName / "summary" / "summary.json";

    if (!collector.writeSummaryJson(summary, summaryPath.string()))
    {
        std::cerr << "[RuntimeManager] Failed to write summary JSON for sample: "
            << samplePath << "\n";
        return false;
    }

    std::cout << "[RuntimeManager] Summary JSON written to: "
        << summaryPath.string() << "\n";
    return true;
}
