#pragma once
#include <string>
#include "QmpClient.h"

class QEMUController
{
public:
    // qemuExecutable: full path to qemu-system-* binary
    explicit QEMUController(const std::string& qemuExecutable,
                            const std::string& diskImagePath);
    ~QEMUController();

    // For now, vmName is just for logging.
    bool startVm(const std::string& vmName);

    // Attempt to terminate the running QEMU process.
    bool stopVm();

    // True if we think the process is still alive.
    bool isRunning() const;

    // QMP-related helpers
    bool connectQmp(const std::string& host = "127.0.0.1", int port = 4444);
    bool createSnapshot(const std::string& name);
    bool loadSnapshot(const std::string& name);
    bool queryStatus(); // wrapper around QMP query-status

private:
    std::string m_qemuExecutable;
    std::string m_lastVmName;
	std::string m_diskImagePath;
    bool m_isRunning;

	// QMP client
	QmpClient* m_qmpClient;
	std::string m_qmpHost;
	int m_qmpPort;


#ifdef _WIN32
    void* m_processHandle;     // actually a HANDLE
    unsigned long m_processId; // actually a DWORD
#endif

    bool launchProcess(const std::string& arguments);
    void cleanupProcessHandle();
};