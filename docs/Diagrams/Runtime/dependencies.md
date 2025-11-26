```mermaid
graph TD
    %% Core side
    DC[DebuggerCore]
    SM[SessionManager]

    %% Runtime subsystem
    RM[RuntimeManager]
    ER[EnvironmentRegistry]
    SC[SandboxController <br/>interface]
    QSC[QemuSandboxController]
    CSC[ContainerSandboxController]
    TC[TargetController]
    SNAP[SnapshotCollector]
    DAC[DebugAdapterClient]

    %% Backend/system-level entities
    QEMU[QEMU / VM Process]
    CONT[Container Runtime]
    DBG[Debug Adapter<br/>gdbserver/custom]
    TARGET[Target Program]

    %% Core depends on Runtime
    DC --> RM
    DC --> SM

    %% RuntimeManager dependencies
    RM --> ER
    RM --> SC
    RM --> TC
    RM --> SNAP

    %% SandboxController implementations
    QSC --> SC
    CSC --> SC

    %% Controllers depend on backend clients
    QSC --> QEMU
    CSC --> CONT

    TC --> DAC
    SNAP --> DAC

    DAC --> DBG
    DBG --> TARGET
    QEMU --> TARGET
