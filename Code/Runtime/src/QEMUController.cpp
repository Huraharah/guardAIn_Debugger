#include "QemuController.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// Constructor definition must be qualified with the class name
QemuController::QemuController(const RuntimeConfig& cfg)
    : cfg_(cfg),
    m_qemuExecutable(cfg.qemuBinary),         // Which QEMU binary to use - full path e.g. "A:\QEMU\qemu-system-x86_64.exe"
    m_baseImagePath(cfg.baseImagePath),       // Full base image path e.g. "A:\VMs\linux_base.qcow2"
    m_vmDirectory(cfg.vmDirectory),           // Where to store per-sample VM images e.g. "A:\VMs"
	m_sampleDirectory(cfg.sampleDirectory),   // Where samples are stored on host e.g. "A:\samples"
	m_sampleName(cfg.sampleName),             // Current sample name e.g. "suspicious"
	m_diskImagePath(""),                      // To be set per-VM instance
    m_isRunning(false),
    m_qmpClient(nullptr),
    m_qmpHost("127.0.0.1"),
    m_qmpPort(4444)
#ifdef _WIN32
    , m_processHandle(nullptr)
    , m_processId(0)
#endif
{
}

QemuController::~QemuController()
{
    if (m_isRunning)
    {
        stopVm();
    }
    cleanupProcessHandle();

    if (m_qmpClient)
    {
        delete m_qmpClient;
        m_qmpClient = nullptr;
    }
}

void QemuController::setDiskImagePath(const std::string& path)
{
    m_diskImagePath = path;
	Logger::debug("[QEMUController] Disk image path set to : " + m_diskImagePath);
}
void QemuController::setIsRunning(bool isRunning)
{
    m_isRunning = isRunning;
	Logger::debug(std::string("[QEMUController] isRunning set to : ") + (isRunning ? "true" : "false"));
}

unsigned long QemuController::getProcessID()
{
	return m_processId;
}

HANDLE QemuController::getProcessHandle()
{
    return static_cast<HANDLE>(m_processHandle);
}

bool QemuController::startVm(const std::string& vmName, bool useSnapshot)
{
    if (m_isRunning)
    {
        Logger::error("[QEMUController] VM already running: " + m_lastVmName);
        return false;
    }

    m_lastVmName = vmName;

    // NOTE: m_diskImagePath should be something like "A:\\VMs\\linux_suspicious.qcow2"
    // For now we assume no spaces in path; if there are, we’ll need to quote it.
    std::string args;
    args += "-m 2048 ";
    args += "-M q35 ";
    args += "-accel tcg ";
    if (useSnapshot)
    {
        args += "-snapshot ";
    }
    args += "-drive file=" + m_diskImagePath + ",if=virtio,format=qcow2 ";
    args += "-nic user,hostfwd=tcp:127.0.0.1:10022-:22 ";
    args += "-monitor tcp:127.0.0.1:10023,server,nowait ";

    Logger::info(std::string("[QEMUController] Starting VM '") + vmName + std::string("' using:"));
    Logger::info(std::string("    ") + m_qemuExecutable + " " + args);

    bool ok = launchProcess(args);
    if (ok)
    {
        m_isRunning = true;
    }
    return ok;
}

bool QemuController::stopVm()
{
    if (!m_isRunning)
    {
        Logger::error("[QEMUController] stopVm() called but no VM is marked as running.");
        return false;
    }

#ifdef _WIN32
    if (m_processHandle != nullptr)
    {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(m_processHandle), &exitCode))
        {
            if (exitCode != STILL_ACTIVE)
            {
                // Process already exited on its own – this is fine.
                Logger::warn(std::string("[QEMUController] QEMU process (pid=") + std::to_string(m_processId) + ") has already exited with code " + std::to_string(exitCode));
                m_isRunning = false;
                cleanupProcessHandle();
                return true;
            }
        }
        else
        {
            Logger::error("[QEMUController] GetExitCodeProcess failed. Error=" + GetLastError());
        }

        Logger::info(std::string("[QEMUController] Terminating QEMU process (pid=") + std::to_string(m_processId) + ")...");

        if (!TerminateProcess(static_cast<HANDLE>(m_processHandle), 0))
        {
            Logger::error("[QEMUController] TerminateProcess failed. Error=" + GetLastError());
            // We still fall through and cleanup; the process might be gone anyway.
        }
        else
        {
            // Wait up to 5 seconds for it to die.
            WaitForSingleObject(m_processHandle, 5000);
			m_isRunning = false;
        }
    }
