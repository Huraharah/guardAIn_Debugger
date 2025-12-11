#pragma once

#include <string>
#include "QmpClient.h"
#include "Logger.h"
#include "RuntimeConfig.h"

class QemuController
{
public:
    // Construct from shared runtime configuration
    explicit QemuController(const RuntimeConfig& cfg);

    ~QemuController();

    // For now, vmName is just for logging.
    bool startVm(const std::string& vmName, bool useSnapshot = true);

    // Attempt to terminate the running QEMU process.
    bool stopVm();

    // True if we think the process is still alive.
    bool isRunning() const;

    // QMP-related helpers
    bool connectQmp(const std::string& host = "127.0.0.1", int port = 4444);
    bool createSnapshot(const std::string& name);
    bool loadSnapshot(const std::string& name);
    bool queryStatus(); // wrapper around QMP query-status

	// Util Getters/Setters
	void setDiskImagePath(const std::string& path);
	void setIsRunning(bool isRunning);
    unsigned long getProcessID();
    HANDLE getProcessHandle();

    // Request guest shutdown via QMP (platform-specific, not always implemented)
    bool requestGuestShutdownQmp();
	bool waitForExitWithTimeoutMs(int timeoutMs);

private:
    RuntimeConfig cfg_;

    std::string m_qemuExecutable;
    std::string m_lastVmName;
    std::string m_baseImagePath;
	std::string m_vmDirectory;
	std::string m_sampleDirectory;
	std::string m_sampleName;
	std::string m_diskImagePath;
    bool        m_isRunning = false;

    // QMP client
    QmpClient* m_qmpClient = nullptr;
    std::string m_qmpHost;
    int         m_qmpPort = 0;

#ifdef _WIN32
    void* m_processHandle = nullptr;     // actually a HANDLE
    unsigned long m_processId = 0;           // actually a DWORD
#endif

    bool launchProcess(const std::string& arguments);
    void cleanupProcessHandle();
};