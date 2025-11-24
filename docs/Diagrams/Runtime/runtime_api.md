# Runtime Subsystem API Specification

**Masters Thesis Project — guardAIn Debugger**
**Version:** 0.1 (Draft)

---

## 1. Overview

The **Runtime subsystem** provides controlled, sandboxed execution of target programs for **dynamic analysis**. It is responsible for:

* Creating and managing isolated environments (e.g., QEMU VMs, containers)
* Launching and controlling target programs inside those environments
* Capturing **raw runtime snapshots** (call stack, registers, key memory/state)
* Reporting runtime status and errors back to the **Core Engine**

This document defines the **internal C++ API contract** between:

* The **Core Engine** (primarily `DebuggerCore` and `SessionManager` / `AnalysisManager`)
* The **Runtime subsystem**, via the `RuntimeManager` class

Unlike `mcp_api.md`, this is **not** a network/JSON API. It is a **C++ interface specification** used inside the same process.

---

## 2. Architectural Context

At a high level, the architecture separates:

* **Analysis Backend (MCP)** – static/dynamic reasoning (Ghidra, LLM, naming, explanations)
* **Runtime Backend** – execution environment (QEMU/containers, debug adapter, target process)

`RuntimeManager` is the C++ façade that **orchestrates the Runtime Backend** on behalf of the Core Engine.

Conceptually:

* `DebuggerCore` owns a `RuntimeManager` instance.

* When a **dynamic** or **hybrid** session is created, `DebuggerCore` uses `RuntimeManager` to:

  * Create an environment
  * Launch the target
  * Control execution
  * Capture raw snapshots

* `AnalysisManager` then takes those snapshots, enriches them, and may send them to the MCP backend for explanation or comparison.

---

## 3. Design Goals

The Runtime subsystem is designed to:

1. **Isolate dynamic execution** from the rest of the system

   * Well-defined API, no direct QEMU/VM calls from UI or other components

2. **Support multiple backend implementations**

   * QEMU VM
   * Container-based sandbox
   * Host process (for testing/dev)

3. **Provide deterministic lifecycle control**

   * Clear states for environments and targets (NotStarted, Running, Paused, Terminated, Error)

4. **Capture reusable runtime snapshots**

   * Raw, structured data that can be:

     * Stored in `Session`
     * Enriched into higher-level `Snapshot` models
     * Sent to MCP for `/context/explain` and `/context/compare`

5. **Integrate cleanly with `SessionState` and error handling**

   * Environment failures map to `SessionState::Error`
   * Snapshots map to `SessionState::SnapshotAvailable`

---

## 4. Public C++ Interface: `RuntimeManager`

This section defines the **public surface** of the Runtime subsystem as seen by `DebuggerCore`.

### 4.1 Namespaces and lifetime

`RuntimeManager` lives in the same namespace as the Core Engine:

```cpp
namespace guardain::core {

class RuntimeManager {
public:
    RuntimeManager();
    ~RuntimeManager();

    RuntimeManager(const RuntimeManager&) = delete;
    RuntimeManager& operator=(const RuntimeManager&) = delete;

    // Methods defined below...
};

} // namespace guardain::core
```

`DebuggerCore` owns a single `RuntimeManager` instance for the lifetime of the application.

---

### 4.2 Type aliases

For clarity and decoupling from concrete implementations:

```cpp
using EnvironmentId = std::string; // e.g., "env-1234"
using TargetId      = std::string; // e.g., "tgt-0001"
```

These are opaque identifiers from the Core Engine perspective.

---

### 4.3 Environment lifecycle API

#### 4.3.1 Create environment

```cpp
std::pair<EnvironmentId, RuntimeError>
createEnvironment(const RuntimeConfig& config);
```

* Creates a new sandbox/VM/container with the given `RuntimeConfig`.
* Returns:

  * `EnvironmentId` for subsequent operations
  * `RuntimeError` with `code == RuntimeErrorCode::None` on success

#### 4.3.2 Destroy environment

```cpp
RuntimeError destroyEnvironment(const EnvironmentId& envId);
```

* Stops and tears down the environment if it exists.
* Invalidates any associated `TargetId`s.
* Safe to call multiple times; no-op if already destroyed.

#### 4.3.3 Query environment status

```cpp
RuntimeStatus getEnvironmentStatus(const EnvironmentId& envId) const;

std::vector<EnvironmentInfo> listEnvironments() const;
```

* Provides status for a single environment or a list of all managed environments.

---

### 4.4 Target process control API

#### 4.4.1 Launch target

```cpp
std::pair<TargetId, RuntimeError>
launchTarget(const EnvironmentId& envId,
             const TargetLaunchConfig& launchConfig);
```

* Launches the target program inside the specified environment.
* Optionally sets up debug hooks (gdbserver, custom protocol, etc.).
* On success, returns a `TargetId`.

