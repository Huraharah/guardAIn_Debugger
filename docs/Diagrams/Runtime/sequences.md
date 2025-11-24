## 1️⃣ Dynamic Session Start: Create Environment + Launch Target

This covers:

* User creates a **dynamic** session
* Core creates a runtime environment
* Core launches target inside it
* Session gets wired to env/target

```mermaid
sequenceDiagram
    participant User
    participant UI as UI Layer
    participant DC as DebuggerCore
    participant SM as SessionManager
    participant RM as RuntimeManager
    participant ER as EnvironmentRegistry
    participant SC as SandboxController
    participant TC as TargetController

    User->>UI: Create new session (Dynamic/Hybrid)
    UI->>DC: createDynamicSession(request)

    DC->>SM: createSession(config)
    SM-->>DC: sessionId

    Note over DC: Build RuntimeConfig

    DC->>RM: createEnvironment(runtimeConfig)
    RM->>SC: provisionSandbox(runtimeConfig)
    SC-->>RM: envId or error

    alt env creation succeeds
        RM->>ER: registerEnvironment(sessionId, envId)
        RM-->>DC: envId
    else env creation fails
        RM-->>DC: RuntimeError(EnvironmentCreationFailed)
        DC->>SM: updateSessionState(sessionId, Error)
        DC-->>UI: report error
        DC-->>RM: abort flow
    end

    Note over DC: Build TargetLaunchConfig

    DC->>RM: launchTarget(envId, launchConfig)
    RM->>TC: startTarget(envId, launchConfig)
    TC-->>RM: targetId or error

    alt target launch succeeds
        RM->>ER: registerTarget(sessionId, envId, targetId)
        RM-->>DC: targetId
        DC->>SM: attachRuntimeIds(sessionId, envId, targetId)
        DC->>SM: updateSessionState(sessionId, DynamicRunning)
        DC-->>UI: session ready
    else target launch fails
        RM-->>DC: RuntimeError(TargetLaunchFailed)
        DC->>SM: updateSessionState(sessionId, Error)
        DC-->>UI: report launch error
    end

```
---

## 2️⃣ Snapshot Capture: Runtime → Core → MCP Explain

This one shows:

* User hits “Capture snapshot & explain”
* Core asks RuntimeManager for a **RawRuntimeSnapshot**
* AnalysisManager enriches it into a `Snapshot`
* Optional MCP `/context/explain` call
* Result attached to Session

```mermaid
sequenceDiagram
    participant User
    participant UI as UI Layer
    participant DC as DebuggerCore
    participant SM as SessionManager
    participant RM as RuntimeManager
    participant SNAP as SnapshotCollector
    participant TC as TargetController
    participant AM as AnalysisManager
    participant BC as McpClient
    participant MCP as MCP Backend

    User->>UI: Capture snapshot & explain
    UI->>DC: requestSnapshotExplain(sessionId)

    DC->>SM: getRuntimeBinding(sessionId)
    SM-->>DC: envId, targetId

    Note over DC: Prepare SnapshotCaptureOptions<br>(e.g. callStack+locals)

    DC->>RM: captureSnapshot(envId, targetId, options)
    RM->>TC: ensureTargetPausedOrConsistent(targetId)
    TC-->>RM: ok

    RM->>SNAP: collectSnapshot(envId, targetId, options)
    SNAP->>TC: readState(call stack, locals, regs, memory)
    TC-->>SNAP: raw state data
    SNAP-->>RM: RawRuntimeSnapshot

    RM-->>DC: RawRuntimeSnapshot, RuntimeError(None)

    alt snapshot capture failed
        RM-->>DC: invalid snapshot, RuntimeError(SnapshotFailed)
        DC->>SM: updateSessionState(sessionId, Error)
        DC-->>UI: report snapshot error
        DC-->>RM: abort flow
    end

    Note over DC,AM: Convert RawRuntimeSnapshot<br>-> Snapshot (core data model)

    DC->>AM: buildSnapshot(sessionId, RawRuntimeSnapshot)
    AM-->>DC: Snapshot instance

    Note over AM: Prepare /context/explain payload<br>(call stack + locals + optional static context)

    AM->>BC: POST /context/explain (snapshot+staticContext)
    BC->>MCP: /context/explain
    MCP-->>BC: Explanation JSON
    BC-->>AM: Explanation

    Note over AM: attach Explanation to Snapshot<br>and save to Session

    AM->>SM: saveSnapshot(sessionId, Snapshot)
    AM->>SM: updateSessionState(sessionId, SnapshotAvailable)

    AM-->>DC: success
    DC-->>UI: show snapshot + explanation<br>>(call stack, locals, summary)
```
---

## 3️⃣ Session Shutdown: Terminate Target & Destroy Environment

This covers:

* User closes a dynamic session
* Core terminates the target
* Core destroys environment
* Session transitions to `Completed` / `Closed`

```mermaid
sequenceDiagram
    participant User
    participant UI as UI Layer
    participant DC as DebuggerCore
    participant SM as SessionManager
    participant RM as RuntimeManager
    participant ER as EnvironmentRegistry
    participant TC as TargetController
    participant SC as SandboxController

    User->>UI: Close session / Stop debugging
    UI->>DC: closeSession(sessionId)

    DC->>SM: getRuntimeBinding(sessionId)
    SM-->>DC: envId, targetId

    alt targetId is valid
        DC->>RM: terminateTarget(envId, targetId)
        RM->>TC: terminate(targetId)
        TC-->>RM: RuntimeError(None) or error

        alt terminate ok
            RM-->>DC: RuntimeError(None)
        else terminate error
            RM-->>DC: RuntimeError(CommunicationError or InternalError)
            DC->>SM: addRuntimeWarning(sessionId,<br>"Target may not have terminated cleanly")
        end
    else no active target
        Note over DC: No target to terminate
    end

    DC->>RM: destroyEnvironment(envId)
    RM->>SC: destroySandbox(envId)
    SC-->>RM: RuntimeError(None) or error

    alt destroy ok
        RM->>ER: unregisterEnvironment(envId)
        RM-->>DC: RuntimeError(None)
        DC->>SM: updateSessionState(sessionId, Completed)
    else destroy error
        RM-->>DC: RuntimeError(EnvironmentCreationFailed or InternalError)
        DC->>SM: updateSessionState(sessionId, Error)
    end

    DC->>SM: markSessionClosed(sessionId)
    DC-->>UI: session closed
```
