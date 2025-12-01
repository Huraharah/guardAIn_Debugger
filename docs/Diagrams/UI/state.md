# UI State Models

This document describes high-level UI states for guardAInDBG and their relationship to session state.

---

## 1. Session View State Machine

The UI mirrors the CoreEngine's session state but adds a few UI-specific states for loading and busy indicators.

```mermaid
stateDiagram-v2
    [*] --> NoSessionSelected

    NoSessionSelected --> SessionLoading: selectSession()
    SessionLoading --> SessionStaticReady: static analysis loaded
    SessionLoading --> SessionDynamicReady: dynamic-only session loaded
    SessionLoading --> SessionError: load failed

    SessionStaticReady --> StaticAnalyzing: runStaticAnalysis()
    StaticAnalyzing --> SessionStaticReady: analysis completed
    StaticAnalyzing --> SessionError: analysis failed

    SessionStaticReady --> DynamicRunning: startDynamicSession()
    DynamicReady --> DynamicRunning: resume()
    DynamicRunning --> DynamicReady: pause/stop
    DynamicRunning --> SessionError: runtime failure

    SessionStaticReady --> Closed: closeSession()
    DynamicReady --> Closed: closeSession()
    SessionError --> Closed: closeSession()

    state DynamicReady {
        [*] --> AwaitingSnapshots
        AwaitingSnapshots --> SnapshotSelected: selectSnapshot()
        SnapshotSelected --> AwaitingSnapshots: deselectSnapshot()
    }
```
---
## 2. Error Log Visibility State

```mermaid
stateDiagram-v2
    [*] --> Hidden
    Hidden --> Collapsed: first non-fatal error
    Collapsed --> Expanded: user selects "Show Full Logs" or clicks log icon
    Expanded --> Collapsed: user closes drawer
    Collapsed --> Hidden: user clears all logs
```