#### 4.4.2 Execution control

```cpp
RuntimeError pauseTarget(const EnvironmentId& envId,
                         const TargetId& targetId);

RuntimeError resumeTarget(const EnvironmentId& envId,
                          const TargetId& targetId);

RuntimeError terminateTarget(const EnvironmentId& envId,
                             const TargetId& targetId);
```

* High-level control over the target process execution.

#### 4.4.3 Target status

```cpp
TargetStatus getTargetStatus(const EnvironmentId& envId,
                             const TargetId& targetId) const;

std::vector<TargetInfo>
listTargets(const EnvironmentId& envId) const;
```

---

### 4.5 Snapshot capture API

#### 4.5.1 Capture raw runtime snapshot

```cpp
std::pair<RawRuntimeSnapshot, RuntimeError>
captureSnapshot(const EnvironmentId& envId,
                const TargetId& targetId,
                const SnapshotCaptureOptions& options);
```

* Captures a single snapshot of the target state:

  * Call stack
  * Registers
  * Selected variables/memory regions
  * Timestamp
* `RawRuntimeSnapshot` is a *runtime-level* representation, suitable for:

  * Storing in `Session`
  * Converting into the higher-level `Snapshot` model
  * Sending as JSON to MCP (`/context/explain`, `/context/compare`)

#### 4.5.2 Convenience API for integration with Session

```cpp
std::pair<RawRuntimeSnapshot, RuntimeError>
captureSnapshotForSession(const std::string& sessionId,
                          const SnapshotCaptureOptions& options);
```

* Optional convenience method that:

  * Looks up the environment/target associated with a given `Session`
  * Captures a snapshot in that context

The Core Engine may use this to keep `DebuggerCore` code simpler.

---

### 4.6 Error introspection

```cpp
RuntimeError getLastError(const EnvironmentId& envId) const;
RuntimeError getLastError() const; // global last error
```

* Provides the last error encountered for a specific environment or overall.

---

## 5. Data Types and Models

This section defines the data structures passed between the Core Engine and the Runtime subsystem.

### 5.1 RuntimeConfig

Describes how an environment should be created.

```cpp
enum class SandboxKind {
    QemuVM,
    Container,
    HostProcess // for testing / fallback
};

struct RuntimeConfig {
    std::string    name;               // logical name (for UI/logging)
    SandboxKind    kind;
    std::filesystem::path baseImage;   // disk image, container base, etc.
    std::filesystem::path workingDir;  // working directory inside environment

    bool           enableNetworking = false;
    bool           enableFileSharing = false;

    // Optional resource limits
    uint32_t       cpuCores = 1;
    uint64_t       memoryLimitMB = 1024;

    // Optional extra environment variables/settings
    std::map<std::string, std::string> environmentVars;
};
```

---

### 5.2 EnvironmentInfo

Summary of a running or configured environment.

```cpp
struct EnvironmentInfo {
    EnvironmentId  id;
    std::string    name;
    SandboxKind    kind;
    RuntimeStatus  status;
    std::string    backendDetails; // free-form (e.g., "QEMU x86_64, image=...")

    // Optional stats
    uint64_t       uptimeSeconds = 0;
    uint32_t       activeTargets = 0;
};
```

---

### 5.3 RuntimeStatus

State of an environment’s lifecycle.

```cpp
enum class RuntimeStatus {
    NotCreated,   // before createEnvironment
    Creating,
    Ready,        // environment up, no targets running
    Running,      // at least one target running
    Paused,       // all targets paused
    Stopping,
    Terminated,
    Error
};
```

---

### 5.4 TargetLaunchConfig

Describes how to start the target program inside an environment.

```cpp
struct TargetLaunchConfig {
    std::filesystem::path binaryPath;
    std::vector<std::string> arguments;
    std::vector<std::string> environmentVars;
    std::string stdinData;          // optional, for scripted startup
    bool attachDebugger = true;     // enable debug/instrumentation hooks
};
```

---

### 5.5 TargetInfo and TargetStatus

```cpp
enum class TargetStatus {
    NotStarted,
    Launching,
    Running,
    Paused,
    Exited,
    Crashed,
    Error
};

struct TargetInfo {
    TargetId      id;
    std::string   name;             // e.g. binary file name
    TargetStatus  status;
    int           exitCode;         // valid if Exited
    std::string   lastErrorMessage;
};
```

---

### 5.6 RawRuntimeSnapshot

This is the low-level, runtime-focused snapshot of program state.

```cpp
struct RawRuntimeSnapshot {
    EnvironmentId  environmentId;
    TargetId       targetId;
    std::string    snapshotId;      // unique within session/environment
    std::chrono::system_clock::time_point timestamp;

    std::vector<StackFrameRaw> callStack;
    std::map<std::string, ValueRaw> locals; // simplified locals view
    std::vector<std::string> notes;         // any runtime notes/tags

    // Optional raw memory/register state (implementation-dependent)
    std::string    rawRegisterDump;
    std::string    rawMemorySummaryJson;
};
```

