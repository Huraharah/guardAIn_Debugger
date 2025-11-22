## Use Case 1: Create and Run a Static Analysis Session

### Name: UC-01 – Static Binary Analysis with Ghidra
Primary Actor: Sam (Reverse Engineer)
#### Preconditions:

guardAIn Debugger is installed and running.

MCP backend and Ghidra integration are configured.

#### Main Flow:

User selects “New Analysis Session -> Static Binary” in the UI.

#### UI prompts for:

Binary path

Optional tags/notes

UI calls DebuggerCore::startNewSession(targetPath, mode=Static).

#### DebuggerCore:

Creates a session record.

Invokes IBackendClient::sendRequest("ghidra/analyze", {binary_path}).

#### MCP server receives request:

Calls handlers/ghidra.py to run Ghidra headless on the binary.

Extracts relevant artifacts (functions, strings, imports, sections, CFG metadata).

Optionally calls model_introspection/AI logic to generate a high-level summary.

Returns JSON with summary + key features.

DebuggerCore parses the response into internal structures and updates session state.

#### UI displays:

High-level summary (“This binary appears to…”)

Overview panel (entry points, function count, suspicious patterns).

#### Alternate / Error Flows:

Binary is invalid or cannot be opened -> backend returns error -> UI shows actionable message.

Ghidra not configured -> backend indicates missing dependency -> UI offers configuration help.

## ---------------------------------------------------------------------------

## Use Case 2: Capture Context Snapshot at Breakpoint

### Name: UC-02 – Breakpoint Snapshot & Explanation
Primary Actor: Alex (Developer)
#### Preconditions:

A dynamic debugging session is active.

Debugger is paused at a breakpoint.

#### Main Flow:

User clicks “Capture Snapshot & Explain” in the UI.

UI sends request to DebuggerCore::requestContextSnapshot(sessionId).

#### DebuggerCore:

Collects available local context: call stack, local variables, relevant memory regions.

Packages this into a JSON payload.

Calls IBackendClient::sendRequest("context/explain", snapshotJson).

MCP backend (context.py handler):

Normalizes the context into a model-friendly format.

Calls the AI model (or MCP tool) with a prompt like:

“Given this call stack, variables, and memory state, describe what the program is doing and what is unusual.”

Returns a structured explanation (e.g., summary, suspected_issue, important_signals).

DebuggerCore updates the session with this explanation object.

#### UI displays:

A natural-language explanation panel.

Links to “jump to” related frames/variables in the UI.

#### Alternate Flows:

Context capture fails (unsupported platform) -> message: “Dynamic snapshots not available for this target.”

AI call fails/timeouts ? UI shows partial snapshot and suggests retry.

## ---------------------------------------------------------------------------

## Use Case 3: Compare Two Snapshots

### Name: UC-03 – State Diff & Explanation
Primary Actor: Alex (Developer)
#### Preconditions:

At least two snapshots exist in the session.

#### Main Flow:

User opens the Snapshots view and selects Snapshot A and Snapshot B.

UI calls DebuggerCore::compareSnapshots(sessionId, snapshotAId, snapshotBId).

#### DebuggerCore:

Computes a structural diff of key elements (variables, memory ranges, call stack).

Sends diff to backend via IBackendClient::sendRequest("context/compare", diffJson).

MCP backend (context.py or similar):

Analyzes the diff.

#### Produces:

List of important changes.

Natural-language explanation of what likely happened between A and B.

DebuggerCore returns comparison object to the UI.

#### UI shows:

Side-by-side snapshot comparison.

“Key changes” list.

AI explanation text.

#### Alternate Flows:

Snapshots are incompatible (different architectures) -> error message: “Cannot compare snapshots from different targets.”

## ---------------------------------------------------------------------------

#### Use Case 4: Suspicious Behavior Highlight

### Name: UC-04 – Suspicious Pattern Detection in Binary
Primary Actor: Sam (Security Analyst)
#### Preconditions:

UC-01 has been completed, static analysis artifacts are available.

#### Main Flow:

User clicks “Highlight Suspicious Behavior” in the analysis view.

UI calls something like DebuggerCore::runBehaviorScan(sessionId).

#### DebuggerCore:

Extracts relevant artifacts (function summaries, strings, imports).

Sends them to backend via IBackendClient::sendRequest("ghidra/behavior_scan", artifactsJson).

#### MCP backend:

Applies pattern-matching (rule-based + AI classification).

Returns a list of flagged regions with reasons/scores.

DebuggerCore attaches these flags to session metadata.

UI visually marks suspicious functions/regions and displays rationales when hovered/selected.

#### Alternate Flows:

No suspicious patterns found -> UI shows “No issues detected” message.

Backend error during scan -> UI shows error and suggests retrying later.

## ---------------------------------------------------------------------------