# UI Components (guardAInDBG)

This document describes the main UI components of the guardAInDBG desktop application and how they interact with the CoreEngine (`DebuggerCore`).

The UI is a single-window desktop app with a left "navigator" pane, a tabbed main display, a bottom control strip, and an optional error log drawer.

---

## 1. Window Layout Overview

- **MainWindow**
  - Top: Menu bar
  - Left: Navigator pane (Sessions, Functions, Snapshots, Error Log)
  - Center: Tabbed main display area
  - Bottom: Target/output controls + status bar
  - Optional bottom drawer: Error Log (expanded view)

---

## 2. Navigator Pane Components

### 2.1 SessionListView

**Responsibilities**

- Display a list of active and saved sessions.
- Allow the user to select the current session.
- Indicate session status (New, Analyzing, Ready, Running, Error, Closed).

**Displayed fields (per session)**

- Session ID
- Target path
- Session type (Static / Dynamic / Hybrid)
- Status badge (e.g., Running, Ready, Error)
- Optional icon for error / warning

**Key actions → CoreEngine**

- `New Static Session` → `DebuggerCore::createStaticSession(targetPath)`
- `New Dynamic Session` → `DebuggerCore::createDynamicSession(targetPath)`
- `New Hybrid Session` → `DebuggerCore::createHybridSession(targetPath)` (wrapper over static+dynamic)
- `Load Session` → `DebuggerCore::loadSession(sessionId)`
- `Close Session` → `DebuggerCore::closeSession(sessionId)`
- `Terminate Session` → `DebuggerCore::terminateDynamicSession(sessionId)`
- `Select Session` → updates current `sessionId` in the UI state; subsequent actions use this.

---

### 2.2 FunctionListView

**Responsibilities**

- Show functions discovered in static analysis for the selected session.
- Allow navigation to a specific function’s details.

**Displayed fields**

- Function name (possibly renamed)
- Address
- Flags (e.g., suspicious, calls network APIs, crypto)

**Key actions → CoreEngine**

- On select function:
  - `DebuggerCore::getFunctionDetails(sessionId, functionId)`  
    → Updates Static Analysis tab.
- Context menu actions:
  - `Explain Function` → `DebuggerCore::requestFunctionExplanation(sessionId, functionId)`
  - `Mark as Suspicious` → `DebuggerCore::tagFunction(sessionId, functionId, "suspicious")`

---

### 2.3 SnapshotListView

**Responsibilities**

- Show all captured snapshots for the selected session.
- Allow the user to pick snapshots to inspect or compare.

**Displayed fields**

- Snapshot ID / index
- Instruction address / function name
- Timestamp
- Optional tags (e.g., “after network call”, “crypto init”)

**Key actions → CoreEngine**

- On select snapshot:
  - `DebuggerCore::getSnapshotDetails(sessionId, snapshotId)`  
    → Updates Dynamic Snapshots tab.
- On compare snapshots (multi-select):
  - `DebuggerCore::compareSnapshots(sessionId, snapshotA, snapshotB)`  
    → MCP `/context/compare`, result shown in AI Explanations tab.
- On capture (button elsewhere but logically related):
  - `DebuggerCore::captureSnapshot(sessionId)`  
    → RuntimeEngine snapshot, then UI refresh.

---

### 2.4 ErrorLogView (Navigator entry)

**Responsibilities**

- Show a rolling list of non-fatal warnings and errors.
- Allow user to open the full Error Log drawer.

**Displayed fields**

- Timestamp
- Severity icon (Info / Warning / Error)
- Source (CoreEngine / MCP / RuntimeEngine / UI)
- Message text
- Optional related sessionId / snapshotId

**Key actions**

- Click on log entry:
  - If it relates to a session/snapshot, UI navigates to that context.
- Menu: “Show Full Logs” → expands full Error Log drawer.

---

## 3. Main Display Tabs

### 3.1 Static Analysis Tab

**Responsibilities**

- Present static analysis results for the selected session.
- Provide context for functions, imports, strings, and suspicious patterns.

**Layout**

- Optional left sub-pane:
  - Filter controls (functions / imports / strings / suspicious)