**Note:**
`StackFrameRaw` and `ValueRaw` can be thin versions of the types already defined in your core data model (`StackFrame`, `Value`), or re-use those directly.

---

### 5.7 SnapshotCaptureOptions

Controls what kind of data is captured.

```cpp
struct SnapshotCaptureOptions {
    bool captureCallStack   = true;
    bool captureLocals      = true;
    bool captureRegisters   = false;
    bool captureMemory      = false;

    // Optional: restrict to a single thread, frame depth, etc.
    uint32_t maxStackDepth  = 64;
};
```

---

### 5.8 RuntimeError and RuntimeErrorCode

```cpp
enum class RuntimeErrorCode {
    None = 0,

    EnvironmentNotFound,
    TargetNotFound,
    EnvironmentCreationFailed,
    TargetLaunchFailed,
    CommunicationError,
    SnapshotFailed,
    InvalidState,
    InternalError
};

struct RuntimeError {
    RuntimeErrorCode code = RuntimeErrorCode::None;
    std::string      message; // human-readable

    explicit operator bool() const {
        return code != RuntimeErrorCode::None;
    }
};
```

---

## 6. Behavior and Lifecycle Rules

### 6.1 Environment lifecycle

* Environments start in `RuntimeStatus::NotCreated`.
* After `createEnvironment(config)`:

  * On success: `Ready`
  * On failure: `Error`
* When at least one target is running: `Running`
* When all targets are paused: `Paused`
* During teardown: `Stopping` → `Terminated`
* Any unrecoverable VM/backend failure → `Error`, then `Terminated` after cleanup.

### 6.2 Target lifecycle

* Targets begin in `TargetStatus::NotStarted`.
* After `launchTarget`:

  * `Launching` → `Running` on success
  * `Error` on failure
* `pauseTarget` → `Paused`
* `resumeTarget` → `Running`
* `terminateTarget` → `Exited` (or `Crashed` if abnormal)

### 6.3 Integration with SessionState

The Core Engine maps runtime events to `SessionState`:

* When the first target starts successfully:

  * `SessionState` → `DynamicRunning`
* After a successful snapshot:

  * `SessionState` → `SnapshotAvailable`
* On unrecoverable runtime error:

  * `SessionState` → `Error`

---

## 7. Error Handling Semantics

* Runtime operations **do not throw** for environment/target errors; they return `RuntimeError` or `(value, RuntimeError)` pairs.
* Exceptions may be used for programmer errors (e.g., invalid API usage), but *not* for runtime conditions like “target crashed”.
* The Core Engine is responsible for:

  * Logging runtime errors via `BackendMessage` (or a parallel runtime log type)
  * Updating `SessionState` to `Error` when appropriate
  * Surfacing user-friendly messages in the UI

---

## 8. Threading and Concurrency

* Calls to `RuntimeManager` are assumed to be made from the **Core/worker thread**, not directly from the UI thread.
* Snapshot capture (`captureSnapshot`) is **logically synchronous** in v0.1:

  * The call blocks until the snapshot is collected or the operation fails.
  * Future versions may introduce an asynchronous variant.
* Implementations must ensure:

  * Internal synchronization for environment/target state
  * Safe access to QEMU/VM/container APIs from one or more internal threads

The Core Engine’s `threading_model.md` should note that `RuntimeManager` belongs to the core/worker execution domain.

---

## 9. Integration Points with Other Subsystems

### 9.1 With DebuggerCore

Typical flow for a dynamic session:

1. User creates a **dynamic** or **hybrid** session.

2. `DebuggerCore`:

   * Builds a `RuntimeConfig`
   * Calls `RuntimeManager::createEnvironment`
   * Calls `RuntimeManager::launchTarget`
   * Associates `EnvironmentId` + `TargetId` with the `Session`.

3. On “Capture snapshot”:

   * `DebuggerCore` or `AnalysisManager` calls `captureSnapshot` or `captureSnapshotForSession`.

### 9.2 With AnalysisManager and MCP

1. `AnalysisManager` receives a `RawRuntimeSnapshot`.
2. It converts this to the higher-level `Snapshot` model (per `data_models.md`).
3. It may send a JSON version of the snapshot to MCP `/context/explain` or `/context/compare`.
4. The resulting `Explanation` / `SnapshotComparison` is attached to the `Session`.

---

## 10. Future Extensions (Reserved)

The following are reserved for future versions and are not required in v0.1:

* Streaming trace collection (continuous snapshots/trace logs)
* Breakpoint configuration API (set/remove breakpoints symbolically)
* Multi-target per environment (more than one process per VM/container)
* Async variants of snapshot/launch/termination operations
* Richer memory inspection (per-region, typed views)
