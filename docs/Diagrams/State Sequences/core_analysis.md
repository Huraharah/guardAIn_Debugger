## Static Analysis Flow (runStaticAnalysis)

```mermaid
sequenceDiagram

    actor User
    participant UI as UI Layer
    participant Core as DebuggerCore
    participant SM as SessionManager
    participant AM as AnalysisManager
    participant BC as IBackendClient/McpClient
    participant MCP as MCP Server
    participant GH as Ghidra

    User->>UI: New Static Session (select binary)
    UI->>Core: createStaticSession(binaryPath)
    Core->>SM: createSession(Static, binaryPath)
    SM-->>Core: sessionId

    User->>UI: Run Static Analysis
    UI->>Core: runStaticAnalysis(sessionId)
    Core->>SM: getSession(sessionId)
    SM-->>Core: Session&
    Core->>AM: runStaticAnalysis(Session&)

    AM->>BC: sendRequest("/ghidra/analyze", {sessionId, binaryPath})
    BC->>MCP: HTTP/JSON request
    MCP->>GH: run headless analysis(binaryPath)
    GH-->>MCP: functions, strings, imports, metadata
    MCP-->>BC: JSON StaticAnalysisResult
    BC-->>AM: BackendResponse(success, payload)

    AM->>AM: parse payload → StaticAnalysisResult
    AM->>Session: session.staticAnalysis = result
    AM-->>Core: StaticAnalysisResult&
    Core-->>UI: StaticAnalysisSummary

    UI->>User: Show overview, functions, suspicious regions
```

### ---------------------------------------------------------------------------

## Dynamic Snapshot and Explanation Flow (captureSnapshot & /context/explain)

```mermaid
sequenceDiagram

    actor User
    participant UI as UI Layer
    participant Core as DebuggerCore
    participant SM as SessionManager
    participant AM as AnalysisManager
    participant BC as IBackendClient/McpClient
    participant MCP as MCP Server
    participant CH as context.py/LLM

    User->>UI: "Explain current state"
    UI->>Core: requestContextSnapshot(sessionId)
    Core->>SM: getSession(sessionId)
    SM-->>Core: Session&

    Core->>AM: captureSnapshot(Session&)

    AM->>AM: collect runtime context\n(call stack, locals, etc.)
    AM->>BC: sendRequest("/context/explain", snapshotJson)
    BC->>MCP: HTTP/JSON request
    MCP->>CH: explain(snapshotJson)
    CH->>CH: build prompt, call LLM/model
    CH-->>MCP: {summary, suspected_issue, important_signals}
    MCP-->>BC: JSON explanation
    BC-->>AM: BackendResponse(success, payload)

    AM->>AM: parse payload → Explanation
    AM->>Session: append Snapshot{rawCtx, explanation}
    AM-->>Core: SnapshotSummary

    Core-->>UI: SnapshotSummary + explanation
    UI->>User: Show call stack, key vars, AI explanation
```

### ---------------------------------------------------------------------------

## State Comparison Flow (compareSnapshots & /context/compare)

```mermaid
sequenceDiagram

    actor User
    participant UI as UI Layer
    participant Core as DebuggerCore
    participant SM as SessionManager
    participant AM as AnalysisManager
    participant BC as IBackendClient/McpClient
    participant MCP as MCP Server
    participant CH as context.py/LLM

    User->>UI: Select Snapshot A & B, click "Compare"
    UI->>Core: compareSnapshots(sessionId, snapAId, snapBId)
    Core->>SM: getSession(sessionId)
    SM-->>Core: Session&
    Core->>AM: compareSnapshots(Session&, snapAId, snapBId)

    AM->>Session: lookup Snapshot A & B
    AM->>AM: compute structural diff\n(or package both snapshots)

    AM->>BC: sendRequest("/context/compare", diffJson)
    BC->>MCP: HTTP/JSON request
    MCP->>CH: compare(diffJson)
    CH->>CH: analyze changes, call LLM/model
    CH-->>MCP: {summary, key_changes[]}
    MCP-->>BC: JSON comparison result
    BC-->>AM: BackendResponse(success, payload)

    AM->>AM: parse payload → SnapshotComparison
    AM->>Session: store comparison result (optional)
    AM-->>Core: SnapshotComparisonSummary
    Core-->>UI: Summary + list of key changes
    UI->>User: Show side-by-side diff and explanation
```

### ---------------------------------------------------------------------------

## Hybrid Flow: Dynamic Snapshot with Static Context

```mermaid
sequenceDiagram

    actor User
    participant UI as UI Layer
    participant Core as DebuggerCore
    participant SM as SessionManager
    participant AM as AnalysisManager
    participant BC as IBackendClient/McpClient
    participant MCP as MCP Server
    participant CH as context.py/LLM

    User->>UI: "Explain current state (rich)"
    UI->>Core: requestContextSnapshot(sessionId)
    Core->>SM: getSession(sessionId)
    SM-->>Core: Session&

    Core->>AM: captureSnapshot(Session&)

    AM->>AM: collect runtime context (call stack, locals)
    AM->>Session: read staticAnalysis (if present)
    AM->>AM: build hybridContextJson\n{snapshot, relatedFunctionSummaries}

    AM->>BC: sendRequest("/context/explain", hybridContextJson)
    BC->>MCP: HTTP/JSON request
    MCP->>CH: explain(hybridContextJson)
    CH->>CH: combine static + dynamic view,\ncall LLM/model
    CH-->>MCP: enriched explanation
    MCP-->>BC: JSON response
    BC-->>AM: BackendResponse(success, payload)

    AM->>Session: attach enriched explanation to snapshot
    AM-->>Core: SnapshotSummary (with deeper context)
    Core-->>UI: Update snapshot view
    UI->>User: Show explanation referencing\nboth runtime state and static function behavior
```