- Main pane:
  - Summary header (session + function)
  - Pretty-formatted analysis details (with toggle between pretty/raw JSON)

**Key actions → CoreEngine**

- `DebuggerCore::runStaticAnalysis(sessionId)`  
  (also accessible via bottom “Run Static Analysis” button)
- `DebuggerCore::getStaticSummary(sessionId)`
- `DebuggerCore::requestFunctionExplanation(sessionId, functionId)`

---

### 3.2 Dynamic Snapshots Tab

**Responsibilities**

- Provide detailed view of runtime snapshots.
- Act as the main UI for dynamic analysis.

**Layout**

- Left: snapshot timeline/list (bound to SnapshotListView selection)
- Right: snapshot details:
  - Registers
  - Call stack
  - Locals
  - Notable memory values / events

**Key actions → CoreEngine**

- `DebuggerCore::captureSnapshot(sessionId)`
- `DebuggerCore::getSnapshotDetails(sessionId, snapshotId)`
- `DebuggerCore::requestSnapshotExplanation(sessionId, snapshotId)`  
  (MCP `/context/explain`)
- `DebuggerCore::compareSnapshots(sessionId, snapshotA, snapshotB)`

---

### 3.3 Dynamic Analysis Results Tab

**Responsibilities**

- Show an aggregated, higher-level view of runtime behavior.

**Layout**

- Left: list of phases or key events (if available), e.g.:
  - Initialization
  - Credential Harvesting
  - Exfiltration Attempt
- Right: phase details and summary:
  - Key API calls
  - Snapshot references
  - AI commentary

**Key actions → CoreEngine**

- `DebuggerCore::getDynamicSummary(sessionId)`
- Possible future: `DebuggerCore::requestBehaviorExplanation(sessionId, phaseId)`

---

### 3.4 AI Explanations Tab

**Responsibilities**

- Central place to view explanations generated by the MCP/LLM.

**Layout**

- Left (optional): list of explanation items
  - Static scan summary
  - Function explanations
  - Snapshot explanations
  - Snapshot comparisons
- Right: explanation content (rich text)

**Key actions → CoreEngine**

- `DebuggerCore::getExplanationList(sessionId)`
- `DebuggerCore::getExplanationDetails(sessionId, explanationId)`
- `DebuggerCore::refreshExplanation(sessionId, explanationId)` (re-request from MCP)

---

### 3.5 AI Direct Prompting / Conversation Tab

**Responsibilities**

- Allow the user to directly converse with the LLM with optional context.

**Layout**

- Main: chat-like history of prompts and responses.
- Bottom: prompt input box + context controls.

**Key actions → CoreEngine**

- `DebuggerCore::sendCustomPrompt(sessionId, prompt, options)`  
  (options may include “attach current static summary”, “attach current snapshot”)
- `DebuggerCore::getPromptHistory(sessionId)`

---

## 4. Bottom Control Strip

### 4.1 Target Path Controls

- **Target path input box**
  - Manual entry or file picker dialog.
- **Validation**
  - UI validates path existence; errors go to ErrorLogView (non-fatal) or popup (fatal on run).

**Key actions → CoreEngine**

- Sets `targetPath` when creating new sessions.

---

### 4.2 Output Path / Export Controls

- Output directory input box + folder picker.
- “Export Results” button.

**Key actions → CoreEngine**

- `DebuggerCore::exportSessionResults(sessionId, outputPath)`
- Errors routed to ErrorLogView if export fails.

---

### 4.3 Automated Execution Buttons

- **Run Static Analysis**
  - `DebuggerCore::runStaticAnalysis(sessionId)`
- **Run Dynamic Analysis**
  - `DebuggerCore::startDynamicSession(sessionId)`
- **Run Hybrid Analysis**
  - `DebuggerCore::runHybridAnalysis(sessionId)`  
    (static + runtime orchestration)

Buttons are enabled/disabled based on session state.

---

## 5. Status Bar

**Responsibilities**

- Show global UI/application status.

**Typical states**

- `Ready`
- `Running static analysis…`
- `Starting dynamic environment…`
- `Waiting for MCP response…`
- `Error: see Error Log for details`

Status bar is updated based on callbacks from `DebuggerCore` (e.g. completion events, error events).