#endif

    m_isRunning = false;
    cleanupProcessHandle();
    return true;
}

bool QemuController::isRunning() const
{
    return m_isRunning;
}

bool QemuController::launchProcess(const std::string& arguments)
{
#ifdef _WIN32
    // Build command line: "C:\path\qemu-system-x86_64.exe" <args>
    std::string cmdLine = "\"" + m_qemuExecutable + "\" " + arguments;

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    // Create in a new console so you can see QEMU's output/window for now.
    BOOL success = CreateProcessA(
        nullptr,                      // lpApplicationName
        cmdLine.data(),               // lpCommandLine (modifiable buffer)
        nullptr,                      // lpProcessAttributes
        nullptr,                      // lpThreadAttributes
        FALSE,                        // bInheritHandles
        CREATE_NEW_CONSOLE,           // dwCreationFlags
        nullptr,                      // lpEnvironment
        nullptr,                      // lpCurrentDirectory
        &si,                          // lpStartupInfo
        &pi                           // lpProcessInformation
    );

    if (!success)
    {
        Logger::error("[QEMUController] Failed to launch process. Error=" + GetLastError());
        return false;
    }

    m_processHandle = pi.hProcess;
    m_processId = pi.dwProcessId;

    // We don't need the thread handle.
    CloseHandle(pi.hThread);

    Logger::info("[QEMUController] QEMU started with pid=" + std::to_string(m_processId));
    return true;
#else
    // TODO: Implement for non-Windows platforms later.
    Logger::error("[QEMUController] launchProcess not implemented for this platform.);
    return false;
#endif
}

void QemuController::cleanupProcessHandle()
{
#ifdef _WIN32
    if (m_processHandle != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(m_processHandle));
        m_processHandle = nullptr;
        m_processId = 0;
    }
#endif
}

// QMP-related helpers
bool QemuController::connectQmp(const std::string& host, int port)
{
    if (!m_isRunning)
    {
        Logger::error("[QEMUController] Cannot connect QMP: VM is not running.");
        return false;
    }

    // Clean up any existing client
    if (m_qmpClient)
    {
        delete m_qmpClient;
        m_qmpClient = nullptr;
    }

    m_qmpHost = host;
    m_qmpPort = port;

    m_qmpClient = new QmpClient(m_qmpHost, m_qmpPort);
    if (!m_qmpClient->connectToServer())
    {
        Logger::error("[QEMUController] Failed to connect QMP client.");
        delete m_qmpClient;
        m_qmpClient = nullptr;
        return false;
    }

    if (!m_qmpClient->negotiateCapabilities())
    {
        Logger::error("[QEMUController] QMP capabilities negotiation failed.");
        return false;
    }

    return true;
}

bool QemuController::createSnapshot(const std::string& name)
{
    if (!m_qmpClient)
    {
        Logger::error("[QEMUController] createSnapshot called but QMP is not connected.");
        return false;
    }
    return m_qmpClient->saveSnapshot(name);
}

bool QemuController::loadSnapshot(const std::string& name)
{
    if (!m_qmpClient)
    {
        Logger::error("[QEMUController] loadSnapshot called but QMP is not connected.");
        return false;
    }
    return m_qmpClient->loadSnapshot(name);
}

bool QemuController::queryStatus()
{
    if (!m_qmpClient)
    {
        Logger::error("[QEMUController] queryStatus called but QMP is not connected.");
        return false;
    }
    return m_qmpClient->queryStatus();
}
