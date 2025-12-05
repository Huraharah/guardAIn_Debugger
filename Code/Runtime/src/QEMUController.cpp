#include "QEMUController.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

QEMUController::QEMUController(const std::string& qemuExecutable,
                               const std::string& diskImagePath)
    : m_qemuExecutable(qemuExecutable),
    m_diskImagePath(diskImagePath),
    m_isRunning(false),
    m_processHandle(nullptr),
    m_processId(0),
    m_qmpClient(nullptr),
    m_qmpHost("127.0.0.1"),
    m_qmpPort(4444)
{
}


QEMUController::~QEMUController()
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

bool QEMUController::startVm(const std::string& vmName)
{
    if (m_isRunning)
    {
        std::cerr << "[QEMUController] VM already running: " << m_lastVmName << "\n";
        return false;
    }

    m_lastVmName = vmName;

    // NOTE: m_diskImagePath should be something like "A:\\VMs\\linux_base.qcow2"
    // For now we assume no spaces in path; if there are, we’ll need to quote it.
	// TODO: Move these args to parameters and/or config later.
    std::string args =
        "-m 2048 "
        "-M q35 "
        "-accel tcg "
        "-drive file=" + m_diskImagePath + ",if=virtio,format=qcow2 "
        "-nic user,hostfwd=tcp::10022-:22 "
        "-qmp tcp:localhost:4444,server,nowait "
        "-nographic ";

    std::cout << "[QEMUController] Starting VM '" << vmName << "' using:\n"
        << "    " << m_qemuExecutable << " " << args << "\n";

    bool ok = launchProcess(args);
    if (ok)
    {
        m_isRunning = true;
    }
    return ok;
}


bool QEMUController::stopVm()
{
    if (!m_isRunning)
    {
        std::cerr << "[QEMUController] stopVm() called but no VM is marked as running.\n";
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
                std::cout << "[QEMUController] QEMU process (pid=" << m_processId
                    << ") has already exited with code " << exitCode << ".\n";
                m_isRunning = false;
                cleanupProcessHandle();
                return true;
            }
        }
        else
        {
            std::cerr << "[QEMUController] GetExitCodeProcess failed. Error="
                << GetLastError() << "\n";
        }

        std::cout << "[QEMUController] Terminating QEMU process (pid=" << m_processId << ")...\n";

        if (!TerminateProcess(static_cast<HANDLE>(m_processHandle), 0))
        {
            std::cerr << "[QEMUController] TerminateProcess failed. Error="
                << GetLastError() << "\n";
            // We still fall through and cleanup; the process might be gone anyway.
        }
        else
        {
            // Wait up to 5 seconds for it to die.
            WaitForSingleObject(m_processHandle, 5000);
        }
    }
#endif

    m_isRunning = false;
    cleanupProcessHandle();
    return true;
}

bool QEMUController::isRunning() const
{
    return m_isRunning;
}

bool QEMUController::launchProcess(const std::string& arguments)
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
        std::cerr << "[QEMUController] Failed to launch process. Error=" << GetLastError() << "\n";
        return false;
    }

    m_processHandle = pi.hProcess;
    m_processId = pi.dwProcessId;

    // We don't need the thread handle.
    CloseHandle(pi.hThread);

    std::cout << "[QEMUController] QEMU started with pid=" << m_processId << "\n";
    return true;
#else
    // TODO: Implement for non-Windows platforms later.
    std::cerr << "[QEMUController] launchProcess not implemented for this platform.\n";
    return false;
#endif
}

void QEMUController::cleanupProcessHandle()
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
bool QEMUController::connectQmp(const std::string& host, int port)
{
    if (!m_isRunning)
    {
        std::cerr << "[QEMUController] Cannot connect QMP: VM is not running.\n";
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
        std::cerr << "[QEMUController] Failed to connect QMP client.\n";
        delete m_qmpClient;
        m_qmpClient = nullptr;
        return false;
    }

    if (!m_qmpClient->negotiateCapabilities())
    {
        std::cerr << "[QEMUController] QMP capabilities negotiation failed.\n";
        return false;
    }

    return true;
}

bool QEMUController::createSnapshot(const std::string& name)
{
    if (!m_qmpClient)
    {
        std::cerr << "[QEMUController] createSnapshot called but QMP is not connected.\n";
        return false;
    }
    return m_qmpClient->saveSnapshot(name);
}

bool QEMUController::loadSnapshot(const std::string& name)
{
    if (!m_qmpClient)
    {
        std::cerr << "[QEMUController] loadSnapshot called but QMP is not connected.\n";
        return false;
    }
    return m_qmpClient->loadSnapshot(name);
}

bool QEMUController::queryStatus()
{
    if (!m_qmpClient)
    {
        std::cerr << "[QEMUController] queryStatus called but QMP is not connected.\n";
        return false;
    }
    return m_qmpClient->queryStatus();
}